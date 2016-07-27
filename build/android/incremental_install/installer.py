#!/usr/bin/env python
#
# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Install *_incremental.apk targets as well as their dependent files."""

import argparse
import glob
import logging
import os
import posixpath
import shutil
import sys

sys.path.append(os.path.join(os.path.dirname(__file__), os.pardir))
from devil.android import apk_helper
from devil.android import device_utils
from devil.android import device_errors
from devil.android.sdk import version_codes
from devil.utils import reraiser_thread
from pylib import constants
from pylib.utils import run_tests_helper
from pylib.utils import time_profile

prev_sys_path = list(sys.path)
sys.path.insert(0, os.path.join(os.path.dirname(__file__), os.pardir, 'gyp'))
from util import build_utils
sys.path = prev_sys_path


def _TransformDexPaths(paths):
  """Given paths like ["/a/b/c", "/a/c/d"], returns ["b.c", "c.d"]."""
  if len(paths) == 1:
    return [os.path.basename(paths[0])]

  prefix_len = len(os.path.commonprefix(paths))
  return [p[prefix_len:].replace(os.sep, '.') for p in paths]


def _Execute(concurrently, *funcs):
  """Calls all functions in |funcs| concurrently or in sequence."""
  timer = time_profile.TimeProfile()
  if concurrently:
    reraiser_thread.RunAsync(funcs)
  else:
    for f in funcs:
      f()
  timer.Stop(log=False)
  return timer


def _GetDeviceIncrementalDir(package):
  """Returns the device path to put incremental files for the given package."""
  return '/data/local/tmp/incremental-app-%s' % package


def Uninstall(device, package):
  """Uninstalls and removes all incremental files for the given package."""
  main_timer = time_profile.TimeProfile()
  device.Uninstall(package)
  device.RunShellCommand(['rm', '-rf', _GetDeviceIncrementalDir(package)],
                         check_return=True)
  logging.info('Uninstall took %s seconds.', main_timer.GetDelta())


