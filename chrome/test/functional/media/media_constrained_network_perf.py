#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Records metrics on playing media under constrained network conditions.

Spins up a Constrained Network Server (CNS) and runs through a test matrix of
bandwidth, latency, and packet loss settings.  Tests running media files defined
in _TEST_MEDIA_EPP record the extra-play-percentage (EPP) metric and the
time-to-playback (TTP) metric in a format consumable by the Chromium perf bots.
Other tests running media files defined in _TEST_MEDIA_NO_EPP record only the
TTP metric.

Since even a small number of different settings yields a large test matrix, the
design is threaded... however PyAuto is not, so a global lock is used when calls
into PyAuto are necessary.  The number of threads can be set by _TEST_THREADS.

The CNS code is located under: <root>/src/media/tools/constrained_network_server
"""

import logging
import os
import posixpath
import Queue

import pyauto_media
import pyauto_utils

import cns_test_base
import worker_thread

# The network constraints used for measuring ttp and epp.
# Previous tests with 2% and 5% packet loss resulted in inconsistent data. Thus
# packet loss is not used often in perf tests. Tests with very low bandwidth,
# such as 56K Dial-up resulted in very slow tests (about 8 mins to run each
# test iteration). In addition, metrics for Dial-up would be out of range of the
# other tests metrics, making the graphs hard to read.
_TESTS_TO_RUN = [cns_test_base.Cable,
                 cns_test_base.Wifi,
                 cns_test_base.DSL,
                 cns_test_base.Slow,
                 cns_test_base.NoConstraints]

# HTML test path; relative to src/chrome/test/data.  Loads a test video and
# records metrics in JavaScript.
_TEST_HTML_PATH = os.path.join(
    'media', 'html', 'media_constrained_network.html')

# Number of threads to use during testing.
_TEST_THREADS = 3

# Number of times we run the same test to eliminate outliers.
_TEST_ITERATIONS = 3

# Media file names used for measuring epp and tpp.
_TEST_MEDIA_EPP = ['roller.webm']
_TEST_MEDIA_EPP.extend(posixpath.join('crowd', name) for name in
                       ['crowd360.ogv', 'crowd.wav', 'crowd.ogg'])

# Media file names used for measuring tpp without epp.
_TEST_MEDIA_NO_EPP = [posixpath.join('dartmoor', name) for name in
                      ['dartmoor2.ogg', 'dartmoor2.m4a', 'dartmoor2.mp3',
                       'dartmoor2.wav']]
_TEST_MEDIA_NO_EPP.extend(posixpath.join('crowd', name) for name in
                          ['crowd1080.webm', 'crowd1080.ogv', 'crowd1080.mp4',
                           'crowd360.webm', 'crowd360.mp4'])

# Timeout values for epp and ttp tests in seconds.
_TEST_EPP_TIMEOUT = 180
_TEST_TTP_TIMEOUT = 20


class CNSWorkerThread(worker_thread.WorkerThread):
  """Worker thread.  Runs a test for each task in the queue."""

  def __init__(self, *args, **kwargs):
    """Sets up CNSWorkerThread class variables."""
    # Allocate local vars before WorkerThread.__init__ runs the thread.
    self._metrics = {}
    self._test_iterations = _TEST_ITERATIONS
    worker_thread.WorkerThread.__init__(self, *args, **kwargs)

  def _HaveMetricOrError(self, var_name, unique_url):
    """Checks if the page has variable value ready or if an error has occured.

    The varaible value must be set to < 0 pre-run.

    Args:
      var_name: The variable name to check the metric for.
      unique_url: The url of the page to check for the variable's metric.

    Returns:
      True is the var_name value is >=0 or if an error_msg exists.
    """
    self._metrics[var_name] = int(self.GetDOMValue(var_name, url=unique_url))
    end_test = self.GetDOMValue('endTest', url=unique_url)

    return self._metrics[var_name] >= 0 or end_test

  def _GetEventsLog(self, unique_url):
    """Returns the log of video events fired while running the test.

    Args:
      unique_url: The url of the page identifying the test.
    """
    return self.GetDOMValue('eventsMsg', url=unique_url)

  def _GetVideoProgress(self, unique_url):
    """Gets the video's current play progress percentage.

    Args:
      unique_url: The url of the page to check for video play progress.
    """
    return int(self.CallJavascriptFunc('calculateProgress', url=unique_url))

  def RunTask(self, unique_url, task):
    """Runs the specific task on the url given.

    It is assumed that a tab with the unique_url is already loaded.
    Args:
      unique_url: A unique identifier of the test page.
      task: A (series_name, settings, file_name, run_epp) tuple.
    Returns:
      True if at least one iteration of the tests run as expected.
    """
    ttp_results = []
    epp_results = []
    # Build video source URL.  Values <= 0 mean the setting is disabled.
    series_name, settings, (file_name, run_epp) = task
    video_url = cns_test_base.GetFileURL(
        file_name, bandwidth=settings[0], latency=settings[1],
        loss=settings[2], new_port=True)

    graph_name = series_name + '_' + os.path.basename(file_name)
    for iter_num in xrange(self._test_iterations):
      # Start the test!
      self.CallJavascriptFunc('startTest', [video_url], url=unique_url)

      # Wait until the necessary metrics have been collected.
      self._metrics['epp'] = self._metrics['ttp'] = -1
      self.WaitUntil(self._HaveMetricOrError, args=['ttp', unique_url],
                     retry_sleep=1, timeout=_TEST_EPP_TIMEOUT, debug=False)
      # Do not wait for epp if ttp is not available.
      if self._metrics['ttp'] >= 0:
        ttp_results.append(self._metrics['ttp'])
        if run_epp:
          self.WaitUntil(
              self._HaveMetricOrError, args=['epp', unique_url], retry_sleep=2,
              timeout=_TEST_EPP_TIMEOUT, debug=False)

          if self._metrics['epp'] >= 0:
            epp_results.append(self._metrics['epp'])

          logging.debug('Iteration:%d - Test %s ended with %d%% of the video '
                        'played.', iter_num, graph_name,
                        self._GetVideoProgress(unique_url),)

      if self._metrics['ttp'] < 0 or (run_epp and self._metrics['epp'] < 0):
        logging.error('Iteration:%d - Test %s failed to end gracefully due '
                      'to time-out or error.\nVideo events fired:\n%s',
                      iter_num, graph_name, self._GetEventsLog(unique_url))

    # End of iterations, print results,
    pyauto_utils.PrintPerfResult('ttp', graph_name, ttp_results, 'ms')

    # Return true if we got at least one result to report.
    if run_epp:
      pyauto_utils.PrintPerfResult('epp', graph_name, epp_results, '%')
      return len(epp_results) != 0
    return len(ttp_results) != 0


class MediaConstrainedNetworkPerfTest(cns_test_base.CNSTestBase):
  """PyAuto test container.  See file doc string for more information."""

  def _RunDummyTest(self, test_path):
    """Runs a dummy test with high bandwidth and no latency or packet loss.

    Fails the unit test if the dummy test does not end.

    Args:
      test_path: Path to HTML/JavaScript test code.
    """
    tasks = Queue.Queue()
    tasks.put(('Dummy Test', [5000, 0, 0], (_TEST_MEDIA_EPP[0], True)))
    # Dummy test should successfully finish by passing all the tests.
    if worker_thread.RunWorkerThreads(self, CNSWorkerThread, tasks, 1,
                                      test_path):
      self.fail('Failed to run dummy test.')

  def testConstrainedNetworkPerf(self):

    """Starts CNS, spins up worker threads to run through _TEST_CONSTRAINTS."""
    # Run a dummy test to avoid Chrome/CNS startup overhead.
    logging.debug('Starting a dummy test to avoid Chrome/CNS startup overhead.')
    self._RunDummyTest(_TEST_HTML_PATH)
    logging.debug('Dummy test has finished. Starting real perf tests.')

    # Tests that wait for EPP metrics.
    media_files = [(name, True) for name in _TEST_MEDIA_EPP]
    media_files.extend((name, False) for name in _TEST_MEDIA_NO_EPP)
    tasks = cns_test_base.CreateCNSPerfTasks(_TESTS_TO_RUN, media_files)
    if worker_thread.RunWorkerThreads(self, CNSWorkerThread, tasks,
                                      _TEST_THREADS, _TEST_HTML_PATH):
      self.fail('Some tests failed to run as expected.')


if __name__ == '__main__':
  pyauto_media.Main()
