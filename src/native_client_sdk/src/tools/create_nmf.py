#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import errno
import json
import optparse
import os
import re
import shutil
import struct
import subprocess
import sys

import getos
import quote

if sys.version_info < (2, 6, 0):
  sys.stderr.write("python 2.6 or later is required run this script\n")
  sys.exit(1)

NeededMatcher = re.compile('^ *NEEDED *([^ ]+)\n$')
FormatMatcher = re.compile('^(.+):\\s*file format (.+)\n$')

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

OBJDUMP_ARCH_MAP = {
    # Names returned by Linux's objdump:
    'elf64-x86-64': 'x86-64',
    'elf32-i386': 'x86-32',
    'elf32-little': 'arm',
    'elf32-littlearm': 'arm',
    # Names returned by x86_64-nacl-objdump:
    'elf64-nacl': 'x86-64',
    'elf32-nacl': 'x86-32',
}

ARCH_LOCATION = {
    'x86-32': 'lib32',
    'x86-64': 'lib64',
    'arm': 'lib',
}


# These constants are used within nmf files.
RUNNABLE_LD = 'runnable-ld.so'  # Name of the dynamic loader
MAIN_NEXE = 'main.nexe'  # Name of entry point for execution
PROGRAM_KEY = 'program'  # Key of the program section in an nmf file
URL_KEY = 'url'  # Key of the url field for a particular file in an nmf file
FILES_KEY = 'files'  # Key of the files section in an nmf file
PNACL_OPTLEVEL_KEY = 'optlevel' # key for PNaCl optimization level
PORTABLE_KEY = 'portable' # key for portable section of manifest
TRANSLATE_KEY = 'pnacl-translate' # key for translatable objects


# The proper name of the dynamic linker, as kept in the IRT.  This is
# excluded from the nmf file by convention.
LD_NACL_MAP = {
    'x86-32': 'ld-nacl-x86-32.so.1',
    'x86-64': 'ld-nacl-x86-64.so.1',
    'arm': None,
}


def DebugPrint(message):
  if DebugPrint.debug_mode:
    sys.stderr.write('%s\n' % message)


DebugPrint.debug_mode = False  # Set to True to enable extra debug prints


def MakeDir(dirname):
  """Just like os.makedirs but doesn't generate errors when dirname
  already exists.
  """
  if os.path.isdir(dirname):
    return

  Trace("mkdir: %s" % dirname)
  try:
    os.makedirs(dirname)
  except OSError as exception_info:
    if exception_info.errno != errno.EEXIST:
      raise


class Error(Exception):
  '''Local Error class for this file.'''
  pass


def ParseElfHeader(path):
  """Determine properties of a nexe by parsing elf header.
  Return tuple of architecture and boolean signalling whether
  the executable is dynamic (has INTERP header) or static.
  """
  # From elf.h:
  # typedef struct
  # {
  #   unsigned char e_ident[EI_NIDENT]; /* Magic number and other info */
  #   Elf64_Half e_type; /* Object file type */
  #   Elf64_Half e_machine; /* Architecture */
  #   ...
  # } Elf32_Ehdr;
  elf_header_format = '16s2H'
  elf_header_size = struct.calcsize(elf_header_format)

  with open(path, 'rb') as f:
    header = f.read(elf_header_size)

  try:
    header = struct.unpack(elf_header_format, header)
  except struct.error:
    raise Error("error parsing elf header: %s" % path)
  e_ident, _, e_machine = header[:3]

  elf_magic = '\x7fELF'
  if e_ident[:4] != elf_magic:
    raise Error('Not a valid NaCL executable: %s' % path)

  e_machine_mapping = {
    3 : 'x86-32',
    40 : 'arm',
    62 : 'x86-64'
  }
  if e_machine not in e_machine_mapping:
    raise Error('Unknown machine type: %s' % e_machine)

  # Set arch based on the machine type in the elf header
  arch = e_machine_mapping[e_machine]

  # Now read the full header in either 64bit or 32bit mode
  dynamic = IsDynamicElf(path, arch == 'x86-64')
  return arch, dynamic


