#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import time
import unittest

import pyauto_functional  # Must be imported before pyauto
import pyauto
import pyauto_errors


class PyAutoTest(pyauto.PyUITest):
  """Test functionality of the PyAuto framework."""

  _EXTRA_CHROME_FLAGS = [
    '--scooby-doo=123',
    '--donald-duck=cool',
    '--super-mario',
    '--marvin-the-martian',
  ]

  def ExtraChromeFlags(self):
    """Ensures Chrome is launched with some custom flags.

    Overrides the default list of extra flags passed to Chrome.  See
    ExtraChromeFlags() in pyauto.py.
    """
    return pyauto.PyUITest.ExtraChromeFlags(self) + self._EXTRA_CHROME_FLAGS

  def testSetCustomChromeFlags(self):
    """Ensures that Chrome can be launched with custom flags."""
    self.NavigateToURL('about://version')
    for flag in self._EXTRA_CHROME_FLAGS:
      self.assertEqual(self.FindInPage(flag)['match_count'], 1,
                       msg='Missing expected Chrome flag "%s"' % flag)

  def testCallOnInvalidWindow(self):
    """Verify that exception is raised when a browser is missing/invalid."""
    self.assertEqual(1, self.GetBrowserWindowCount())
    self.assertRaises(
        pyauto_errors.JSONInterfaceError,
        lambda: self.FindInPage('some text', windex=1))  # invalid window

  def testJSONInterfaceTimeout(self):
    """Verify that an exception is raised when the JSON interface times out."""
    self.ClearEventQueue()
    self.AddDomEventObserver('foo')
    self.assertRaises(
        pyauto_errors.AutomationCommandTimeout,
        lambda: self.GetNextEvent(timeout=2000))  # event queue is empty

  def testActionTimeoutChanger(self):
    """Verify that ActionTimeoutChanger works."""
    new_timeout = 1000  # 1 sec
    changer = pyauto.PyUITest.ActionTimeoutChanger(self, new_timeout)
    self.assertEqual(self._automation_timeout, new_timeout)

    # Verify the amount of time taken for automation timeout
    then = time.time()
    self.assertRaises(
        pyauto_errors.AutomationCommandTimeout,
        lambda: self.ExecuteJavascript('invalid js should timeout'))
    elapsed = time.time() - then
    self.assertTrue(elapsed < new_timeout / 1000.0 + 2,  # margin of 2 secs
        msg='ActionTimeoutChanger did not work. '
            'Automation timeout took %f secs' % elapsed)


if __name__ == '__main__':
  pyauto_functional.Main()
