# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import ast
import contextlib
import fnmatch
import json
import os
import pipes
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import zipfile

# Some clients do not add //build/android/gyp to PYTHONPATH.
import md5_check  # pylint: disable=relative-import

sys.path.append(os.path.join(os.path.dirname(__file__), os.pardir, os.pardir))
from pylib import constants

COLORAMA_ROOT = os.path.join(constants.DIR_SOURCE_ROOT,
                             'third_party', 'colorama', 'src')
# aapt should ignore OWNERS files in addition the default ignore pattern.
AAPT_IGNORE_PATTERN = ('!OWNERS:!.svn:!.git:!.ds_store:!*.scc:.*:<dir>_*:' +
                       '!CVS:!thumbs.db:!picasa.ini:!*~:!*.d.stamp')
_HERMETIC_TIMESTAMP = (2001, 1, 1, 0, 0, 0)
_HERMETIC_FILE_ATTR = (0644 << 16L)


@contextlib.contextmanager
def TempDir():
  dirname = tempfile.mkdtemp()
  try:
    yield dirname
  finally:
    shutil.rmtree(dirname)


def MakeDirectory(dir_path):
  try:
    os.makedirs(dir_path)
  except OSError:
    pass


def DeleteDirectory(dir_path):
  if os.path.exists(dir_path):
    shutil.rmtree(dir_path)


def Touch(path, fail_if_missing=False):
  if fail_if_missing and not os.path.exists(path):
    raise Exception(path + ' doesn\'t exist.')

  MakeDirectory(os.path.dirname(path))
  with open(path, 'a'):
    os.utime(path, None)


def FindInDirectory(directory, filename_filter):
  files = []
  for root, _dirnames, filenames in os.walk(directory):
    matched_files = fnmatch.filter(filenames, filename_filter)
    files.extend((os.path.join(root, f) for f in matched_files))
  return files


def FindInDirectories(directories, filename_filter):
  all_files = []
  for directory in directories:
    all_files.extend(FindInDirectory(directory, filename_filter))
  return all_files


def ParseGnList(gn_string):
  return ast.literal_eval(gn_string)


def ParseGypList(gyp_string):
  # The ninja generator doesn't support $ in strings, so use ## to
  # represent $.
  # TODO(cjhopman): Remove when
  # https://code.google.com/p/gyp/issues/detail?id=327
  # is addressed.
  gyp_string = gyp_string.replace('##', '$')

  if gyp_string.startswith('['):
    return ParseGnList(gyp_string)
  return shlex.split(gyp_string)


def CheckOptions(options, parser, required=None):
  if not required:
    return
  for option_name in required:
    if getattr(options, option_name) is None:
      parser.error('--%s is required' % option_name.replace('_', '-'))


def WriteJson(obj, path, only_if_changed=False):
  old_dump = None
  if os.path.exists(path):
    with open(path, 'r') as oldfile:
      old_dump = oldfile.read()

  new_dump = json.dumps(obj, sort_keys=True, indent=2, separators=(',', ': '))

  if not only_if_changed or old_dump != new_dump:
    with open(path, 'w') as outfile:
      outfile.write(new_dump)


def ReadJson(path):
  with open(path, 'r') as jsonfile:
    return json.load(jsonfile)


class CalledProcessError(Exception):
  """This exception is raised when the process run by CheckOutput
  exits with a non-zero exit code."""

  def __init__(self, cwd, args, output):
    super(CalledProcessError, self).__init__()
    self.cwd = cwd
    self.args = args
    self.output = output

  def __str__(self):
    # A user should be able to simply copy and paste the command that failed
    # into their shell.
    copyable_command = '( cd {}; {} )'.format(os.path.abspath(self.cwd),
        ' '.join(map(pipes.quote, self.args)))
    return 'Command failed: {}\n{}'.format(copyable_command, self.output)


# This can be used in most cases like subprocess.check_output(). The output,
# particularly when the command fails, better highlights the command's failure.
# If the command fails, raises a build_utils.CalledProcessError.
def CheckOutput(args, cwd=None,
                print_stdout=False, print_stderr=True,
                stdout_filter=None,
                stderr_filter=None,
                fail_func=lambda returncode, stderr: returncode != 0):
  if not cwd:
    cwd = os.getcwd()

  child = subprocess.Popen(args,
      stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=cwd)
  stdout, stderr = child.communicate()

  if stdout_filter is not None:
    stdout = stdout_filter(stdout)

  if stderr_filter is not None:
    stderr = stderr_filter(stderr)

  if fail_func(child.returncode, stderr):
    raise CalledProcessError(cwd, args, stdout + stderr)

  if print_stdout:
    sys.stdout.write(stdout)
  if print_stderr:
    sys.stderr.write(stderr)

  return stdout


def GetModifiedTime(path):
  # For a symlink, the modified time should be the greater of the link's
  # modified time and the modified time of the target.
  return max(os.lstat(path).st_mtime, os.stat(path).st_mtime)