def IsDynamicElf(path, is64bit):
  """Examine an elf file to determine if it is dynamically
  linked or not.
  This is determined by searching the program headers for
  a header of type PT_INTERP.
  """
  if is64bit:
    elf_header_format = '16s2HI3QI3H'
  else:
    elf_header_format = '16s2HI3II3H'

  elf_header_size = struct.calcsize(elf_header_format)

  with open(path, 'rb') as f:
    header = f.read(elf_header_size)
    header = struct.unpack(elf_header_format, header)
    p_header_offset = header[5]
    p_header_entry_size = header[9]
    num_p_header = header[10]
    f.seek(p_header_offset)
    p_headers = f.read(p_header_entry_size*num_p_header)

  # Read the first word of each Phdr to find out its type.
  #
  # typedef struct
  # {
  #   Elf32_Word  p_type;     /* Segment type */
  #   ...
  # } Elf32_Phdr;
  elf_phdr_format = 'I'
  PT_INTERP = 3

  while p_headers:
    p_header = p_headers[:p_header_entry_size]
    p_headers = p_headers[p_header_entry_size:]
    phdr_type = struct.unpack(elf_phdr_format, p_header[:4])[0]
    if phdr_type == PT_INTERP:
      return True

  return False


class ArchFile(object):
  '''Simple structure containing information about

  Attributes:
    name: Name of this file
    path: Full path to this file on the build system
    arch: Architecture of this file (e.g., x86-32)
    url: Relative path to file in the staged web directory.
        Used for specifying the "url" attribute in the nmf file.'''

  def __init__(self, name, path, url, arch=None):
    self.name = name
    self.path = path
    self.url = url
    self.arch = arch
    if not arch:
      self.arch = ParseElfHeader(path)[0]

  def __repr__(self):
    return '<ArchFile %s>' % self.path

  def __str__(self):
    '''Return the file path when invoked with the str() function'''
    return self.path


