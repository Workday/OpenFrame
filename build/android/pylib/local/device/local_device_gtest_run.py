# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import imp
import itertools
import os
import posixpath

from devil.android import device_errors
from devil.android import device_temp_file
from devil.android import ports
from devil.utils import reraiser_thread
from incremental_install import installer
from pylib import constants
from pylib.gtest import gtest_test_instance
from pylib.local import local_test_server_spawner
from pylib.local.device import local_device_environment
from pylib.local.device import local_device_test_run

_COMMAND_LINE_FLAGS_SUPPORTED = True

_MAX_INLINE_FLAGS_LENGTH = 50  # Arbitrarily chosen.
_EXTRA_COMMAND_LINE_FILE = (
    'org.chromium.native_test.NativeTestActivity.CommandLineFile')
_EXTRA_COMMAND_LINE_FLAGS = (
    'org.chromium.native_test.NativeTestActivity.CommandLineFlags')
_EXTRA_TEST_LIST = (
    'org.chromium.native_test.NativeTestInstrumentationTestRunner'
        '.TestList')
_EXTRA_TEST = (
    'org.chromium.native_test.NativeTestInstrumentationTestRunner'
        '.Test')

_MAX_SHARD_SIZE = 256
_SECONDS_TO_NANOS = int(1e9)

# The amount of time a test executable may run before it gets killed.
_TEST_TIMEOUT_SECONDS = 30*60

# TODO(jbudorick): Move this up to the test instance if the net test server is
# handled outside of the APK for the remote_device environment.
_SUITE_REQUIRES_TEST_SERVER_SPAWNER = [
  'components_browsertests', 'content_unittests', 'content_browsertests',
  'net_unittests', 'unit_tests'
]

# No-op context manager. If we used Python 3, we could change this to
# contextlib.ExitStack()
class _NullContextManager(object):
  def __enter__(self):
    pass
  def __exit__(self, *args):
    pass


# TODO(jbudorick): Move this inside _ApkDelegate once TestPackageApk is gone.
def PullAppFilesImpl(device, package, files, directory):
  device_dir = device.GetApplicationDataDirectory(package)
  host_dir = os.path.join(directory, str(device))
  for f in files:
    device_file = posixpath.join(device_dir, f)
    host_file = os.path.join(host_dir, *f.split(posixpath.sep))
    host_file_base, ext = os.path.splitext(host_file)
    for i in itertools.count():
      host_file = '%s_%d%s' % (host_file_base, i, ext)
      if not os.path.exists(host_file):
        break
    device.PullFile(device_file, host_file)


class _ApkDelegate(object):
  def __init__(self, test_instance):
    self._activity = test_instance.activity
    self._apk_helper = test_instance.apk_helper
    self._package = test_instance.package
    self._runner = test_instance.runner
    self._permissions = test_instance.permissions
    self._suite = test_instance.suite
    self._component = '%s/%s' % (self._package, self._runner)
    self._extras = test_instance.extras

  def Install(self, device, incremental=False):
    if not incremental:
      device.Install(self._apk_helper, reinstall=True,
                     permissions=self._permissions)
      return

    installer_script = os.path.join(constants.GetOutDirectory(), 'bin',
                                    'install_%s_apk_incremental' % self._suite)
    try:
      install_wrapper = imp.load_source('install_wrapper', installer_script)
    except IOError:
      raise Exception(('Incremental install script not found: %s\n'
                       'Make sure to first build "%s_incremental"') %
                      (installer_script, self._suite))
    params = install_wrapper.GetInstallParameters()

    installer.Install(device, self._apk_helper, split_globs=params['splits'],
                      native_libs=params['native_libs'],
                      dex_files=params['dex_files'])

  def Run(self, test, device, flags=None, **kwargs):
    extras = dict(self._extras)

    if ('timeout' in kwargs
        and gtest_test_instance.EXTRA_SHARD_NANO_TIMEOUT not in extras):
      # Make sure the instrumentation doesn't kill the test before the
      # scripts do. The provided timeout value is in seconds, but the
      # instrumentation deals with nanoseconds because that's how Android
      # handles time.
      extras[gtest_test_instance.EXTRA_SHARD_NANO_TIMEOUT] = int(
          kwargs['timeout'] * _SECONDS_TO_NANOS)

    command_line_file = _NullContextManager()
    if flags:
      if len(flags) > _MAX_INLINE_FLAGS_LENGTH:
        command_line_file = device_temp_file.DeviceTempFile(device.adb)
        device.WriteFile(command_line_file.name, '_ %s' % flags)
        extras[_EXTRA_COMMAND_LINE_FILE] = command_line_file.name
      else:
        extras[_EXTRA_COMMAND_LINE_FLAGS] = flags

    test_list_file = _NullContextManager()
    if test:
      if len(test) > 1:
        test_list_file = device_temp_file.DeviceTempFile(device.adb)
        device.WriteFile(test_list_file.name, '\n'.join(test))
        extras[_EXTRA_TEST_LIST] = test_list_file.name
      else:
        extras[_EXTRA_TEST] = test[0]

    with command_line_file, test_list_file:
      try:
        return device.StartInstrumentation(
            self._component, extras=extras, raw=False, **kwargs)
      except Exception:
        device.ForceStop(self._package)
        raise

  def PullAppFiles(self, device, files, directory):
    PullAppFilesImpl(device, self._package, files, directory)

  def Clear(self, device):
    device.ClearApplicationState(self._package, permissions=self._permissions)


