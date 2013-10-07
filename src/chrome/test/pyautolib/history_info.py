# Copyright (c) 2011 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""History: python representation for history.

Obtain one of these from PyUITestSuite::GetHistoryInfo() call.

Example:
class MyTest(pyauto.PyUITest):
  def testBasic(self):
    url = 'http://www.google.com/'
    self.NavigateToURL(url)
    history = self.GetHistoryInfo()
    self.assertEqual(1, len(history))
    self.assertEqual(url, history[0]['url'])

See more tests in chrome/test/functional/history.py.
"""

import simplejson as json

from pyauto_errors import JSONInterfaceError


class HistoryInfo(object):
  """Represent info about browsing history.

  The info is represented as a list of history items containing url, title,
  time, etc.
  """
  def __init__(self, history_dict):
    """Initialize a HistoryInfo from a string of json.

    Args:
      json_string: a dictionary as returned by the IPC command 'GetHistoryInfo'.
                   A typical dict representing history info looks like:
                   {'history': [
                      {'url': 'http://www.google.com/',
                       'title': 'Google',
                       ...,
                       ...,
                    }, ] }

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    # JSON string prepared in GetHistoryInfo() in automation_provider.cc
    self.historydict = history_dict

  def History(self):
    """Get history list.

    History is ordered latest first, that is in the same order as
    chrome://history/ would list.

    Example:
    [ { u'snippet': u'',
        u'starred': False,
        u'time': 1271781612,
        u'title': u'Google News',
        u'url': u'http://news.google.com/'},
      { u'snippet': u'',
        u'starred': True,
        u'time': 1271781602,
        u'title': u'Google',
        u'url': u'http://www.google.com/'}]

    The snippet attribute will be empty in most cases. If GetHistoryInfo() is
    provided a non-empty search_text arg, the snippet attribute will contain the
    snippet as it would be visible when searching for that text in the
    chrome://history/ UI.

    Returns:
      [item1, item2, ...]
    """
    return self.historydict.get('history', [])