class NmfUtils(object):
  '''Helper class for creating and managing nmf files

  Attributes:
    manifest: A JSON-structured dict containing the nmf structure
    needed: A dict with key=filename and value=ArchFile (see GetNeeded)
  '''

  def __init__(self, main_files=None, objdump=None,
               lib_path=None, extra_files=None, lib_prefix=None,
               remap=None, pnacl_optlevel=None):
    '''Constructor

    Args:
      main_files: List of main entry program files.  These will be named
          files->main.nexe for dynamic nexes, and program for static nexes
      objdump: path to x86_64-nacl-objdump tool (or Linux equivalent)
      lib_path: List of paths to library directories
      extra_files: List of extra files to include in the nmf
      lib_prefix: A list of path components to prepend to the library paths,
          both for staging the libraries and for inclusion into the nmf file.
          Examples:  ['..'], ['lib_dir']
      remap: Remaps the library name in the manifest.
      pnacl_optlevel: Optimization level for PNaCl translation.
      '''
    self.objdump = objdump
    self.main_files = main_files or []
    self.extra_files = extra_files or []
    self.lib_path = lib_path or []
    self.manifest = None
    self.needed = {}
    self.lib_prefix = lib_prefix or []
    self.remap = remap or {}
    self.pnacl = main_files and main_files[0].endswith('pexe')
    self.pnacl_optlevel = pnacl_optlevel

    for filename in self.main_files:
      if not os.path.exists(filename):
        raise Error('Input file not found: %s' % filename)
      if not os.path.isfile(filename):
        raise Error('Input is not a file: %s' % filename)

  def GleanFromObjdump(self, files, arch):
    '''Get architecture and dependency information for given files

    Args:
      files: A list of files to examine.
          [ '/path/to/my.nexe',
            '/path/to/lib64/libmy.so',
            '/path/to/mydata.so',
            '/path/to/my.data' ]
      arch: The architecure we are looking for, or None to accept any
            architecture.

    Returns: A tuple with the following members:
      input_info: A dict with key=filename and value=ArchFile of input files.
          Includes the input files as well, with arch filled in if absent.
          Example: { '/path/to/my.nexe': ArchFile(my.nexe),
                     '/path/to/libfoo.so': ArchFile(libfoo.so) }
      needed: A set of strings formatted as "arch/name".  Example:
          set(['x86-32/libc.so', 'x86-64/libgcc.so'])
    '''
    if not self.objdump:
      self.objdump = FindObjdumpExecutable()
      if not self.objdump:
        raise Error('No objdump executable found (see --help for more info)')

    full_paths = set()
    for filename in files:
      if os.path.exists(filename):
        full_paths.add(filename)
      else:
        for path in self.FindLibsInPath(filename):
          full_paths.add(path)

    cmd = [self.objdump, '-p'] + list(full_paths)
    DebugPrint('GleanFromObjdump[%s](%s)' % (arch, cmd))
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, bufsize=-1)

    input_info = {}
    found_basenames = set()
    needed = set()
    output, err_output = proc.communicate()
    if proc.returncode:
      raise Error('%s\nStdError=%s\nobjdump failed with error code: %d' %
                  (output, err_output, proc.returncode))

    for line in output.splitlines(True):
      # Objdump should display the architecture first and then the dependencies
      # second for each file in the list.
      matched = FormatMatcher.match(line)
      if matched:
        filename = matched.group(1)
        file_arch = OBJDUMP_ARCH_MAP[matched.group(2)]
        if arch and file_arch != arch:
          continue
        name = os.path.basename(filename)
        found_basenames.add(name)
        input_info[filename] = ArchFile(
            arch=file_arch,
            name=name,
            path=filename,
            url='/'.join(self.lib_prefix + [ARCH_LOCATION[file_arch], name]))
      matched = NeededMatcher.match(line)
      if matched:
        match = '/'.join([file_arch, matched.group(1)])
        needed.add(match)
        Trace("NEEDED: %s" % match)

    for filename in files:
      if os.path.basename(filename) not in found_basenames:
        raise Error('Library not found [%s]: %s' % (arch, filename))

    return input_info, needed

  def FindLibsInPath(self, name):
    '''Finds the set of libraries matching |name| within lib_path

    Args:
      name: name of library to find

    Returns:
      A list of system paths that match the given name within the lib_path'''
    files = []
    for dirname in self.lib_path:
      filename = os.path.join(dirname, name)
      if os.path.exists(filename):
        files.append(filename)
    if not files:
      raise Error('cannot find library %s' % name)
    return files

  def GetNeeded(self):
    '''Collect the list of dependencies for the main_files

    Returns:
      A dict with key=filename and value=ArchFile of input files.
          Includes the input files as well, with arch filled in if absent.
          Example: { '/path/to/my.nexe': ArchFile(my.nexe),
                     '/path/to/libfoo.so': ArchFile(libfoo.so) }'''

    if self.needed:
      return self.needed

    DebugPrint('GetNeeded(%s)' % self.main_files)

    dynamic = any(ParseElfHeader(f)[1] for f in self.main_files)

    if dynamic:
      examined = set()
      all_files, unexamined = self.GleanFromObjdump(self.main_files, None)
      for arch_file in all_files.itervalues():
        arch_file.url = arch_file.path
        if unexamined:
          unexamined.add('/'.join([arch_file.arch, RUNNABLE_LD]))

      while unexamined:
        files_to_examine = {}

        # Take all the currently unexamined files and group them
        # by architecture.
        for arch_name in unexamined:
          arch, name = arch_name.split('/')
          files_to_examine.setdefault(arch, []).append(name)

        # Call GleanFromObjdump() for each architecture.
        needed = set()
        for arch, files in files_to_examine.iteritems():
          new_files, new_needed = self.GleanFromObjdump(files, arch)
          all_files.update(new_files)
          needed |= new_needed

        examined |= unexamined
        unexamined = needed - examined

      # With the runnable-ld.so scheme we have today, the proper name of
      # the dynamic linker should be excluded from the list of files.
      ldso = [LD_NACL_MAP[arch] for arch in set(OBJDUMP_ARCH_MAP.values())]
      for name, arch_file in all_files.items():
        if arch_file.name in ldso:
          del all_files[name]

      self.needed = all_files
    else:
      for filename in self.main_files:
        url = os.path.split(filename)[1]
        archfile = ArchFile(name=os.path.basename(filename),
                            path=filename, url=url)
        self.needed[filename] = archfile

    return self.needed

  def StageDependencies(self, destination_dir):
    '''Copies over the dependencies into a given destination directory

    Each library will be put into a subdirectory that corresponds to the arch.

    Args:
      destination_dir: The destination directory for staging the dependencies
    '''
    nexe_root = os.path.dirname(os.path.abspath(self.main_files[0]))
    nexe_root = os.path.normcase(nexe_root)

    needed = self.GetNeeded()
    for arch_file in needed.itervalues():
      urldest = arch_file.url
      source = arch_file.path

      # for .nexe and .so files specified on the command line stage
      # them in paths relative to the .nexe (with the .nexe always
      # being staged at the root).
      if source in self.main_files:
        absdest = os.path.normcase(os.path.abspath(urldest))
        if absdest.startswith(nexe_root):
          urldest = os.path.relpath(urldest, nexe_root)

      destination = os.path.join(destination_dir, urldest)

      if (os.path.normcase(os.path.abspath(source)) ==
          os.path.normcase(os.path.abspath(destination))):
        continue

      # make sure target dir exists
      MakeDir(os.path.dirname(destination))

      Trace('copy: %s -> %s' % (source, destination))
      shutil.copy2(source, destination)

  def _GeneratePNaClManifest(self):
    manifest = {}
    manifest[PROGRAM_KEY] = {}
    manifest[PROGRAM_KEY][PORTABLE_KEY] = {}
    translate_dict =  {
      "url": os.path.basename(self.main_files[0]),
    }
    if self.pnacl_optlevel is not None:
      translate_dict[PNACL_OPTLEVEL_KEY] = self.pnacl_optlevel
    manifest[PROGRAM_KEY][PORTABLE_KEY][TRANSLATE_KEY] = translate_dict
    self.manifest = manifest

  def _GenerateManifest(self):
    '''Create a JSON formatted dict containing the files

    NaCl will map url requests based on architecture.  The startup NEXE
    can always be found under the top key PROGRAM.  Additional files are under
    the FILES key further mapped by file name.  In the case of 'runnable' the
    PROGRAM key is populated with urls pointing the runnable-ld.so which acts
    as the startup nexe.  The application itself is then placed under the
    FILES key mapped as 'main.exe' instead of the original name so that the
    loader can find it. '''
    manifest = { FILES_KEY: {}, PROGRAM_KEY: {} }

    needed = self.GetNeeded()

    runnable = any(n.endswith(RUNNABLE_LD) for n in needed)

    extra_files_kv = [(key, ArchFile(name=key,
                                     arch=arch,
                                     path=url,
                                     url=url))
                      for key, arch, url in self.extra_files]

    nexe_root = os.path.dirname(os.path.abspath(self.main_files[0]))

    for need, archinfo in needed.items() + extra_files_kv:
      urlinfo = { URL_KEY: archinfo.url }
      name = archinfo.name

      # If starting with runnable-ld.so, make that the main executable.
      if runnable:
        if need.endswith(RUNNABLE_LD):
          manifest[PROGRAM_KEY][archinfo.arch] = urlinfo
          continue

      if need in self.main_files:
        # Ensure that the .nexe and .so names are relative to the root
        # of where the .nexe lives.
        if os.path.abspath(urlinfo[URL_KEY]).startswith(nexe_root):
          urlinfo[URL_KEY] = os.path.relpath(urlinfo[URL_KEY], nexe_root)

        if need.endswith(".nexe"):
          # Place it under program if we aren't using the runnable-ld.so.
          if not runnable:
            manifest[PROGRAM_KEY][archinfo.arch] = urlinfo
            continue
          # Otherwise, treat it like another another file named main.nexe.
          name = MAIN_NEXE

      name = self.remap.get(name, name)
      fileinfo = manifest[FILES_KEY].get(name, {})
      fileinfo[archinfo.arch] = urlinfo
      manifest[FILES_KEY][name] = fileinfo
    self.manifest = manifest

  def GetManifest(self):
    '''Returns a JSON-formatted dict containing the NaCl dependencies'''
    if not self.manifest:
      if self.pnacl:
        self._GeneratePNaClManifest()
      else:
        self._GenerateManifest()
    return self.manifest

  def GetJson(self):
    '''Returns the Manifest as a JSON-formatted string'''
    pretty_string = json.dumps(self.GetManifest(), indent=2)
    # json.dumps sometimes returns trailing whitespace and does not put
    # a newline at the end.  This code fixes these problems.
    pretty_lines = pretty_string.split('\n')
    return '\n'.join([line.rstrip() for line in pretty_lines]) + '\n'