class _ExeDelegate(object):
  def __init__(self, tr, exe):
    self._exe_host_path = exe
    self._exe_file_name = os.path.split(exe)[-1]
    self._exe_device_path = '%s/%s' % (
        constants.TEST_EXECUTABLE_DIR, self._exe_file_name)
    deps_host_path = self._exe_host_path + '_deps'
    if os.path.exists(deps_host_path):
      self._deps_host_path = deps_host_path
      self._deps_device_path = self._exe_device_path + '_deps'
    else:
      self._deps_host_path = None
    self._test_run = tr

  def Install(self, device, incremental=False):
    assert not incremental
    # TODO(jbudorick): Look into merging this with normal data deps pushing if
    # executables become supported on nonlocal environments.
    host_device_tuples = [(self._exe_host_path, self._exe_device_path)]
    if self._deps_host_path:
      host_device_tuples.append((self._deps_host_path, self._deps_device_path))
    device.PushChangedFiles(host_device_tuples)

  def Run(self, test, device, flags=None, **kwargs):
    tool = self._test_run.GetTool(device).GetTestWrapper()
    if tool:
      cmd = [tool]
    else:
      cmd = []
    cmd.append(self._exe_device_path)

    if test:
      cmd.append('--gtest_filter=%s' % ':'.join(test))
    if flags:
      cmd.append(flags)
    cwd = constants.TEST_EXECUTABLE_DIR

    env = {
      'LD_LIBRARY_PATH':
          '%s/%s_deps' % (constants.TEST_EXECUTABLE_DIR, self._exe_file_name),
    }
    try:
      gcov_strip_depth = os.environ['NATIVE_COVERAGE_DEPTH_STRIP']
      external = device.GetExternalStoragePath()
      env['GCOV_PREFIX'] = '%s/gcov' % external
      env['GCOV_PREFIX_STRIP'] = gcov_strip_depth
    except (device_errors.CommandFailedError, KeyError):
      pass

    output = device.RunShellCommand(
        cmd, cwd=cwd, env=env, check_return=True, large_output=True, **kwargs)
    return output

  def PullAppFiles(self, device, files, directory):
    pass

  def Clear(self, device):
    device.KillAll(self._exe_file_name, blocking=True, timeout=30, quiet=True)