def IsTimeStale(output, inputs):
  if not os.path.exists(output):
    return True

  output_time = GetModifiedTime(output)
  for i in inputs:
    if GetModifiedTime(i) > output_time:
      return True
  return False


def IsDeviceReady():
  device_state = CheckOutput(['adb', 'get-state'])
  return device_state.strip() == 'device'


def CheckZipPath(name):
  if os.path.normpath(name) != name:
    raise Exception('Non-canonical zip path: %s' % name)
  if os.path.isabs(name):
    raise Exception('Absolute zip path: %s' % name)


def ExtractAll(zip_path, path=None, no_clobber=True, pattern=None,
               predicate=None):
  if path is None:
    path = os.getcwd()
  elif not os.path.exists(path):
    MakeDirectory(path)

  with zipfile.ZipFile(zip_path) as z:
    for name in z.namelist():
      if name.endswith('/'):
        continue
      if pattern is not None:
        if not fnmatch.fnmatch(name, pattern):
          continue
      if predicate and not predicate(name):
        continue
      CheckZipPath(name)
      if no_clobber:
        output_path = os.path.join(path, name)
        if os.path.exists(output_path):
          raise Exception(
              'Path already exists from zip: %s %s %s'
              % (zip_path, name, output_path))
      z.extract(name, path)


def AddToZipHermetic(zip_file, zip_path, src_path=None, data=None,
                     compress=None):
  """Adds a file to the given ZipFile with a hard-coded modified time.

  Args:
    zip_file: ZipFile instance to add the file to.
    zip_path: Destination path within the zip file.
    src_path: Path of the source file. Mutually exclusive with |data|.
    data: File data as a string.
    compress: Whether to enable compression. Default is take from ZipFile
        constructor.
  """
  assert (src_path is None) != (data is None), (
      '|src_path| and |data| are mutually exclusive.')
  CheckZipPath(zip_path)
  zipinfo = zipfile.ZipInfo(filename=zip_path, date_time=_HERMETIC_TIMESTAMP)
  zipinfo.external_attr = _HERMETIC_FILE_ATTR

  if src_path:
    with file(src_path) as f:
      data = f.read()

  # zipfile will deflate even when it makes the file bigger. To avoid
  # growing files, disable compression at an arbitrary cut off point.
  if len(data) < 16:
    compress = False

  # None converts to ZIP_STORED, when passed explicitly rather than the
  # default passed to the ZipFile constructor.
  args = []
  if compress is not None:
    args.append(zipfile.ZIP_DEFLATED if compress else zipfile.ZIP_STORED)
  zip_file.writestr(zipinfo, data, *args)


def DoZip(inputs, output, base_dir=None):
  """Creates a zip file from a list of files.

  Args:
    inputs: A list of paths to zip, or a list of (zip_path, fs_path) tuples.
    output: Destination .zip file.
    base_dir: Prefix to strip from inputs.
  """
  input_tuples = []
  for tup in inputs:
    if isinstance(tup, basestring):
      tup = (os.path.relpath(tup, base_dir), tup)
    input_tuples.append(tup)

  # Sort by zip path to ensure stable zip ordering.
  input_tuples.sort(key=lambda tup: tup[0])
  with zipfile.ZipFile(output, 'w') as outfile:
    for zip_path, fs_path in input_tuples:
      AddToZipHermetic(outfile, zip_path, src_path=fs_path)


def ZipDir(output, base_dir):
  """Creates a zip file from a directory."""
  inputs = []
  for root, _, files in os.walk(base_dir):
    for f in files:
      inputs.append(os.path.join(root, f))
  DoZip(inputs, output, base_dir)


def MatchesGlob(path, filters):
  """Returns whether the given path matches any of the given glob patterns."""
  return filters and any(fnmatch.fnmatch(path, f) for f in filters)


def MergeZips(output, inputs, exclude_patterns=None, path_transform=None):
  path_transform = path_transform or (lambda p, z: p)
  added_names = set()

  with zipfile.ZipFile(output, 'w') as out_zip:
    for in_file in inputs:
      with zipfile.ZipFile(in_file, 'r') as in_zip:
        for name in in_zip.namelist():
          # Ignore directories.
          if name[-1] == '/':
            continue
          dst_name = path_transform(name, in_file)
          already_added = dst_name in added_names
          if not already_added and not MatchesGlob(dst_name, exclude_patterns):
            AddToZipHermetic(out_zip, dst_name, data=in_zip.read(name))
            added_names.add(dst_name)


def PrintWarning(message):
  print 'WARNING: ' + message


def PrintBigWarning(message):
  print '*****     ' * 8
  PrintWarning(message)
  print '*****     ' * 8