def Trace(msg):
  if Trace.verbose:
    sys.stderr.write(str(msg) + '\n')

Trace.verbose = False


def ParseExtraFiles(encoded_list, err):
  """Parse the extra-files list and return a canonicalized list of
  [key, arch, url] triples.  The |encoded_list| should be a list of
  strings of the form 'key:url' or 'key:arch:url', where an omitted
  'arch' is taken to mean 'portable'.

  All entries in |encoded_list| are checked for syntax errors before
  returning.  Error messages are written to |err| (typically
  sys.stderr) so that the user has actionable feedback for fixing all
  errors, rather than one at a time.  If there are any errors, None is
  returned instead of a list, since an empty list is a valid return
  value.
  """
  seen_error = False
  canonicalized = []
  for ix in range(len(encoded_list)):
    kv = encoded_list[ix]
    unquoted = quote.unquote(kv, ':')
    if len(unquoted) == 3:
      if unquoted[1] != ':':
        err.write('Syntax error for key:value tuple ' +
                  'for --extra-files argument: ' + kv + '\n')
        seen_error = True
      else:
        canonicalized.append([unquoted[0], 'portable', unquoted[2]])
    elif len(unquoted) == 5:
      if unquoted[1] != ':' or unquoted[3] != ':':
        err.write('Syntax error for key:arch:url tuple ' +
                  'for --extra-files argument: ' +
                  kv + '\n')
        seen_error = True
      else:
        canonicalized.append([unquoted[0], unquoted[2], unquoted[4]])
    else:
      err.write('Bad key:arch:url tuple for --extra-files: ' + kv + '\n')
  if seen_error:
    return None
  return canonicalized


