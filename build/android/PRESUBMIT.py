# Copyright (c) 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Presubmit script for android buildbot.

See http://dev.chromium.org/developers/how-tos/depottools/presubmit-scripts for
details on the presubmit API built into depot_tools.
"""


def CommonChecks(input_api, output_api):
  output = []

  def J(*dirs):
    """Returns a path relative to presubmit directory."""
    return input_api.os_path.join(input_api.PresubmitLocalPath(), *dirs)

  build_pys = [
      r'gyp/.*\.py$',
      r'gn/.*\.py',
      r'incremental_install/.*\.py',
  ]
  output.extend(input_api.canned_checks.RunPylint(
      input_api,
      output_api,
      pylintrc='pylintrc',
      # symbols has its own PRESUBMIT.py
      black_list=build_pys + [r'pylib/symbols/.*\.py$'],
      extra_paths_list=[J(), J('buildbot')]))
  output.extend(input_api.canned_checks.RunPylint(
      input_api,
      output_api,
      white_list=build_pys,
      extra_paths_list=[J('gyp'), J('gn')]))

  # Disabled due to http://crbug.com/410936
  #output.extend(input_api.canned_checks.RunUnitTestsInDirectory(
  #input_api, output_api, J('buildbot', 'tests')))

  pylib_test_env = dict(input_api.environ)
  pylib_test_env.update({
      'PYTHONPATH': input_api.PresubmitLocalPath(),
      'PYTHONDONTWRITEBYTECODE': '1',
  })
  output.extend(input_api.canned_checks.RunUnitTests(
      input_api,
      output_api,
      unit_tests=[
          J('.', 'emma_coverage_stats_test.py'),
          J('devil', 'android', 'fastboot_utils_test.py'),
          J('devil', 'android', 'battery_utils_test.py'),
          J('devil', 'android', 'device_utils_test.py'),
          J('devil', 'android', 'md5sum_test.py'),
          J('devil', 'android', 'logcat_monitor_test.py'),
          J('devil', 'utils', 'cmd_helper_test.py'),
          J('devil', 'utils', 'timeout_retry_unittest.py'),
          J('gyp', 'util', 'md5_check_test.py'),
          J('play_services', 'update_test.py'),
          J('pylib', 'base', 'test_dispatcher_unittest.py'),
          J('pylib', 'gtest', 'gtest_test_instance_test.py'),
          J('pylib', 'instrumentation',
            'instrumentation_test_instance_test.py'),
          J('pylib', 'results', 'json_results_test.py'),
      ],
      env=pylib_test_env))
  return output


def CheckChangeOnUpload(input_api, output_api):
  return CommonChecks(input_api, output_api)


def CheckChangeOnCommit(input_api, output_api):
  return CommonChecks(input_api, output_api)
