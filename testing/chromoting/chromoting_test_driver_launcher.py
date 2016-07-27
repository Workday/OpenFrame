# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Utility script to run chromoting test driver tests on the Chromoting bot."""

import argparse

from chromoting_test_utilities import InitialiseTestMachineForLinux
from chromoting_test_utilities import PrintHostLogContents
from chromoting_test_utilities import PROD_DIR_ID
from chromoting_test_utilities import RunCommandInSubProcess
from chromoting_test_utilities import TestCaseSetup
from chromoting_test_utilities import TestMachineCleanup

TEST_ENVIRONMENT_TEAR_DOWN_INDICATOR = 'Global test environment tear-down'
FAILED_INDICATOR = '[  FAILED  ]'


def LaunchCTDCommand(args, command):
  """Launches the specified chromoting test driver command.

  Args:
    args: Command line args, used for test-case startup tasks.
    command: Chromoting Test Driver command line.
  Returns:
    command, host_log_file_names: Tuple of:
    "command" if there was a test-environment failure, or any failing test, and
    list of host-log file-names.
  """
  host_log_file_names = []

  host_log_file_names.append(TestCaseSetup(args))
  results = RunCommandInSubProcess(command)

  tear_down_index = results.find(TEST_ENVIRONMENT_TEAR_DOWN_INDICATOR)
  if tear_down_index == -1:
    # The test environment did not tear down. Something went horribly wrong.
    return '[Command failed]: ' + command, host_log_file_names

  end_results_list = results[tear_down_index:].split('\n')
  failed_tests_list = []
  for result in end_results_list:
    if result.startswith(FAILED_INDICATOR):
      failed_tests_list.append(result)

  if failed_tests_list:
    test_result = '[Command]: ' + command
    # Note: Skipping the first one is intentional.
    for i in range(1, len(failed_tests_list)):
      test_result += '    ' + failed_tests_list[i]
    return test_result, host_log_file_names

  # All tests passed!
  return '', host_log_file_names


def main(args):
  InitialiseTestMachineForLinux(args.cfg_file)

  failed_tests = ''
  host_log_files = []
  with open(args.commands_file) as f:
    for line in f:
      # Replace the PROD_DIR value in the command-line with
      # the passed in value.
      line = line.replace(PROD_DIR_ID, args.prod_dir)
      # Launch specified command line for test.
      test_results, log_files = LaunchCTDCommand(args, line)
      failed_tests += test_results
      host_log_files.extend(log_files)

  # All tests completed. Include host-logs in the test results.
  PrintHostLogContents(host_log_files)

  return failed_tests, host_log_files


if __name__ == '__main__':
  parser = argparse.ArgumentParser()
  parser.add_argument('-f', '--commands_file',
                      help='path to file listing commands to be launched.')
  parser.add_argument('-p', '--prod_dir',
                      help='path to folder having product and test binaries.')
  parser.add_argument('-c', '--cfg_file',
                      help='path to test host config file.')
  parser.add_argument('--me2me_manifest_file',
                      help='path to me2me host manifest file.')
  parser.add_argument('--it2me_manifest_file',
                      help='path to it2me host manifest file.')
  parser.add_argument(
      '-u', '--user_profile_dir',
      help='path to user-profile-dir, used by connect-to-host tests.')
  command_line_args = parser.parse_args()
  host_logs = ''
  failing_tests = ''
  try:
    failing_tests, host_logs = main(command_line_args)
    if failing_tests:
      print '++++++++++FAILED TESTS++++++++++'
      print failing_tests.rstrip('\n')
      print '++++++++++++++++++++++++++++++++'
      raise Exception('At least one test failed.')
  finally:
    # Stop host and cleanup user-profile-dir.
    TestMachineCleanup(command_line_args.user_profile_dir, host_logs)
