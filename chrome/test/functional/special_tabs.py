#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import logging
import os

import pyauto_functional  # Must be imported before pyauto
import pyauto
import test_utils

class SpecialTabsTest(pyauto.PyUITest):
  """TestCase for Special Tabs like about:version, chrome://history, etc."""

  @staticmethod
  def GetSpecialAcceleratorTabs():
    """Get a dict of accelerators and corresponding tab titles."""
    ret = {
        pyauto.IDC_SHOW_HISTORY: 'History',
        pyauto.IDC_MANAGE_EXTENSIONS: 'Extensions',
        pyauto.IDC_SHOW_DOWNLOADS: 'Downloads',
    }
    return ret

  special_url_redirects = {
   'about:': 'chrome://version',
   'about:about': 'chrome://about',
   'about:appcache-internals': 'chrome://appcache-internals',
   'about:credits': 'chrome://credits',
   'about:dns': 'chrome://dns',
   'about:histograms': 'chrome://histograms',
   'about:plugins': 'chrome://plugins',
   'about:sync': 'chrome://sync-internals',
   'about:sync-internals': 'chrome://sync-internals',
   'about:version': 'chrome://version',
  }

  special_url_tabs = {
    'chrome://about': { 'title': 'Chrome URLs' },
    'chrome://appcache-internals': { 'title': 'AppCache Internals' },
    'chrome://blob-internals': { 'title': 'Blob Storage Internals' },
    'chrome://feedback': {},
    'chrome://chrome-urls': { 'title': 'Chrome URLs' },
    'chrome://crashes': { 'title': 'Crashes' },
    'chrome://credits': { 'title': 'Credits' },
    'chrome://downloads': { 'title': 'Downloads' },
    'chrome://dns': { 'title': 'About DNS' },
    'chrome://extensions': { 'title': 'Extensions' },
    'chrome://flags': {},
    'chrome://flash': {},
    'chrome://gpu-internals': {},
    'chrome://histograms': { 'title': 'About Histograms' },
    'chrome://history': { 'title': 'History' },
    'chrome://inspect': { 'title': 'Inspect with Chrome Developer Tools' },
    'chrome://media-internals': { 'title': 'Media Internals' },
    'chrome://memory-redirect': { 'title': 'About Memory' },
    'chrome://net-internals': {},
    'chrome://net-internals/help.html': {},
    'chrome://newtab': { 'title': 'New Tab', 'CSP': False },
    'chrome://plugins': { 'title': 'Plug-ins' },
    'chrome://settings': { 'title': 'Settings' },
    'chrome://settings/autofill': { 'title': 'Settings - Autofill settings' },
    'chrome://settings/clearBrowserData':
      { 'title': 'Settings - Clear browsing data' },
    'chrome://settings/content': { 'title': 'Settings - Content settings' },
    'chrome://settings/languages':
      { 'title': 'Settings - Languages' },
    'chrome://settings/passwords': { 'title': 'Settings - Passwords' },
    'chrome://stats': {},
    'chrome://sync': { 'title': 'Sync Internals' },
    'chrome://sync-internals': { 'title': 'Sync Internals' },
    'chrome://terms': {},
    'chrome://version': { 'title': 'About Version' },
    'chrome://view-http-cache': {},
    'chrome://webrtc-internals': { 'title': 'WebRTC Internals' },
  }
  broken_special_url_tabs = {
    # crashed under debug when invoked from location bar (bug 88223).
    'chrome://devtools': { 'CSP': False },

    # returns "not available" despite having an URL constant.
    'chrome://dialog': { 'CSP': False },

    # separate window on mac, PC untested, not implemented elsewhere.
    'chrome://ipc': { 'CSP': False },

    # race against redirects via meta-refresh.
    'chrome://memory': { 'CSP': False },
  }

  chromeos_special_url_tabs = {
    'chrome://choose-mobile-network': { 'title': 'undefined', 'CSP': True },
    'chrome://flags': { 'CSP': True },
    'chrome://imageburner': { 'title':'Create a Recovery Media', 'CSP': True },
    'chrome://keyboardoverlay': { 'title': 'Keyboard Overlay', 'CSP': True },
    'chrome://network': { 'title': 'About Network' },
    'chrome://os-credits': { 'title': 'Credits', 'CSP': False },
    'chrome://proxy-settings': { 'CSP': False },
    'chrome://register': { 'CSP': False },
    'chrome://settings/languages':
      { 'title': 'Settings - Languages and input' },
    'chrome://sim-unlock': { 'title': 'Enter SIM card PIN', 'CSP': False },
    'chrome://system': { 'title': 'About System', 'CSP': False },

    # OVERRIDE - title and page different on CrOS
    'chrome://settings/accounts': { 'title': 'Settings - Users' },
  }
  broken_chromeos_special_url_tabs = {
    # returns "not available" page on chromeos=1 linux but has an URL constant.
    'chrome://activationmessage': { 'CSP': False },
    'chrome://cloudprintresources': { 'CSP': False },
    'chrome://cloudprintsetup': { 'CSP': False },
    'chrome://collected-cookies': { 'CSP': False },
    'chrome://constrained-test': { 'CSP': False },
    'chrome://enterprise-enrollment': { 'CSP': False },
    'chrome://http-auth': { 'CSP': False },
    'chrome://login-container': { 'CSP': False },
    'chrome://media-player': { 'CSP': False },
    'chrome://screenshots': { 'CSP': False },
    'chrome://slideshow': { 'CSP': False },
    'chrome://syncresources': { 'CSP': False },
    'chrome://theme': { 'CSP': False },
    'chrome://view-http-cache': { 'CSP': False },

    # crashes on chromeos=1 on linux, possibly missing real CrOS features.
    'chrome://cryptohome': { 'CSP': False},
    'chrome://mobilesetup': { 'CSP': False },
    'chrome://print': { 'CSP': False },
  }

  linux_special_url_tabs = {
    'chrome://linux-proxy-config': { 'title': 'Proxy Configuration Help' },
    'chrome://tcmalloc': { 'title': 'tcmalloc stats' },
    'chrome://sandbox': { 'title': 'Sandbox Status' },
  }
  broken_linux_special_url_tabs = {}

  mac_special_url_tabs = {
    'chrome://settings/languages': { 'title': 'Settings - Languages' },
  }
  broken_mac_special_url_tabs = {}

  win_special_url_tabs = {
    'chrome://conflicts': {},
  }
  broken_win_special_url_tabs = {
    # Sync on windows badly broken at the moment.
    'chrome://sync': {},
  }

  google_special_url_tabs = {
    # OVERRIDE - different title for Google Chrome vs. Chromium.
    'chrome://terms': {
      'title': 'Google Chrome Terms of Service',
    },
  }
  broken_google_special_url_tabs = {}

  google_chromeos_special_url_tabs = {
    # OVERRIDE - different title for Google Chrome OS vs. Chromium OS.
    'chrome://terms': {
      'title': 'Google Chrome OS Terms',
    },
  }
  broken_google_chromeos_special_url_tabs = {}

  google_win_special_url_tabs = {}
  broken_google_win_special_url_tabs = {}

  google_mac_special_url_tabs = {}
  broken_google_mac_special_url_tabs = {}

  google_linux_special_url_tabs = {}
  broken_google_linux_special_url_tabs = {}

  def _VerifyAppCacheInternals(self):
    """Confirm about:appcache-internals contains expected content for Caches.
       Also confirms that the about page populates Application Caches."""
    # Navigate to html page to activate DNS prefetching.
    self.NavigateToURL('http://futtta.be/html5/offline.php')
    # Wait for page to load and display sucess or fail message.
    self.WaitUntil(
        lambda: self.GetDOMValue('document.getElementById("status").innerHTML'),
                                 expect_retval='cached')
    self.TabGoBack()
    test_utils.StringContentCheck(
        self, self.GetTabContents(),
        ['Manifest',
         'http://futtta.be/html5/manifest.php'],
        [])

  def _VerifyAboutDNS(self):
    """Confirm about:dns contains expected content related to DNS info.
       Also confirms that prefetching DNS records propogate."""
    # Navigate to a page to activate DNS prefetching.
    self.NavigateToURL('http://www.google.com')
    self.TabGoBack()
    test_utils.StringContentCheck(self, self.GetTabContents(),
                                  ['Host name', 'How long ago', 'Motivation'],
                                  [])

  def _GetPlatformSpecialURLTabs(self):
    tabs = self.special_url_tabs.copy()
    broken_tabs = self.broken_special_url_tabs.copy()
    if self.IsChromeOS():
      tabs.update(self.chromeos_special_url_tabs)
      broken_tabs.update(self.broken_chromeos_special_url_tabs)
    elif self.IsLinux():
      tabs.update(self.linux_special_url_tabs)
      broken_tabs.update(self.broken_linux_special_url_tabs)
    elif self.IsMac():
      tabs.update(self.mac_special_url_tabs)
      broken_tabs.update(self.broken_mac_special_url_tabs)
    elif self.IsWin():
      tabs.update(self.win_special_url_tabs)
      broken_tabs.update(self.broken_win_special_url_tabs)
    for key, value in broken_tabs.iteritems():
      if key in tabs:
       del tabs[key]
    broken_tabs = {}
    if self.GetBrowserInfo()['properties']['branding'] == 'Google Chrome':
      tabs.update(self.google_special_url_tabs)
      broken_tabs.update(self.broken_google_special_url_tabs)
      if self.IsChromeOS():
        tabs.update(self.google_chromeos_special_url_tabs)
        broken_tabs.update(self.broken_google_chromeos_special_url_tabs)
      elif self.IsLinux():
        tabs.update(self.google_linux_special_url_tabs)
        broken_tabs.update(self.broken_google_linux_special_url_tabs)
      elif self.IsMac():
        tabs.update(self.google_mac_special_url_tabs)
        broken_tabs.update(self.broken_google_mac_special_url_tabs)
      elif self.IsWin():
        tabs.update(self.google_win_special_url_tabs)
        broken_tabs.update(self.broken_google_win_special_url_tabs)
      for key, value in broken_tabs.iteritems():
        if key in tabs:
         del tabs[key]
    return tabs

  def testSpecialURLRedirects(self):
    """Test that older about: URLs are implemented by newer chrome:// URLs.
       The location bar may not get updated in all cases, so checking the
       tab URL is misleading, instead check for the same contents as the
       chrome:// page."""
    tabs = self._GetPlatformSpecialURLTabs()
    for url, redirect in self.special_url_redirects.iteritems():
      if redirect in tabs:
        logging.debug('Testing redirect from %s to %s.' % (url, redirect))
        self.NavigateToURL(url)
        self.assertEqual(self.special_url_tabs[redirect]['title'],
                         self.GetActiveTabTitle())

  def testSpecialURLTabs(self):
    """Test special tabs created by URLs like chrome://downloads,
       chrome://settings/extensionSettings, chrome://history etc.
       Also ensures they specify content-security-policy and not inline
       scripts for those pages that are expected to do so.  Patches which
       break this test by including new inline javascript are security
       vulnerabilities and should be reverted."""
    tabs = self._GetPlatformSpecialURLTabs()
    for url, properties in tabs.iteritems():
      logging.debug('Testing URL %s.' % url)
      self.NavigateToURL(url)
      expected_title = 'title' in properties and properties['title'] or url
      actual_title = self.GetActiveTabTitle()
      self.assertTrue(self.WaitUntil(
          lambda: self.GetActiveTabTitle(), expect_retval=expected_title),
          msg='Title did not match for %s. Expected: %s. Got %s' % (
              url, expected_title, self.GetActiveTabTitle()))
      include_list = []
      exclude_list = []
      no_csp = 'CSP' in properties and not properties['CSP']
      if no_csp:
        exclude_list.extend(['Content-Security-Policy'])
      else:
        exclude_list.extend(['<script>', 'onclick=', 'onload=',
                             'onchange=', 'onsubmit=', 'javascript:'])
      if 'includes' in properties:
        include_list.extend(properties['includes'])
      if 'excludes' in properties:
        exclude_list.extend(properties['exlcudes'])
      test_utils.StringContentCheck(self, self.GetTabContents(),
                                    include_list, exclude_list)
      result = self.ExecuteJavascript("""
          var r = 'blocked';
          var f = 'executed';
          var s = document.createElement('script');
          s.textContent = 'r = f';
          document.body.appendChild(s);
          window.domAutomationController.send(r);
        """)
      logging.debug('has csp %s, result %s.' % (not no_csp, result))
      if no_csp:
        self.assertEqual(result, 'executed',
                         msg='Got %s for %s' % (result, url))
      else:
        self.assertEqual(result, 'blocked',
                         msg='Got %s for %s' % (result, url))

      # Restart browser so that every URL gets a fresh instance.
      self.RestartBrowser(clear_profile=True)

  def testAboutAppCacheTab(self):
    """Test App Cache tab to confirm about page populates caches."""
    self.NavigateToURL('about:appcache-internals')
    self._VerifyAppCacheInternals()
    self.assertEqual('AppCache Internals', self.GetActiveTabTitle())

  def testAboutDNSTab(self):
    """Test DNS tab to confirm DNS about page propogates records."""
    self.NavigateToURL('about:dns')
    self._VerifyAboutDNS()
    self.assertEqual('About DNS', self.GetActiveTabTitle())

  def testSpecialAcceratorTabs(self):
    """Test special tabs created by accelerators."""
    for accel, title in self.GetSpecialAcceleratorTabs().iteritems():
      self.RunCommand(accel)
      self.assertTrue(self.WaitUntil(
            self.GetActiveTabTitle, expect_retval=title),
          msg='Expected "%s", got "%s"' % (title, self.GetActiveTabTitle()))


if __name__ == '__main__':
  pyauto_functional.Main()