class LocalDeviceGtestRun(local_device_test_run.LocalDeviceTestRun):

  def __init__(self, env, test_instance):
    assert isinstance(env, local_device_environment.LocalDeviceEnvironment)
    assert isinstance(test_instance, gtest_test_instance.GtestTestInstance)
    super(LocalDeviceGtestRun, self).__init__(env, test_instance)

    if self._test_instance.apk:
      self._delegate = _ApkDelegate(self._test_instance)
    elif self._test_instance.exe:
      self._delegate = _ExeDelegate(self, self._test_instance.exe)

    self._servers = {}

  #override
  def TestPackage(self):
    return self._test_instance.suite

  #override
  def SetUp(self):
    @local_device_test_run.handle_shard_failures
    def individual_device_set_up(dev):
      def install_apk():
        # Install test APK.
        self._delegate.Install(dev, incremental=self._env.incremental_install)

      def push_test_data():
        # Push data dependencies.
        external_storage = dev.GetExternalStoragePath()
        data_deps = self._test_instance.GetDataDependencies()
        host_device_tuples = [
            (h, d if d is not None else external_storage)
            for h, d in data_deps]
        dev.PushChangedFiles(host_device_tuples)

      def init_tool_and_start_servers():
        tool = self.GetTool(dev)
        tool.CopyFiles(dev)
        tool.SetupEnvironment()

        self._servers[str(dev)] = []
        if self.TestPackage() in _SUITE_REQUIRES_TEST_SERVER_SPAWNER:
          self._servers[str(dev)].append(
              local_test_server_spawner.LocalTestServerSpawner(
                  ports.AllocateTestServerPort(), dev, tool))

        for s in self._servers[str(dev)]:
          s.SetUp()

      steps = (install_apk, push_test_data, init_tool_and_start_servers)
      if self._env.concurrent_adb:
        reraiser_thread.RunAsync(steps)
      else:
        for step in steps:
          step()

    self._env.parallel_devices.pMap(individual_device_set_up)

  #override
  def _ShouldShard(self):
    return True

  #override
  def _CreateShards(self, tests):
    device_count = len(self._env.devices)
    shards = []
    for i in xrange(0, device_count):
      unbounded_shard = tests[i::device_count]
      shards += [unbounded_shard[j:j+_MAX_SHARD_SIZE]
                 for j in xrange(0, len(unbounded_shard), _MAX_SHARD_SIZE)]
    return shards

  #override
  def _GetTests(self):
    # Even when there's only one device, it still makes sense to retrieve the
    # test list so that tests can be split up and run in batches rather than all
    # at once (since test output is not streamed).
    @local_device_test_run.handle_shard_failures_with(
        on_failure=self._env.BlacklistDevice)
    def list_tests(dev):
      tests = self._delegate.Run(
          None, dev, flags='--gtest_list_tests', timeout=20)
      tests = gtest_test_instance.ParseGTestListTests(tests)
      tests = self._test_instance.FilterTests(tests)
      return tests

    # Query all devices in case one fails.
    test_lists = self._env.parallel_devices.pMap(list_tests).pGet(None)
    # TODO(agrieve): Make this fail rather than return an empty list when
    #     all devices fail.
    return list(sorted(set().union(*[set(tl) for tl in test_lists if tl])))

  #override
  def _RunTest(self, device, test):
    # Run the test.
    timeout = (self._test_instance.shard_timeout
               * self.GetTool(device).GetTimeoutScale())
    output = self._delegate.Run(
        test, device, flags=self._test_instance.test_arguments,
        timeout=timeout, retries=0)
    for s in self._servers[str(device)]:
      s.Reset()
    if self._test_instance.app_files:
      self._delegate.PullAppFiles(device, self._test_instance.app_files,
                                  self._test_instance.app_file_dir)
    # Clearing data when using incremental install wipes out cached optimized
    # dex files (and shouldn't be necessary by tests anyways).
    if not self._env.incremental_install:
      self._delegate.Clear(device)

    # Parse the output.
    # TODO(jbudorick): Transition test scripts away from parsing stdout.
    results = self._test_instance.ParseGTestOutput(output)
    return results

  #override
  def TearDown(self):
    @local_device_test_run.handle_shard_failures
    def individual_device_tear_down(dev):
      for s in self._servers.get(str(dev), []):
        s.TearDown()

      tool = self.GetTool(dev)
      tool.CleanUpEnvironment()

    self._env.parallel_devices.pMap(individual_device_tear_down)
