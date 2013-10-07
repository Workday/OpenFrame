#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Script to create Chrome Installer archive.

  This script is used to create an archive of all the files required for a
  Chrome install in appropriate directory structure. It reads chrome.release
  file as input, creates chrome.7z archive, compresses setup.exe and
  generates packed_files.txt for mini_installer project.

"""

import ConfigParser
import glob
import optparse
import os
import shutil
import subprocess
import sys


ARCHIVE_DIR = "installer_archive"

# suffix to uncompresed full archive file, appended to options.output_name
ARCHIVE_SUFFIX = ".7z"
BSDIFF_EXEC = "bsdiff.exe"
CHROME_DIR = "Chrome-bin"
CHROME_PATCH_FILE_SUFFIX = "_patch"  # prefixed by options.output_name

# compressed full archive suffix, will be prefixed by options.output_name
COMPRESSED_ARCHIVE_SUFFIX = ".packed.7z"

COMPRESSED_FILE_EXT = ".packed.7z"     # extension of patch archive file
COURGETTE_EXEC = "courgette.exe"
MINI_INSTALLER_INPUT_FILE = "packed_files.txt"
PATCH_FILE_EXT = '.diff'
SETUP_EXEC = "setup.exe"
SETUP_PATCH_FILE_PREFIX = "setup_patch"
TEMP_ARCHIVE_DIR = "temp_installer_archive"
VERSION_FILE = "VERSION"


def BuildVersion(build_dir):
  """Returns the full build version string constructed from information in
  VERSION_FILE.  Any segment not found in that file will default to '0'.
  """
  major = 0
  minor = 0
  build = 0
  patch = 0
  for line in open(os.path.join(build_dir, '../../chrome', VERSION_FILE), 'r'):
    line = line.rstrip()
    if line.startswith('MAJOR='):
      major = line[6:]
    elif line.startswith('MINOR='):
      minor = line[6:]
    elif line.startswith('BUILD='):
      build = line[6:]
    elif line.startswith('PATCH='):
      patch = line[6:]
  return '%s.%s.%s.%s' % (major, minor, build, patch)


def CompressUsingLZMA(build_dir, compressed_file, input_file):
  lzma_exec = GetLZMAExec(build_dir)
  cmd = [lzma_exec,
         'a', '-t7z',
          # Flags equivalent to -mx9 (ultra) but with the bcj2 turned on (exe
          # pre-filter). This results in a ~2.3MB decrease in installer size on
          # a 24MB installer.
          # Additionally, these settings reflect a 7zip 4.42 and up change in
          # the definition of -mx9, increasting the dicionary size moving to
          # 26bit = 64MB. This results in an additional ~3.5MB decrease.
          # Older 7zip versions can support these settings, as these changes
          # rely on existing functionality in the lzma format.
          '-m0=BCJ2',
          '-m1=LZMA:d27:fb128',
          '-m2=LZMA:d22:fb128:mf=bt2',
          '-m3=LZMA:d22:fb128:mf=bt2',
          '-mb0:1',
          '-mb0s1:2',
          '-mb0s2:3',
          compressed_file,
          input_file,]
  if os.path.exists(compressed_file):
    os.remove(compressed_file)
  RunSystemCommand(cmd)


def CopyAllFilesToStagingDir(config, distribution, staging_dir, build_dir,
                             enable_hidpi, enable_touch_ui):
  """Copies the files required for installer archive.
  Copies all common files required for various distributions of Chromium and
  also files for the specific Chromium build specified by distribution.
  """
  CopySectionFilesToStagingDir(config, 'GENERAL', staging_dir, build_dir)
  if distribution:
    if len(distribution) > 1 and distribution[0] == '_':
      distribution = distribution[1:]
    CopySectionFilesToStagingDir(config, distribution.upper(),
                                 staging_dir, build_dir)
  if enable_hidpi == '1':
    CopySectionFilesToStagingDir(config, 'HIDPI', staging_dir, build_dir)
  if enable_touch_ui == '1':
    CopySectionFilesToStagingDir(config, 'TOUCH', staging_dir, build_dir)


def CopySectionFilesToStagingDir(config, section, staging_dir, src_dir):
  """Copies installer archive files specified in section from src_dir to
  staging_dir. This method reads section from config and copies all the
  files specified from src_dir to staging dir.
  """
  for option in config.options(section):
    if option.endswith('dir'):
      continue

    dst_dir = os.path.join(staging_dir, config.get(section, option))
    src_paths = glob.glob(os.path.join(src_dir, option))
    if src_paths and not os.path.exists(dst_dir):
      os.makedirs(dst_dir)
    for src_path in src_paths:
      dst_path = os.path.join(dst_dir, os.path.basename(src_path))
      if not os.path.exists(dst_path):
        shutil.copy(src_path, dst_dir)

def GenerateDiffPatch(options, orig_file, new_file, patch_file):
  if (options.diff_algorithm == "COURGETTE"):
    exe_file = os.path.join(options.last_chrome_installer, COURGETTE_EXEC)
    cmd = '%s -gen "%s" "%s" "%s"' % (exe_file, orig_file, new_file, patch_file)
  else:
    exe_file = os.path.join(options.build_dir, BSDIFF_EXEC)
    cmd = [exe_file, orig_file, new_file, patch_file,]
  RunSystemCommand(cmd)

def GetLZMAExec(build_dir):
  lzma_exec = os.path.join(build_dir, "..", "..", "third_party",
                           "lzma_sdk", "Executable", "7za.exe")
  return lzma_exec

def GetPrevVersion(build_dir, temp_dir, last_chrome_installer, output_name):
  if not last_chrome_installer:
    return ''

  lzma_exec = GetLZMAExec(build_dir)
  prev_archive_file = os.path.join(last_chrome_installer,
                                   output_name + ARCHIVE_SUFFIX)
  cmd = [lzma_exec,
         'x',
         '-o"%s"' % temp_dir,
         prev_archive_file,
         'Chrome-bin/*/chrome.dll',]
  RunSystemCommand(cmd)
  dll_path = glob.glob(os.path.join(temp_dir, 'Chrome-bin', '*', 'chrome.dll'))
  return os.path.split(os.path.split(dll_path[0])[0])[1]

def MakeStagingDirectories(staging_dir):
  """Creates a staging path for installer archive. If directory exists already,
  deletes the existing directory.
  """
  file_path = os.path.join(staging_dir, TEMP_ARCHIVE_DIR)
  if os.path.exists(file_path):
    shutil.rmtree(file_path)
  os.makedirs(file_path)

  temp_file_path = os.path.join(staging_dir, TEMP_ARCHIVE_DIR)
  if os.path.exists(temp_file_path):
    shutil.rmtree(temp_file_path)
  os.makedirs(temp_file_path)
  return (file_path, temp_file_path)

def Readconfig(input_file, current_version):
  """Reads config information from input file after setting default value of
  global variabes.
  """
  variables = {}
  variables['ChromeDir'] = CHROME_DIR
  variables['VersionDir'] = os.path.join(variables['ChromeDir'],
                                          current_version)
  config = ConfigParser.SafeConfigParser(variables)
  config.read(input_file)
  return config

def RunSystemCommand(cmd, **kw):
  print 'Running', cmd
  exit_code = subprocess.call(cmd, **kw)
  if (exit_code != 0):
    raise Exception("Error while running cmd: %s, exit_code: %s" %
                    (cmd, exit_code))

def CreateArchiveFile(options, staging_dir, current_version, prev_version):
  """Creates a new installer archive file after deleting any existing old file.
  """
  # First create an uncompressed archive file for the current build (chrome.7z)
  lzma_exec = GetLZMAExec(options.build_dir)
  archive_file = os.path.join(options.output_dir,
                              options.output_name + ARCHIVE_SUFFIX)
  cmd = [lzma_exec,
         'a',
         '-t7z',
         archive_file,
         os.path.join(staging_dir, CHROME_DIR),
         '-mx0',]
  # There doesnt seem to be any way in 7za.exe to override existing file so
  # we always delete before creating a new one.
  if not os.path.exists(archive_file):
    RunSystemCommand(cmd)
  elif options.skip_rebuild_archive != "true":
    os.remove(archive_file)
    RunSystemCommand(cmd)

  # Do not compress the archive in developer (component) builds.
  if options.component_build == '1':
    compressed_file = os.path.join(
        options.output_dir, options.output_name + COMPRESSED_ARCHIVE_SUFFIX)
    if os.path.exists(compressed_file):
      os.remove(compressed_file)
    return os.path.basename(archive_file)

  # If we are generating a patch, run bsdiff against previous build and
  # compress the resulting patch file. If this is not a patch just compress the
  # uncompressed archive file.
  patch_name_prefix = options.output_name + CHROME_PATCH_FILE_SUFFIX
  if options.last_chrome_installer:
    prev_archive_file = os.path.join(options.last_chrome_installer,
                                     options.output_name + ARCHIVE_SUFFIX)
    patch_file = os.path.join(options.build_dir, patch_name_prefix +
                                                  PATCH_FILE_EXT)
    GenerateDiffPatch(options, prev_archive_file, archive_file, patch_file)
    compressed_archive_file = patch_name_prefix + '_' + \
                              current_version + '_from_' + prev_version + \
                              COMPRESSED_FILE_EXT
    orig_file = patch_file
  else:
    compressed_archive_file = options.output_name + COMPRESSED_ARCHIVE_SUFFIX
    orig_file = archive_file

  compressed_archive_file_path = os.path.join(options.output_dir,
                                              compressed_archive_file)
  CompressUsingLZMA(options.build_dir, compressed_archive_file_path, orig_file)

  return compressed_archive_file


def PrepareSetupExec(options, current_version, prev_version):
  """Prepares setup.exe for bundling in mini_installer based on options."""
  if options.setup_exe_format == "FULL":
    setup_file = SETUP_EXEC
  elif options.setup_exe_format == "DIFF":
    if not options.last_chrome_installer:
      raise Exception(
          "To use DIFF for setup.exe, --last_chrome_installer is needed.")
    prev_setup_file = os.path.join(options.last_chrome_installer, SETUP_EXEC)
    new_setup_file = os.path.join(options.build_dir, SETUP_EXEC)
    patch_file = os.path.join(options.build_dir, SETUP_PATCH_FILE_PREFIX +
                                                  PATCH_FILE_EXT)
    GenerateDiffPatch(options, prev_setup_file, new_setup_file, patch_file)
    setup_file = SETUP_PATCH_FILE_PREFIX + '_' + current_version + \
                 '_from_' + prev_version + COMPRESSED_FILE_EXT
    setup_file_path = os.path.join(options.build_dir, setup_file)
    CompressUsingLZMA(options.build_dir, setup_file_path, patch_file)
  else:
    cmd = ['makecab.exe',
           '/D', 'CompressionType=LZX',
           '/V1',
           '/L', options.output_dir,
           os.path.join(options.build_dir, SETUP_EXEC),]
    # Send useless makecab progress on stdout to the bitbucket.
    RunSystemCommand(cmd, stdout=open(os.devnull, "w"))
    setup_file = SETUP_EXEC[:-1] + "_"
  return setup_file


_RESOURCE_FILE_TEMPLATE = """\
// This file is automatically generated by create_installer_archive.py.
// It contains the resource entries that are going to be linked inside
// mini_installer.exe. For each file to be linked there should be two
// lines:
// - The first line contains the output filename (without path) and the
// type of the resource ('BN' - not compressed , 'BL' - LZ compressed,
// 'B7' - LZMA compressed)
// - The second line contains the path to the input file. Uses '/' to
// separate path components.