def GetSortedTransitiveDependencies(top, deps_func):
  """Gets the list of all transitive dependencies in sorted order.

  There should be no cycles in the dependency graph.

  Args:
    top: a list of the top level nodes
    deps_func: A function that takes a node and returns its direct dependencies.
  Returns:
    A list of all transitive dependencies of nodes in top, in order (a node will
    appear in the list at a higher index than all of its dependencies).
  """
  def Node(dep):
    return (dep, deps_func(dep))

  # First: find all deps
  unchecked_deps = list(top)
  all_deps = set(top)
  while unchecked_deps:
    dep = unchecked_deps.pop()
    new_deps = deps_func(dep).difference(all_deps)
    unchecked_deps.extend(new_deps)
    all_deps = all_deps.union(new_deps)

  # Then: simple, slow topological sort.
  sorted_deps = []
  unsorted_deps = dict(map(Node, all_deps))
  while unsorted_deps:
    for library, dependencies in unsorted_deps.items():
      if not dependencies.intersection(unsorted_deps.keys()):
        sorted_deps.append(library)
        del unsorted_deps[library]

  return sorted_deps


def GetPythonDependencies():
  """Gets the paths of imported non-system python modules.

  A path is assumed to be a "system" import if it is outside of chromium's
  src/. The paths will be relative to the current directory.
  """
  module_paths = (m.__file__ for m in sys.modules.itervalues()
                  if m is not None and hasattr(m, '__file__'))

  abs_module_paths = map(os.path.abspath, module_paths)

  assert os.path.isabs(constants.DIR_SOURCE_ROOT)
  non_system_module_paths = [
      p for p in abs_module_paths if p.startswith(constants.DIR_SOURCE_ROOT)]
  def ConvertPycToPy(s):
    if s.endswith('.pyc'):
      return s[:-1]
    return s

  non_system_module_paths = map(ConvertPycToPy, non_system_module_paths)
  non_system_module_paths = map(os.path.relpath, non_system_module_paths)
  return sorted(set(non_system_module_paths))


def AddDepfileOption(parser):
  # TODO(agrieve): Get rid of this once we've moved to argparse.
  if hasattr(parser, 'add_option'):
    func = parser.add_option
  else:
    func = parser.add_argument
  func('--depfile',
       help='Path to depfile. Must be specified as the action\'s first output.')


def WriteDepfile(path, dependencies):
  with open(path, 'w') as depfile:
    depfile.write(path)
    depfile.write(': ')
    depfile.write(' '.join(dependencies))
    depfile.write('\n')


def ExpandFileArgs(args):
  """Replaces file-arg placeholders in args.

  These placeholders have the form:
    @FileArg(filename:key1:key2:...:keyn)

  The value of such a placeholder is calculated by reading 'filename' as json.
  And then extracting the value at [key1][key2]...[keyn].

  Note: This intentionally does not return the list of files that appear in such
  placeholders. An action that uses file-args *must* know the paths of those
  files prior to the parsing of the arguments (typically by explicitly listing
  them in the action's inputs in build files).
  """
  new_args = list(args)
  file_jsons = dict()
  r = re.compile('@FileArg\((.*?)\)')
  for i, arg in enumerate(args):
    match = r.search(arg)
    if not match:
      continue

    if match.end() != len(arg):
      raise Exception('Unexpected characters after FileArg: ' + arg)

    lookup_path = match.group(1).split(':')
    file_path = lookup_path[0]
    if not file_path in file_jsons:
      file_jsons[file_path] = ReadJson(file_path)

    expansion = file_jsons[file_path]
    for k in lookup_path[1:]:
      expansion = expansion[k]

    new_args[i] = arg[:match.start()] + str(expansion)

  return new_args


def CallAndWriteDepfileIfStale(function, options, record_path=None,
                               input_paths=None, input_strings=None,
                               output_paths=None, force=False,
                               pass_changes=False):
  """Wraps md5_check.CallAndRecordIfStale() and also writes dep & stamp files.

  Depfiles and stamp files are automatically added to output_paths when present
  in the |options| argument. They are then created after |function| is called.
  """
  if not output_paths:
    raise Exception('At least one output_path must be specified.')
  input_paths = list(input_paths or [])
  input_strings = list(input_strings or [])
  output_paths = list(output_paths or [])

  python_deps = None
  if hasattr(options, 'depfile') and options.depfile:
    python_deps = GetPythonDependencies()
    # List python deps in input_strings rather than input_paths since the
    # contents of them does not change what gets written to the depfile.
    input_strings += python_deps
    output_paths += [options.depfile]

  stamp_file = hasattr(options, 'stamp') and options.stamp
  if stamp_file:
    output_paths += [stamp_file]

  def on_stale_md5(changes):
    args = (changes,) if pass_changes else ()
    function(*args)
    if python_deps is not None:
      WriteDepfile(options.depfile, python_deps + input_paths)
    if stamp_file:
      Touch(stamp_file)

  md5_check.CallAndRecordIfStale(
      on_stale_md5,
      record_path=record_path,
      input_paths=input_paths,
      input_strings=input_strings,
      output_paths=output_paths,
      force=force,
      pass_changes=True)