def Install(device, apk, split_globs=None, native_libs=None, dex_files=None,
            enable_device_cache=True, use_concurrency=True,
            show_proguard_warning=False):
  """Installs the given incremental apk and all required supporting files.

  Args:
    device: A DeviceUtils instance.
    apk: The path to the apk, or an ApkHelper instance.
    split_globs: Glob patterns for any required apk splits (optional).
    native_libs: List of app's native libraries (optional).
    dex_files: List of .dex.jar files that comprise the app's Dalvik code.
    enable_device_cache: Whether to enable on-device caching of checksums.
    use_concurrency: Whether to speed things up using multiple threads.
    show_proguard_warning: Whether to print a warning about Proguard not being
        enabled after installing.
  """
  main_timer = time_profile.TimeProfile()
  install_timer = time_profile.TimeProfile()
  push_native_timer = time_profile.TimeProfile()
  push_dex_timer = time_profile.TimeProfile()

  apk = apk_helper.ToHelper(apk)
  apk_package = apk.GetPackageName()
  device_incremental_dir = _GetDeviceIncrementalDir(apk_package)

  # Install .apk(s) if any of them have changed.
  def do_install():
    install_timer.Start()
    if split_globs:
      splits = []
      for split_glob in split_globs:
        splits.extend((f for f in glob.glob(split_glob)))
      device.InstallSplitApk(apk, splits, reinstall=True,
                             allow_cached_props=True, permissions=())
    else:
      device.Install(apk, reinstall=True, permissions=())
    install_timer.Stop(log=False)

  # Push .so and .dex files to the device (if they have changed).
  def do_push_files():
    if native_libs:
      push_native_timer.Start()
      with build_utils.TempDir() as temp_dir:
        device_lib_dir = posixpath.join(device_incremental_dir, 'lib')
        for path in native_libs:
          # Note: Can't use symlinks as they don't work when
          # "adb push parent_dir" is used (like we do here).
          shutil.copy(path, os.path.join(temp_dir, os.path.basename(path)))
        device.PushChangedFiles([(temp_dir, device_lib_dir)],
                                delete_device_stale=True)
      push_native_timer.Stop(log=False)

    if dex_files:
      push_dex_timer.Start()
      # Put all .dex files to be pushed into a temporary directory so that we
      # can use delete_device_stale=True.
      with build_utils.TempDir() as temp_dir:
        device_dex_dir = posixpath.join(device_incremental_dir, 'dex')
        # Ensure no two files have the same name.
        transformed_names = _TransformDexPaths(dex_files)
        for src_path, dest_name in zip(dex_files, transformed_names):
          shutil.copy(src_path, os.path.join(temp_dir, dest_name))
        device.PushChangedFiles([(temp_dir, device_dex_dir)],
                                delete_device_stale=True)
      push_dex_timer.Stop(log=False)

  def check_selinux():
    # Marshmallow has no filesystem access whatsoever. It might be possible to
    # get things working on Lollipop, but attempts so far have failed.
    # http://crbug.com/558818
    has_selinux = device.build_version_sdk >= version_codes.LOLLIPOP
    if has_selinux and apk.HasIsolatedProcesses():
      raise Exception('Cannot use incremental installs on Android L+ without '
                      'first disabling isoloated processes.\n'
                      'To do so, use GN arg:\n'
                      '    disable_incremental_isolated_processes=true')

  cache_path = '%s/files-cache.json' % device_incremental_dir
  def restore_cache():
    if not enable_device_cache:
      logging.info('Ignoring device cache')
      return
    # Delete the cached file so that any exceptions cause the next attempt
    # to re-compute md5s.
    cmd = 'P=%s;cat $P 2>/dev/null && rm $P' % cache_path
    lines = device.RunShellCommand(cmd, check_return=False, large_output=True)
    if lines:
      device.LoadCacheData(lines[0])
    else:
      logging.info('Device cache not found: %s', cache_path)

  def save_cache():
    cache_data = device.DumpCacheData()
    device.WriteFile(cache_path, cache_data)

  # Create 2 lock files:
  # * install.lock tells the app to pause on start-up (until we release it).
  # * firstrun.lock is used by the app to pause all secondary processes until
  #   the primary process finishes loading the .dex / .so files.
  def create_lock_files():
    # Creates or zeros out lock files.
    cmd = ('D="%s";'
           'mkdir -p $D &&'
           'echo -n >$D/install.lock 2>$D/firstrun.lock')
    device.RunShellCommand(cmd % device_incremental_dir, check_return=True)

  # The firstrun.lock is released by the app itself.
  def release_installer_lock():
    device.RunShellCommand('echo > %s/install.lock' % device_incremental_dir,
                           check_return=True)

  # Concurrency here speeds things up quite a bit, but DeviceUtils hasn't
  # been designed for multi-threading. Enabling only because this is a
  # developer-only tool.
  setup_timer = _Execute(
      use_concurrency, create_lock_files, restore_cache, check_selinux)

  _Execute(use_concurrency, do_install, do_push_files)

  finalize_timer = _Execute(use_concurrency, release_installer_lock, save_cache)

  logging.info(
      'Took %s seconds (setup=%s, install=%s, libs=%s, dex=%s, finalize=%s)',
      main_timer.GetDelta(), setup_timer.GetDelta(), install_timer.GetDelta(),
      push_native_timer.GetDelta(), push_dex_timer.GetDelta(),
      finalize_timer.GetDelta())
  if show_proguard_warning:
    logging.warning('Target had proguard enabled, but incremental install uses '
                    'non-proguarded .dex files. Performance characteristics '
                    'may differ.')


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('apk_path',
                      help='The path to the APK to install.')
  parser.add_argument('--split',
                      action='append',
                      dest='splits',
                      help='A glob matching the apk splits. '
                           'Can be specified multiple times.')
  parser.add_argument('--native_lib',
                      dest='native_libs',
                      help='Path to native library (repeatable)',
                      action='append',
                      default=[])
  parser.add_argument('--dex-file',
                      dest='dex_files',
                      help='Path to dex files (repeatable)',
                      action='append',
                      default=[])
  parser.add_argument('-d', '--device', dest='device',
                      help='Target device for apk to install on.')
  parser.add_argument('--uninstall',
                      action='store_true',
                      default=False,
                      help='Remove the app and all side-loaded files.')
  parser.add_argument('--output-directory',
                      help='Path to the root build directory.')
  parser.add_argument('--no-threading',
                      action='store_false',
                      default=True,
                      dest='threading',
                      help='Do not install and push concurrently')
  parser.add_argument('--no-cache',
                      action='store_false',
                      default=True,
                      dest='cache',
                      help='Do not use cached information about what files are '
                           'currently on the target device.')
  parser.add_argument('--show-proguard-warning',
                      action='store_true',
                      default=False,
                      help='Print a warning about proguard being disabled')
  parser.add_argument('-v',
                      '--verbose',
                      dest='verbose_count',
                      default=0,
                      action='count',
                      help='Verbose level (multiple times for more)')

  args = parser.parse_args()

  run_tests_helper.SetLogLevel(args.verbose_count)
  constants.SetBuildType('Debug')
  if args.output_directory:
    constants.SetOutputDirectory(args.output_directory)

  if args.device:
    # Retries are annoying when commands fail for legitimate reasons. Might want
    # to enable them if this is ever used on bots though.
    device = device_utils.DeviceUtils(
        args.device, default_retries=0, enable_device_files_cache=True)
  else:
    devices = device_utils.DeviceUtils.HealthyDevices(
        default_retries=0, enable_device_files_cache=True)
    if not devices:
      raise device_errors.NoDevicesError()
    elif len(devices) == 1:
      device = devices[0]
    else:
      all_devices = device_utils.DeviceUtils.parallel(devices)
      msg = ('More than one device available.\n'
             'Use --device=SERIAL to select a device.\n'
             'Available devices:\n')
      descriptions = all_devices.pMap(lambda d: d.build_description).pGet(None)
      for d, desc in zip(devices, descriptions):
        msg += '  %s (%s)\n' % (d, desc)
      raise Exception(msg)

  apk = apk_helper.ToHelper(args.apk_path)
  if args.uninstall:
    Uninstall(device, apk.GetPackageName())
  else:
    Install(device, apk, split_globs=args.splits, native_libs=args.native_libs,
            dex_files=args.dex_files, enable_device_cache=args.cache,
            use_concurrency=args.threading,
            show_proguard_warning=args.show_proguard_warning)


if __name__ == '__main__':
  sys.exit(main())