def GetSDKRoot():
  """Determine current NACL_SDK_ROOT, either via the environment variable
  itself, or by attempting to derive it from the location of this script.
  """
  sdk_root = os.environ.get('NACL_SDK_ROOT')
  if not sdk_root:
    sdk_root = os.path.dirname(SCRIPT_DIR)
    if not os.path.exists(os.path.join(sdk_root, 'toolchain')):
      return None

  return sdk_root


def FindObjdumpExecutable():
  """Derive path to objdump executable to use for determining shared
  object dependencies.
  """
  sdk_root = GetSDKRoot()
  if not sdk_root:
    return None

  osname = getos.GetPlatform()
  toolchain = os.path.join(sdk_root, 'toolchain', '%s_x86_glibc' % osname)
  objdump = os.path.join(toolchain, 'bin', 'x86_64-nacl-objdump')
  if osname == 'win':
    objdump += '.exe'

  if not os.path.exists(objdump):
    sys.stderr.write('WARNING: failed to find objdump in default '
                     'location: %s' % objdump)
    return None

  return objdump


def GetDefaultLibPath(config):
  """Derive default library path to use when searching for shared
  objects.  This currently include the toolchain library folders
  as well as the top level SDK lib folder and the naclports lib
  folder.  We include both 32-bit and 64-bit library paths.
  """
  assert(config in ('Debug', 'Release'))
  sdk_root = GetSDKRoot()
  if not sdk_root:
    # TOOD(sbc): output a warning here?  We would also need to suppress
    # the warning when run from the chromium build.
    return []

  osname = getos.GetPlatform()
  libpath = [
    # Core toolchain libraries
    'toolchain/%s_x86_glibc/x86_64-nacl/lib' % osname,
    'toolchain/%s_x86_glibc/x86_64-nacl/lib32' % osname,
    # naclports installed libraries
    'toolchain/%s_x86_glibc/x86_64-nacl/usr/lib' % osname,
    'toolchain/%s_x86_glibc/i686-nacl/usr/lib' % osname,
    # SDK bundle libraries
    'lib/glibc_x86_32/%s' % config,
    'lib/glibc_x86_64/%s' % config,
    # naclports bundle libraries
    'ports/lib/glibc_x86_32/%s' % config,
    'ports/lib/glibc_x86_64/%s' % config,
  ]
  libpath = [os.path.normpath(p) for p in libpath]
  libpath = [os.path.join(sdk_root, p) for p in libpath]
  return libpath