%(setup_file)s  %(setup_file_resource_type)s
    "%(setup_file_path)s"

%(archive_file)s  B7
    "%(archive_file_path)s"
"""


def CreateResourceInputFile(
    output_dir, setup_format, archive_file, setup_file, resource_file_path):
  """Creates resource input file (packed_files.txt) for mini_installer project.

  This method checks the format of setup.exe being used and according sets
  its resource type.
  """
  setup_resource_type = "BL"
  if (setup_format == "FULL"):
    setup_resource_type = "BN"
  elif (setup_format == "DIFF"):
    setup_resource_type = "B7"

  # Expand the resource file template.
  args = {
      'setup_file': setup_file,
      'setup_file_resource_type': setup_resource_type,
      'setup_file_path':
          os.path.join(output_dir, setup_file).replace("\\","/"),
      'archive_file': archive_file,
      'archive_file_path':
          os.path.join(output_dir, archive_file).replace("\\","/"),
      }
  resource_file = _RESOURCE_FILE_TEMPLATE % args

  with open(resource_file_path, 'w') as f:
    f.write(resource_file)


# Reads |manifest_name| from |build_dir| and writes |manifest_name| to
# |output_dir| with the same content plus |inserted_string| added just before
# |insert_before|.
def CopyAndAugmentManifest(build_dir, output_dir, manifest_name,
                           inserted_string, insert_before):
  manifest_file = open(os.path.join(build_dir, manifest_name), 'r')
  manifest_lines = manifest_file.readlines()
  manifest_file.close()

  insert_line = -1
  insert_pos = -1
  for i in xrange(len(manifest_lines)):
    insert_pos = manifest_lines[i].find(insert_before)
    if insert_pos != -1:
      insert_line = i
      break
  if insert_line == -1:
    raise ValueError('Could not find {0} in the manifest:\n{1}'.format(
        insert_before, ''.join(manifest_lines)))
  old = manifest_lines[insert_line]
  manifest_lines[insert_line] = (old[:insert_pos] + inserted_string +
                                 old[insert_pos:])

  modified_manifest_file = open(
      os.path.join(output_dir, manifest_name), 'w')
  modified_manifest_file.write(''.join(manifest_lines))
  modified_manifest_file.close()


# Copy the relevant CRT DLLs to |build_dir|. We copy DLLs from all versions
# of VS installed to make sure we have the correct CRT version, unused DLLs
# should not conflict with the others anyways.
def CopyVisualStudioRuntimeDLLs(build_dir, target_arch):
  is_debug = os.path.basename(build_dir).startswith('Debug')
  if not is_debug and not os.path.basename(build_dir).startswith('Release'):
    print ("Warning: could not determine build configuration from "
           "output directory, assuming Release build.")

  crt_dlls = []
  sys_dll_dir = None
  if is_debug:
    crt_dlls = glob.glob(
        "C:/Program Files (x86)/Microsoft Visual Studio */VC/redist/"
        "Debug_NonRedist/" + target_arch + "/Microsoft.*.DebugCRT/*.dll")
  else:
    crt_dlls = glob.glob(
        "C:/Program Files (x86)/Microsoft Visual Studio */VC/redist/" +
        target_arch + "/Microsoft.*.CRT/*.dll")

  # Also handle the case where someone is building using only winsdk and
  # doesn't have Visual Studio installed.
  if not crt_dlls:
    if target_arch == 'x64':
      # check we are are on a 64bit system by existence of WOW64 dir
      if os.access("C:/Windows/SysWOW64", os.F_OK):
        sys_dll_dir = "C:/Windows/System32"
      else:
        # only support packaging of 64bit installer on 64bit system
        # but this just as bad as not finding DLLs at all so we
        # don't abort here to mirror behavior below
        print ("Warning: could not find x64 CRT DLLs on x86 system.")
    else:
      # On a 64-bit system, 32-bit dlls are in SysWOW64 (don't ask).
      if os.access("C:/Windows/SysWOW64", os.F_OK):
        sys_dll_dir = "C:/Windows/SysWOW64"
      else:
        sys_dll_dir = "C:/Windows/System32"

    if sys_dll_dir is not None:
      if is_debug:
        crt_dlls = glob.glob(os.path.join(sys_dll_dir, "msvc*0d.dll"))
      else:
        crt_dlls = glob.glob(os.path.join(sys_dll_dir, "msvc*0.dll"))

  if not crt_dlls:
    print ("Warning: could not find CRT DLLs to copy to build dir - target "
           "may not run on a system that doesn't have those DLLs.")

  for dll in crt_dlls:
    shutil.copy(dll, build_dir)


# Copies component build DLLs and generates required config files and manifests
# in order for chrome.exe and setup.exe to be able to find those DLLs at
# run-time.
# This is meant for developer builds only and should never be used to package
# an official build.
def DoComponentBuildTasks(staging_dir, build_dir, target_arch, current_version):
  # Get the required directories for the upcoming operations.
  chrome_dir = os.path.join(staging_dir, CHROME_DIR)
  version_dir = os.path.join(chrome_dir, current_version)
  installer_dir = os.path.join(version_dir, 'Installer')
  # |installer_dir| is technically only created post-install, but we need it
  # now to add setup.exe's config and manifest to the archive.
  if not os.path.exists(installer_dir):
    os.mkdir(installer_dir)

  # Copy the VS CRT DLLs to |build_dir|. This must be done before the general
  # copy step below to ensure the CRT DLLs are added to the archive and marked
  # as a dependency in the exe manifests generated below.
  CopyVisualStudioRuntimeDLLs(build_dir, target_arch)

  # Copy all the DLLs in |build_dir| to the version directory. Simultaneously
  # build a list of their names to mark them as dependencies of chrome.exe and
  # setup.exe later.
  dlls = glob.glob(os.path.join(build_dir, '*.dll'))
  dll_names = []
  for dll in dlls:
    # remoting_*.dll's don't belong in the archive (it doesn't depend on them
    # in gyp). Trying to copy them causes a build race when creating the
    # installer archive in component mode. See: crbug.com/180996
    if os.path.basename(dll).startswith('remoting_'):
      continue
    shutil.copy(dll, version_dir)
    dll_names.append(os.path.splitext(os.path.basename(dll))[0])

  exe_config = (
      "<configuration>\n"
      "  <windows>\n"
      "    <assemblyBinding xmlns='urn:schemas-microsoft-com:asm.v1'>\n"
      "        <probing privatePath='{rel_path}'/>\n"
      "    </assemblyBinding>\n"
      "  </windows>\n"
      "</configuration>")

  # Write chrome.exe.config to point to the version directory.
  chrome_exe_config_file = open(
      os.path.join(chrome_dir, 'chrome.exe.config'), 'w')
  chrome_exe_config_file.write(exe_config.format(rel_path=current_version))
  chrome_exe_config_file.close()

  # Write setup.exe.config to point to the version directory (which is one
  # level up from setup.exe post-install).
  setup_exe_config_file = open(
      os.path.join(installer_dir, 'setup.exe.config'), 'w')
  setup_exe_config_file.write(exe_config.format(rel_path='..'))
  setup_exe_config_file.close()

  # Add a dependency for each DLL in |dlls| to the existing manifests for
  # chrome.exe and setup.exe. Some of these DLLs are not actually used by
  # either process, but listing them all as dependencies doesn't hurt as it
  # only makes them visible to the exes, just like they already are in the
  # build output directory.
  exe_manifest_dependencies_list = []
  for name in dll_names:
    exe_manifest_dependencies_list.append(
        "<dependency>"
        "<dependentAssembly>"
        "<assemblyIdentity type='win32' name='chrome.{dll_name}' "
        "version='0.0.0.0' language='*'/>"
        "</dependentAssembly>"
        "</dependency>".format(dll_name=name))

  exe_manifest_dependencies = ''.join(exe_manifest_dependencies_list)

  # Write a modified chrome.exe.manifest beside chrome.exe.
  CopyAndAugmentManifest(build_dir, chrome_dir, 'chrome.exe.manifest',
                         exe_manifest_dependencies, '</assembly>')

  # Write a modified setup.exe.manifest beside setup.exe in
  # |version_dir|/Installer.
  CopyAndAugmentManifest(build_dir, installer_dir, 'setup.exe.manifest',
                         exe_manifest_dependencies, '</assembly>')

  # Generate assembly manifests for each DLL in |dlls|. These do not interfere
  # with the private manifests potentially embedded in each DLL. They simply
  # allow chrome.exe and setup.exe to see those DLLs although they are in a
  # separate directory post-install.
  for name in dll_names:
    dll_manifest = (
        "<assembly\n"
        "    xmlns='urn:schemas-microsoft-com:asm.v1' manifestVersion='1.0'>\n"
        "  <assemblyIdentity name='chrome.{dll_name}' version='0.0.0.0'\n"
        "      type='win32'/>\n"
        "  <file name='{dll_name}.dll'/>\n"
        "</assembly>".format(dll_name=name))

    dll_manifest_file = open(os.path.join(
        version_dir, "chrome.{dll_name}.manifest".format(dll_name=name)), 'w')
    dll_manifest_file.write(dll_manifest)
    dll_manifest_file.close()


def main(options):
  """Main method that reads input file, creates archive file and write
  resource input file.
  """
  current_version = BuildVersion(options.build_dir)

  config = Readconfig(options.input_file, current_version)

  (staging_dir, temp_dir) = MakeStagingDirectories(options.staging_dir)

  prev_version = GetPrevVersion(options.build_dir, temp_dir,
                                options.last_chrome_installer,
                                options.output_name)

  # Preferentially copy the files we can find from the output_dir, as
  # this is where we'll find the Syzygy-optimized executables when
  # building the optimized mini_installer.
  if options.build_dir != options.output_dir:
    CopyAllFilesToStagingDir(config, options.distribution,
                             staging_dir, options.output_dir,
                             options.enable_hidpi, options.enable_touch_ui)

  # Now copy the remainder of the files from the build dir.
  CopyAllFilesToStagingDir(config, options.distribution,
                           staging_dir, options.build_dir,
                           options.enable_hidpi, options.enable_touch_ui)

  if options.component_build == '1':
    DoComponentBuildTasks(staging_dir, options.build_dir,
                          options.target_arch, current_version)

  version_numbers = current_version.split('.')
  current_build_number = version_numbers[2] + '.' + version_numbers[3]
  prev_build_number = ''
  if prev_version:
    version_numbers = prev_version.split('.')
    prev_build_number = version_numbers[2] + '.' + version_numbers[3]

  # Name of the archive file built (for example - chrome.7z or
  # patch-<old_version>-<new_version>.7z or patch-<new_version>.7z
  archive_file = CreateArchiveFile(options, staging_dir,
                                   current_build_number, prev_build_number)

  setup_file = PrepareSetupExec(options,
                                current_build_number, prev_build_number)

  CreateResourceInputFile(options.output_dir, options.setup_exe_format,
                          archive_file, setup_file, options.resource_file_path)

def _ParseOptions():
  parser = optparse.OptionParser()
  parser.add_option('-i', '--input_file',
      help='Input file describing which files to archive.')
  parser.add_option('-b', '--build_dir',
      help='Build directory. The paths in input_file are relative to this.')
  parser.add_option('--staging_dir',
      help='Staging directory where intermediate files and directories '
           'will be created')
  parser.add_option('-o', '--output_dir',
      help='The output directory where the archives will be written. '
            'Defaults to the build_dir.')
  parser.add_option('--resource_file_path',
      help='The path where the resource file will be output. '
           'Defaults to %s in the build directory.' %
               MINI_INSTALLER_INPUT_FILE)
  parser.add_option('-d', '--distribution',
      help='Name of Chromium Distribution. Optional.')
  parser.add_option('-s', '--skip_rebuild_archive',
      default="False", help='Skip re-building Chrome.7z archive if it exists.')
  parser.add_option('-l', '--last_chrome_installer',
      help='Generate differential installer. The value of this parameter '
           'specifies the directory that contains base versions of '
           'setup.exe, courgette.exe (if --diff_algorithm is COURGETTE) '
           '& chrome.7z.')
  parser.add_option('-f', '--setup_exe_format', default='COMPRESSED',
      help='How setup.exe should be included {COMPRESSED|DIFF|FULL}.')
  parser.add_option('-a', '--diff_algorithm', default='BSDIFF',
      help='Diff algorithm to use when generating differential patches '
           '{BSDIFF|COURGETTE}.')
  parser.add_option('-n', '--output_name', default='chrome',
      help='Name used to prefix names of generated archives.')
  parser.add_option('--enable_hidpi', default='0',
      help='Whether to include HiDPI resource files.')
  parser.add_option('--enable_touch_ui', default='0',
      help='Whether to include resource files from the "TOUCH" section of the '
           'input file.')
  parser.add_option('--component_build', default='0',
      help='Whether this archive is packaging a component build. This will '
           'also turn off compression of chrome.7z into chrome.packed.7z and '
           'helpfully delete any old chrome.packed.7z in |output_dir|.')
  parser.add_option('--target_arch', default='x86',
      help='Specify the target architecture for installer - this is used '
           'to determine which CRT runtime files to pull and package '
           'with the installer archive {x86|x64}.')

  options, _ = parser.parse_args()
  if not options.build_dir:
    parser.error('You must provide a build dir.')

  options.build_dir = os.path.normpath(options.build_dir)

  if not options.staging_dir:
    parser.error('You must provide a staging dir.')

  if not options.input_file:
    parser.error('You must provide an input file')

  if not options.output_dir:
    options.output_dir = options.build_dir

  if not options.resource_file_path:
    options.resource_file_path = os.path.join(options.build_dir,
                                              MINI_INSTALLER_INPUT_FILE)

  return options


if '__main__' == __name__:
  print sys.argv
  sys.exit(main(_ParseOptions()))
