# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import os
import time

from telemetry.page.actions import wait
from telemetry.unittest import tab_test_case

class WaitActionTest(tab_test_case.TabTestCase):
  def testWaitAction(self):
    unittest_data_dir = os.path.join(os.path.dirname(__file__),
                                     '..', '..', '..', 'unittest_data')
    self._browser.SetHTTPServerDirectories(unittest_data_dir)
    self._tab.Navigate(
      self._browser.http_server.UrlOf('blank.html'))
    self._tab.WaitForDocumentReadyStateToBeComplete()
    self.assertEquals(
        self._tab.EvaluateJavaScript('document.location.pathname;'),
        '/blank.html')

    i = wait.WaitAction({ 'condition': 'duration', 'seconds': 1 })

    start_time = time.time()
    i.RunAction(None, self._tab, None)
    self.assertAlmostEqual(time.time() - start_time, 1, places=1)