def main(argv):
  parser = optparse.OptionParser(
      usage='Usage: %prog [options] nexe [extra_libs...]')
  parser.add_option('-o', '--output', dest='output',
                    help='Write manifest file to FILE (default is stdout)',
                    metavar='FILE')
  parser.add_option('-D', '--objdump', dest='objdump',
                    help='Override the default "objdump" tool used to find '
                         'shared object dependencies',
                    metavar='TOOL')
  parser.add_option('--no-default-libpath', action='store_true',
                    help="Don't include the SDK default library paths")
  parser.add_option('--debug-libs', action='store_true',
                    help='Use debug library paths when constructing default '
                         'library path.')
  parser.add_option('-L', '--library-path', dest='lib_path',
                    action='append', default=[],
                    help='Add DIRECTORY to library search path',
                    metavar='DIRECTORY')
  parser.add_option('-P', '--path-prefix', dest='path_prefix', default='',
                    help='A path to prepend to shared libraries in the .nmf',
                    metavar='DIRECTORY')
  parser.add_option('-s', '--stage-dependencies', dest='stage_dependencies',
                    help='Destination directory for staging libraries',
                    metavar='DIRECTORY')
  parser.add_option('-t', '--toolchain', help='Legacy option, do not use')
  parser.add_option('-n', '--name', dest='name',
                    help='Rename FOO as BAR',
                    action='append', default=[], metavar='FOO,BAR')
  parser.add_option('-x', '--extra-files',
                    help=('Add extra key:file tuple to the "files"' +
                          ' section of the .nmf'),
                    action='append', default=[], metavar='FILE')
  parser.add_option('-O', '--pnacl-optlevel',
                    help='Set the optimization level to N in PNaCl manifests',
                    metavar='N')
  parser.add_option('-v', '--verbose',
                    help='Verbose output', action='store_true')
  parser.add_option('-d', '--debug-mode',
                    help='Debug mode', action='store_true')
  options, args = parser.parse_args(argv)
  if options.verbose:
    Trace.verbose = True
  if options.debug_mode:
    DebugPrint.debug_mode = True

  if options.toolchain is not None:
    sys.stderr.write('warning: option -t/--toolchain is deprecated.\n')

  if len(args) < 1:
    parser.error('No nexe files specified.  See --help for more info')

  canonicalized = ParseExtraFiles(options.extra_files, sys.stderr)
  if canonicalized is None:
    parser.error('Bad --extra-files (-x) argument syntax')

  remap = {}
  for ren in options.name:
    parts = ren.split(',')
    if len(parts) != 2:
      parser.error('Expecting --name=<orig_arch.so>,<new_name.so>')
    remap[parts[0]] = parts[1]

  if options.path_prefix:
    path_prefix = options.path_prefix.split('/')
  else:
    path_prefix = []

  for libpath in options.lib_path:
    if not os.path.exists(libpath):
      sys.stderr.write('Specified library path does not exist: %s\n' % libpath)
    elif not os.path.isdir(libpath):
      sys.stderr.write('Specified library is not a directory: %s\n' % libpath)

  if not options.no_default_libpath:
    # Add default libraries paths to the end of the search path.
    config = options.debug_libs and 'Debug' or 'Release'
    options.lib_path += GetDefaultLibPath(config)

  pnacl_optlevel = None
  if options.pnacl_optlevel is not None:
    pnacl_optlevel = int(options.pnacl_optlevel)
    if pnacl_optlevel < 0 or pnacl_optlevel > 3:
      sys.stderr.write(
          'warning: PNaCl optlevel %d is unsupported (< 0 or > 3)\n' %
          pnacl_optlevel)

  nmf = NmfUtils(objdump=options.objdump,
                 main_files=args,
                 lib_path=options.lib_path,
                 extra_files=canonicalized,
                 lib_prefix=path_prefix,
                 remap=remap,
                 pnacl_optlevel=pnacl_optlevel)

  nmf.GetManifest()
  if not options.output:
    sys.stdout.write(nmf.GetJson())
  else:
    with open(options.output, 'w') as output:
      output.write(nmf.GetJson())

  if options.stage_dependencies and not nmf.pnacl:
    Trace('Staging dependencies...')
    nmf.StageDependencies(options.stage_dependencies)

  return 0


if __name__ == '__main__':
  try:
    rtn = main(sys.argv[1:])
  except Error, e:
    sys.stderr.write('%s: %s\n' % (os.path.basename(__file__), e))
    rtn = 1
  except KeyboardInterrupt:
    sys.stderr.write('%s: interrupted\n' % os.path.basename(__file__))
    rtn = 1
  sys.exit(rtn)
