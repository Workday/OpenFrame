#!/usr/bin/env python
# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""PyAuto: Python Interface to Chromium's Automation Proxy.

PyAuto uses swig to expose Automation Proxy interfaces to Python.
For complete documentation on the functionality available,
run pydoc on this file.

Ref: http://dev.chromium.org/developers/testing/pyauto


Include the following in your PyAuto test script to make it run standalone.

from pyauto import Main

if __name__ == '__main__':
  Main()

This script can be used as an executable to fire off other scripts, similar
to unittest.py
  python pyauto.py test_script
"""

import cStringIO
import copy
import functools
import hashlib
import inspect
import logging
import optparse
import os
import pickle
import pprint
import re
import shutil
import signal
import socket
import stat
import string
import subprocess
import sys
import tempfile
import time
import types
import unittest
import urllib

import pyauto_paths


def _LocateBinDirs():
  """Setup a few dirs where we expect to find dependency libraries."""
  deps_dirs = [
      os.path.dirname(__file__),
      pyauto_paths.GetThirdPartyDir(),
      os.path.join(pyauto_paths.GetThirdPartyDir(), 'webdriver', 'pylib'),
  ]
  sys.path += map(os.path.normpath, pyauto_paths.GetBuildDirs() + deps_dirs)

_LocateBinDirs()

_PYAUTO_DOC_URL = 'http://dev.chromium.org/developers/testing/pyauto'

try:
  import pyautolib
  # Needed so that all additional classes (like: FilePath, GURL) exposed by
  # swig interface get available in this module.
  from pyautolib import *
except ImportError:
  print >>sys.stderr, 'Could not locate pyautolib shared libraries.  ' \
                      'Did you build?\n  Documentation: %s' % _PYAUTO_DOC_URL
  # Mac requires python2.5 even when not the default 'python' (e.g. 10.6)
  if 'darwin' == sys.platform and sys.version_info[:2] != (2,5):
    print  >>sys.stderr, '*\n* Perhaps use "python2.5", not "python" ?\n*'
  raise

# Should go after sys.path is set appropriately
import bookmark_model
import download_info
import history_info
import omnibox_info
import plugins_info
import prefs_info
from pyauto_errors import AutomationCommandFail
from pyauto_errors import AutomationCommandTimeout
from pyauto_errors import JavascriptRuntimeError
from pyauto_errors import JSONInterfaceError
from pyauto_errors import NTPThumbnailNotShownError
import pyauto_utils
import simplejson as json  # found in third_party

_CHROME_DRIVER_FACTORY = None
_DEFAULT_AUTOMATION_TIMEOUT = 45
_HTTP_SERVER = None
_REMOTE_PROXY = None
_OPTIONS = None
_BROWSER_PID = None

class PyUITest(pyautolib.PyUITestBase, unittest.TestCase):
  """Base class for UI Test Cases in Python.

  A browser is created before executing each test, and is destroyed after
  each test irrespective of whether the test passed or failed.

  You should derive from this class and create methods with 'test' prefix,
  and use methods inherited from PyUITestBase (the C++ side).

  Example:

    class MyTest(PyUITest):

      def testNavigation(self):
        self.NavigateToURL("http://www.google.com")
        self.assertEqual("Google", self.GetActiveTabTitle())
  """

  def __init__(self, methodName='runTest', **kwargs):
    """Initialize PyUITest.

    When redefining __init__ in a derived class, make sure that:
      o you make a call this __init__
      o __init__ takes methodName as an arg. this is mandated by unittest module

    Args:
      methodName: the default method name. Internal use by unittest module

      (The rest of the args can be in any order. They can even be skipped in
       which case the defaults will be used.)

      clear_profile: If True, clean the profile dir before use. Defaults to True
      homepage: the home page. Defaults to "about:blank"
    """
    # Fetch provided keyword args, or fill in defaults.
    clear_profile = kwargs.get('clear_profile', True)
    homepage = kwargs.get('homepage', 'about:blank')
    self._automation_timeout = _DEFAULT_AUTOMATION_TIMEOUT * 1000

    pyautolib.PyUITestBase.__init__(self, clear_profile, homepage)
    self.Initialize(pyautolib.FilePath(self.BrowserPath()))
    unittest.TestCase.__init__(self, methodName)

    # Give all pyauto tests easy access to pprint.PrettyPrinter functions.
    self.pprint = pprint.pprint
    self.pformat = pprint.pformat

    # Set up remote proxies, if they were requested.
    self.remotes = []
    self.remote = None
    global _REMOTE_PROXY
    if _REMOTE_PROXY:
      self.remotes = _REMOTE_PROXY
      self.remote = _REMOTE_PROXY[0]

  def __del__(self):
    pyautolib.PyUITestBase.__del__(self)

  def _SetExtraChromeFlags(self):
    """Prepares the browser to launch with the specified extra Chrome flags.

    This function is called right before the browser is launched for the first
    time.
    """
    for flag in self.ExtraChromeFlags():
      if flag.startswith('--'):
        flag = flag[2:]
      split_pos = flag.find('=')
      if split_pos >= 0:
        flag_name = flag[:split_pos]
        flag_val = flag[split_pos + 1:]
        self.AppendBrowserLaunchSwitch(flag_name, flag_val)
      else:
        self.AppendBrowserLaunchSwitch(flag)

  def __SetUp(self):
    named_channel_id = None
    if _OPTIONS:
      named_channel_id = _OPTIONS.channel_id
    if self.IsChromeOS():  # Enable testing interface on ChromeOS.
      if self.get_clear_profile():
        self.CleanupBrowserProfileOnChromeOS()
      self.EnableCrashReportingOnChromeOS()
      if not named_channel_id:
        named_channel_id = self.EnableChromeTestingOnChromeOS()
    else:
      self._SetExtraChromeFlags()  # Flags already previously set for ChromeOS.
    if named_channel_id:
      self._named_channel_id = named_channel_id
      self.UseNamedChannelID(named_channel_id)
    # Initialize automation and fire the browser (does not fire the browser
    # on ChromeOS).
    self.SetUp()

    global _BROWSER_PID
    try:
      _BROWSER_PID = self.GetBrowserInfo()['browser_pid']
    except JSONInterfaceError:
      raise JSONInterfaceError('Unable to get browser_pid over automation '
                               'channel on first attempt.  Something went very '
                               'wrong.  Chrome probably did not launch.')

    # Forcibly trigger all plugins to get registered.  crbug.com/94123
    # Sometimes flash files loaded too quickly after firing browser
    # ends up getting downloaded, which seems to indicate that the plugin
    # hasn't been registered yet.
    if not self.IsChromeOS():
      self.GetPluginsInfo()

    # TODO(dtu): Remove this after crosbug.com/4558 is fixed.
    if self.IsChromeOS():
      self.WaitUntil(lambda: not self.GetNetworkInfo()['offline_mode'])

    if (self.IsChromeOS() and not self.GetLoginInfo()['is_logged_in'] and
        self.ShouldOOBESkipToLogin()):
      if self.GetOOBEScreenInfo()['screen_name'] != 'login':
        self.SkipToLogin()
      if self.ShouldAutoLogin():
        # Login with default creds.
        sys.path.append('/usr/local')  # to import autotest libs
        from autotest.cros import constants
        creds = constants.CREDENTIALS['$default']
        self.Login(creds[0], creds[1])
        assert self.GetLoginInfo()['is_logged_in']
        logging.info('Logged in as %s.' % creds[0])

    # If we are connected to any RemoteHosts, create PyAuto
    # instances on the remote sides and set them up too.
    for remote in self.remotes:
      remote.CreateTarget(self)
      remote.setUp()

  def setUp(self):
    """Override this method to launch browser differently.

    Can be used to prevent launching the browser window by default in case a
    test wants to do some additional setup before firing browser.

    When using the named interface, it connects to an existing browser
    instance.

    On ChromeOS, a browser showing the login window is started. Tests can
    initiate a user session by calling Login() or LoginAsGuest(). Cryptohome
    vaults or flimflam profiles left over by previous tests can be cleared by
    calling RemoveAllCryptohomeVaults() respectively CleanFlimflamDirs() before
    logging in to improve isolation. Note that clearing flimflam profiles
    requires a flimflam restart, briefly taking down network connectivity and
    slowing down the test. This should be done for tests that use flimflam only.
    """
    self.__SetUp()

  def tearDown(self):
    for remote in self.remotes:
      remote.tearDown()

    self.TearDown()  # Destroy browser

  # Method required by the Python standard library unittest.TestCase.
  def runTest(self):
    pass

  @staticmethod
  def BrowserPath():
    """Returns the path to Chromium binaries.

    Expects the browser binaries to be in the
    same location as the pyautolib binaries.
    """
    return os.path.normpath(os.path.dirname(pyautolib.__file__))

  def ExtraChromeFlags(self):
    """Return a list of extra chrome flags to use with Chrome for testing.

    These are flags needed to facilitate testing.  Override this function to
    use a custom set of Chrome flags.
    """
    auth_ext_path = ('/usr/local/autotest/deps/pyauto_dep/' +
        'test_src/chrome/browser/resources/gaia_auth')
    if self.IsChromeOS():
      return [
        '--homepage=about:blank',
        '--allow-file-access',
        '--allow-file-access-from-files',
        '--enable-file-cookies',
        '--disable-default-apps',
        '--dom-automation',
        '--skip-oauth-login',
        # Enables injection of test content script for webui login automation
        '--auth-ext-path=%s' % auth_ext_path,
        # Enable automation provider, chromeos net and chromeos login logs
        '--vmodule=*/browser/automation/*=2,*/chromeos/net/*=2,' +
            '*/chromeos/login/*=2',
      ]
    else:
      return []

  def ShouldOOBESkipToLogin(self):
    """Determine if we should skip the OOBE flow on ChromeOS.

    This makes automation skip the OOBE flow during setUp() and land directly
    to the login screen. Applies only if not logged in already.

    Override and return False if OOBE flow is required, for OOBE tests, for
    example. Calling this function directly will have no effect.

    Returns:
      True, if the OOBE should be skipped and automation should
            go to the 'Add user' login screen directly
      False, if the OOBE should not be skipped.
    """
    assert self.IsChromeOS()
    return True

  def ShouldAutoLogin(self):
    """Determine if we should auto-login on ChromeOS at browser startup.

    To be used for tests that expect user to be logged in before running test,
    without caring which user. ShouldOOBESkipToLogin() should return True
    for this to take effect.

    Override and return False to not auto login, for tests where login is part
    of the use case.

    Returns:
      True, if chrome should auto login after startup.
      False, otherwise.
    """
    assert self.IsChromeOS()
    return True

  def CloseChromeOnChromeOS(self):
    """Gracefully exit chrome on ChromeOS."""

    def _GetListOfChromePids():
      """Retrieves the list of currently-running Chrome process IDs.

      Returns:
        A list of strings, where each string represents a currently-running
        'chrome' process ID.
      """
      proc = subprocess.Popen(['pgrep', '^chrome$'], stdout=subprocess.PIPE)
      proc.wait()
      return [x.strip() for x in proc.stdout.readlines()]

    orig_pids = _GetListOfChromePids()
    subprocess.call(['pkill', '^chrome$'])

    def _AreOrigPidsDead(orig_pids):
      """Determines whether all originally-running 'chrome' processes are dead.

      Args:
        orig_pids: A list of strings, where each string represents the PID for
                   an originally-running 'chrome' process.

      Returns:
        True, if all originally-running 'chrome' processes have been killed, or
        False otherwise.
      """
      for new_pid in _GetListOfChromePids():
        if new_pid in orig_pids:
          return False
      return True

    self.WaitUntil(lambda: _AreOrigPidsDead(orig_pids))

  @staticmethod
  def _IsRootSuid(path):
    """Determine if |path| is a suid-root file."""
    return os.path.isfile(path) and (os.stat(path).st_mode & stat.S_ISUID)

  @staticmethod
  def SuidPythonPath():
    """Path to suid_python binary on ChromeOS.

    This is typically in the same directory as pyautolib.py
    """
    return os.path.join(PyUITest.BrowserPath(), 'suid-python')

  @staticmethod
  def RunSuperuserActionOnChromeOS(action):
    """Run the given action with superuser privs (on ChromeOS).

    Uses the suid_actions.py script.

    Args:
      action: An action to perform.
              See suid_actions.py for available options.

    Returns:
      (stdout, stderr)
    """
    assert PyUITest._IsRootSuid(PyUITest.SuidPythonPath()), \
        'Did not find suid-root python at %s' % PyUITest.SuidPythonPath()
    file_path = os.path.join(os.path.dirname(__file__), 'chromeos',
                             'suid_actions.py')
    args = [PyUITest.SuidPythonPath(), file_path, '--action=%s' % action]
    proc = subprocess.Popen(
        args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout, stderr = proc.communicate()
    return (stdout, stderr)

  def EnableChromeTestingOnChromeOS(self):
    """Enables the named automation interface on chromeos.

    Restarts chrome so that you get a fresh instance.
    Also sets some testing-friendly flags for chrome.

    Expects suid python to be present in the same dir as pyautolib.py
    """
    assert PyUITest._IsRootSuid(self.SuidPythonPath()), \
        'Did not find suid-root python at %s' % self.SuidPythonPath()
    file_path = os.path.join(os.path.dirname(__file__), 'chromeos',
                             'enable_testing.py')
    args = [self.SuidPythonPath(), file_path]
    # Pass extra chrome flags for testing
    for flag in self.ExtraChromeFlags():
      args.append('--extra-chrome-flags=%s' % flag)
    assert self.WaitUntil(lambda: self._IsSessionManagerReady(0))
    proc = subprocess.Popen(args, stdout=subprocess.PIPE)
    automation_channel_path = proc.communicate()[0].strip()
    assert len(automation_channel_path), 'Could not enable testing interface'
    return automation_channel_path

  @staticmethod
  def EnableCrashReportingOnChromeOS():
    """Enables crash reporting on ChromeOS.

    Writes the "/home/chronos/Consent To Send Stats" file with a 32-char
    readable string.  See comment in session_manager_setup.sh which does this
    too.

    Note that crash reporting will work only if breakpad is built in, ie in a
    'Google Chrome' build (not Chromium).
    """
    consent_file = '/home/chronos/Consent To Send Stats'
    def _HasValidConsentFile():
      if not os.path.isfile(consent_file):
        return False
      stat = os.stat(consent_file)
      return (len(open(consent_file).read()) and
              (1000, 1000) == (stat.st_uid, stat.st_gid))
    if not _HasValidConsentFile():
      client_id = hashlib.md5('abcdefgh').hexdigest()
      # Consent file creation and chown to chronos needs to be atomic
      # to avoid races with the session_manager.  crosbug.com/18413
      # Therefore, create a temp file, chown, then rename it as consent file.
      temp_file = consent_file + '.tmp'
      open(temp_file, 'w').write(client_id)
      # This file must be owned by chronos:chronos!
      os.chown(temp_file, 1000, 1000);
      shutil.move(temp_file, consent_file)
    assert _HasValidConsentFile(), 'Could not create %s' % consent_file

  @staticmethod
  def _IsSessionManagerReady(old_pid):
    """Is the ChromeOS session_manager running and ready to accept DBus calls?

    Called after session_manager is killed to know when it has restarted.

    Args:
      old_pid: The pid that session_manager had before it was killed,
               to ensure that we don't look at the DBus interface
               of an old session_manager process.
    """
    pgrep_process = subprocess.Popen(['pgrep', 'session_manager'],
                                     stdout=subprocess.PIPE)
    new_pid = pgrep_process.communicate()[0].strip()
    if not new_pid or old_pid == new_pid:
      return False

    import dbus
    try:
      bus = dbus.SystemBus()
      proxy = bus.get_object('org.chromium.SessionManager',
                             '/org/chromium/SessionManager')
      dbus.Interface(proxy, 'org.chromium.SessionManagerInterface')
    except dbus.DBusException:
      return False
    return True

  @staticmethod
  def CleanupBrowserProfileOnChromeOS():
    """Cleanup browser profile dir on ChromeOS.

    This does not clear cryptohome.

    Browser should not be running, or else there will be locked files.
    """
    profile_dir = '/home/chronos/user'
    for item in os.listdir(profile_dir):
      # Deleting .pki causes stateful partition to get erased.
      if item not in ['log', 'flimflam'] and not item.startswith('.'):
         pyauto_utils.RemovePath(os.path.join(profile_dir, item))

    chronos_dir = '/home/chronos'
    for item in os.listdir(chronos_dir):
      if item != 'user' and not item.startswith('.'):
        pyauto_utils.RemovePath(os.path.join(chronos_dir, item))

  @staticmethod
  def CleanupFlimflamDirsOnChromeOS():
    """Clean the contents of flimflam profiles and restart flimflam."""
    PyUITest.RunSuperuserActionOnChromeOS('CleanFlimflamDirs')

  @staticmethod
  def RemoveAllCryptohomeVaultsOnChromeOS():
    """Remove any existing cryptohome vaults."""
    PyUITest.RunSuperuserActionOnChromeOS('RemoveAllCryptohomeVaults')

  @staticmethod
  def _IsInodeNew(path, old_inode):
    """Determine whether an inode has changed. POSIX only.

    Args:
      path: The file path to check for changes.
      old_inode: The old inode number.

    Returns:
      True if the path exists and its inode number is different from old_inode.
      False otherwise.
    """
    try:
      stat_result = os.stat(path)
    except OSError:
      return False
    if not stat_result:
      return False
    return stat_result.st_ino != old_inode

  def RestartBrowser(self, clear_profile=True, pre_launch_hook=None):
    """Restart the browser.

    For use with tests that require to restart the browser.

    Args:
      clear_profile: If True, the browser profile is cleared before restart.
                     Defaults to True, that is restarts browser with a clean
                     profile.
      pre_launch_hook: If specified, must be a callable that is invoked before
                       the browser is started again. Not supported in ChromeOS.
    """
    if self.IsChromeOS():
      assert pre_launch_hook is None, 'Not supported in ChromeOS'
      self.TearDown()
      if clear_profile:
        self.CleanupBrowserProfileOnChromeOS()
      self.CloseChromeOnChromeOS()
      self.EnableChromeTestingOnChromeOS()
      self.SetUp()
      return
    # Not chromeos
    orig_clear_state = self.get_clear_profile()
    self.CloseBrowserAndServer()
    self.set_clear_profile(clear_profile)
    if pre_launch_hook:
      pre_launch_hook()
    logging.debug('Restarting browser with clear_profile=%s',
                  self.get_clear_profile())
    self.LaunchBrowserAndServer()
    self.set_clear_profile(orig_clear_state)  # Reset to original state.

  @staticmethod
  def DataDir():
    """Returns the path to the data dir chrome/test/data."""
    return os.path.normpath(
        os.path.join(os.path.dirname(__file__), os.pardir, "data"))

  @staticmethod
  def ChromeOSDataDir():
    """Returns the path to the data dir chromeos/test/data."""
    return os.path.normpath(
        os.path.join(os.path.dirname(__file__), os.pardir, os.pardir, os.pardir,
                     "chromeos", "test", "data"))

  @staticmethod
  def GetFileURLForPath(*path):
    """Get file:// url for the given path.

    Also quotes the url using urllib.quote().

    Args:
      path: Variable number of strings that can be joined.
    """
    path_str = os.path.join(*path)
    abs_path = os.path.abspath(path_str)
    if sys.platform == 'win32':
      # Don't quote the ':' in drive letter ( say, C: ) on win.
      # Also, replace '\' with '/' as expected in a file:/// url.
      drive, rest = os.path.splitdrive(abs_path)
      quoted_path = drive.upper() + urllib.quote((rest.replace('\\', '/')))
      return 'file:///' + quoted_path
    else:
      quoted_path = urllib.quote(abs_path)
      return 'file://' + quoted_path

  @staticmethod
  def GetFileURLForDataPath(*relative_path):
    """Get file:// url for the given path relative to the chrome test data dir.

    Also quotes the url using urllib.quote().

    Args:
      relative_path: Variable number of strings that can be joined.
    """
    return PyUITest.GetFileURLForPath(PyUITest.DataDir(), *relative_path)

  @staticmethod
  def GetHttpURLForDataPath(*relative_path):
    """Get http:// url for the given path in the data dir.

    The URL will be usable only after starting the http server.
    """
    global _HTTP_SERVER
    assert _HTTP_SERVER, 'HTTP Server not yet started'
    return _HTTP_SERVER.GetURL(os.path.join('files', *relative_path)).spec()

  @staticmethod
  def ContentDataDir():
    """Get path to content/test/data."""
    return os.path.join(PyUITest.DataDir(), os.pardir, os.pardir, os.pardir,
        'content', 'test', 'data')

  @staticmethod
  def GetFileURLForContentDataPath(*relative_path):
    """Get file:// url for the given path relative to content test data dir.

    Also quotes the url using urllib.quote().

    Args:
      relative_path: Variable number of strings that can be joined.
    """
    return PyUITest.GetFileURLForPath(PyUITest.ContentDataDir(), *relative_path)

  @staticmethod
  def GetFtpURLForDataPath(ftp_server, *relative_path):
    """Get ftp:// url for the given path in the data dir.

    Args:
      ftp_server: handle to ftp server, an instance of SpawnedTestServer
      relative_path: any number of path elements

    The URL will be usable only after starting the ftp server.
    """
    assert ftp_server, 'FTP Server not yet started'
    return ftp_server.GetURL(os.path.join(*relative_path)).spec()

  @staticmethod
  def IsMac():
    """Are we on Mac?"""
    return 'darwin' == sys.platform

  @staticmethod
  def IsLinux():
    """Are we on Linux? ChromeOS is linux too."""
    return sys.platform.startswith('linux')

  @staticmethod
  def IsWin():
    """Are we on Win?"""
    return 'win32' == sys.platform

  @staticmethod
  def IsWin7():
    """Are we on Windows 7?"""
    if not PyUITest.IsWin():
      return False
    ver = sys.getwindowsversion()
    return (ver[3], ver[0], ver[1]) == (2, 6, 1)

  @staticmethod
  def IsWinVista():
    """Are we on Windows Vista?"""
    if not PyUITest.IsWin():
      return False
    ver = sys.getwindowsversion()
    return (ver[3], ver[0], ver[1]) == (2, 6, 0)

  @staticmethod
  def IsWinXP():
    """Are we on Windows XP?"""
    if not PyUITest.IsWin():
      return False
    ver = sys.getwindowsversion()
    return (ver[3], ver[0], ver[1]) == (2, 5, 1)

  @staticmethod
  def IsChromeOS():
    """Are we on ChromeOS (or Chromium OS)?

    Checks for "CHROMEOS_RELEASE_NAME=" in /etc/lsb-release.
    """
    lsb_release = '/etc/lsb-release'
    if not PyUITest.IsLinux() or not os.path.isfile(lsb_release):
      return False
    for line in open(lsb_release).readlines():
      if line.startswith('CHROMEOS_RELEASE_NAME='):
        return True
    return False

  @staticmethod
  def IsPosix():
    """Are we on Mac/Linux?"""
    return PyUITest.IsMac() or PyUITest.IsLinux()

  @staticmethod
  def IsEnUS():
    """Are we en-US?"""
    # TODO: figure out the machine's langugage.
    return True

  @staticmethod
  def GetPlatform():
    """Return the platform name."""
    # Since ChromeOS is also Linux, we check for it first.
    if PyUITest.IsChromeOS():
      return 'chromeos'
    elif PyUITest.IsLinux():
      return 'linux'
    elif PyUITest.IsMac():
      return 'mac'
    elif PyUITest.IsWin():
      return 'win'
    else:
      return 'unknown'

  @staticmethod
  def EvalDataFrom(filename):
    """Return eval of python code from given file.

    The datastructure used in the file will be preserved.
    """
    data_file = os.path.join(filename)
    contents = open(data_file).read()
    try:
      ret = eval(contents)
    except:
      print >>sys.stderr, '%s is an invalid data file.' % data_file
      raise
    return ret

  @staticmethod
  def ChromeOSBoard():
    """What is the ChromeOS board name"""
    if PyUITest.IsChromeOS():
      for line in open('/etc/lsb-release'):
        line = line.strip()
        if line.startswith('CHROMEOS_RELEASE_BOARD='):
          return line.split('=')[1]
    return None

  @staticmethod
  def Kill(pid):
    """Terminate the given pid.

    If the pid refers to a renderer, use KillRendererProcess instead.
    """
    if PyUITest.IsWin():
      subprocess.call(['taskkill.exe', '/T', '/F', '/PID', str(pid)])
    else:
      os.kill(pid, signal.SIGTERM)

  @staticmethod
  def GetPrivateInfo():
    """Fetch info from private_tests_info.txt in private dir.

    Returns:
      a dictionary of items from private_tests_info.txt
    """
    private_file = os.path.join(
        PyUITest.DataDir(), 'pyauto_private', 'private_tests_info.txt')
    assert os.path.exists(private_file), '%s missing' % private_file
    return PyUITest.EvalDataFrom(private_file)

  def WaitUntil(self, function, timeout=-1, retry_sleep=0.25, args=[],
                expect_retval=None, return_retval=False, debug=True):
    """Poll on a condition until timeout.

    Waits until the |function| evalues to |expect_retval| or until |timeout|
    secs, whichever occurs earlier.

    This is better than using a sleep, since it waits (almost) only as much
    as needed.

    WARNING: This method call should be avoided as far as possible in favor
    of a real wait from chromium (like wait-until-page-loaded).
    Only use in case there's really no better option.

    EXAMPLES:-
    Wait for "file.txt" to get created:
      WaitUntil(os.path.exists, args=["file.txt"])

    Same as above, but using lambda:
      WaitUntil(lambda: os.path.exists("file.txt"))

    Args:
      function: the function whose truth value is to be evaluated
      timeout: the max timeout (in secs) for which to wait. The default
               action is to wait for kWaitForActionMaxMsec, as set in
               ui_test.cc
               Use None to wait indefinitely.
      retry_sleep: the sleep interval (in secs) before retrying |function|.
                   Defaults to 0.25 secs.
      args: the args to pass to |function|
      expect_retval: the expected return value for |function|. This forms the
                     exit criteria. In case this is None (the default),
                     |function|'s return value is checked for truth,
                     so 'non-empty-string' should match with True
      return_retval: If True, return the value returned by the last call to
                     |function()|
      debug: if True, displays debug info at each retry.

    Returns:
      The return value of the |function| (when return_retval == True)
      True, if returning when |function| evaluated to True (when
          return_retval == False)
      False, when returning due to timeout
    """
    if timeout == -1:  # Default
      timeout = self._automation_timeout / 1000.0
    assert callable(function), "function should be a callable"
    begin = time.time()
    debug_begin = begin
    retval = None
    while timeout is None or time.time() - begin <= timeout:
      retval = function(*args)
      if (expect_retval is None and retval) or \
         (expect_retval is not None and expect_retval == retval):
        return retval if return_retval else True
      if debug and time.time() - debug_begin > 5:
        debug_begin += 5
        if function.func_name == (lambda: True).func_name:
          function_info = inspect.getsource(function).strip()
        else:
          function_info = '%s()' % function.func_name
        logging.debug('WaitUntil(%s:%d %s) still waiting. '
                      'Expecting %s. Last returned %s.',
                      os.path.basename(inspect.getsourcefile(function)),
                      inspect.getsourcelines(function)[1],
                      function_info,
                      True if expect_retval is None else expect_retval,
                      retval)
      time.sleep(retry_sleep)
    return retval if return_retval else False

  def StartFTPServer(self, data_dir):
    """Start a local file server hosting data files over ftp://

    Args:
      data_dir: path where ftp files should be served

    Returns:
      handle to FTP Server, an instance of SpawnedTestServer
    """
    ftp_server = pyautolib.SpawnedTestServer(
        pyautolib.SpawnedTestServer.TYPE_FTP,
        '127.0.0.1',
        pyautolib.FilePath(data_dir))
    assert ftp_server.Start(), 'Could not start ftp server'
    logging.debug('Started ftp server at "%s".', data_dir)
    return ftp_server

  def StopFTPServer(self, ftp_server):
    """Stop the local ftp server."""
    assert ftp_server, 'FTP Server not yet started'
    assert ftp_server.Stop(), 'Could not stop ftp server'
    logging.debug('Stopped ftp server.')

  def StartHTTPServer(self, data_dir):
    """Starts a local HTTP SpawnedTestServer serving files from |data_dir|.

    Args:
      data_dir: path where the SpawnedTestServer should serve files from.
      This will be appended to the source dir to get the final document root.

    Returns:
      handle to the HTTP SpawnedTestServer
    """
    http_server = pyautolib.SpawnedTestServer(
        pyautolib.SpawnedTestServer.TYPE_HTTP,
        '127.0.0.1',
        pyautolib.FilePath(data_dir))
    assert http_server.Start(), 'Could not start HTTP server'
    logging.debug('Started HTTP server at "%s".', data_dir)
    return http_server

  def StopHTTPServer(self, http_server):
    assert http_server, 'HTTP server not yet started'
    assert http_server.Stop(), 'Cloud not stop the HTTP server'
    logging.debug('Stopped HTTP server.')

  def StartHttpsServer(self, cert_type, data_dir):
    """Starts a local HTTPS SpawnedTestServer serving files from |data_dir|.

    Args:
      cert_type: An instance of SSLOptions.ServerCertificate for three
                 certificate types: ok, expired, or mismatch.
      data_dir: The path where SpawnedTestServer should serve files from.
                This is appended to the source dir to get the final
                document root.

    Returns:
      Handle to the HTTPS SpawnedTestServer
    """
    https_server = pyautolib.SpawnedTestServer(
        pyautolib.SpawnedTestServer.TYPE_HTTPS,
        pyautolib.SSLOptions(cert_type),
        pyautolib.FilePath(data_dir))
    assert https_server.Start(), 'Could not start HTTPS server.'
    logging.debug('Start HTTPS server at "%s".' % data_dir)
    return https_server

  def StopHttpsServer(self, https_server):
    assert https_server, 'HTTPS server not yet started.'
    assert https_server.Stop(), 'Could not stop the HTTPS server.'
    logging.debug('Stopped HTTPS server.')

  class ActionTimeoutChanger(object):
    """Facilitate temporary changes to PyAuto command timeout.

    Automatically resets to original timeout when object is destroyed.
    """
    _saved_timeout = -1  # Saved timeout value

    def __init__(self, ui_test, new_timeout):
      """Initialize.

      Args:
        ui_test: a PyUITest object
        new_timeout: new timeout to use (in milli secs)
      """
      self._saved_timeout = ui_test._automation_timeout
      ui_test._automation_timeout = new_timeout
      self._ui_test = ui_test

    def __del__(self):
      """Reset command_execution_timeout_ms to original value."""
      self._ui_test._automation_timeout = self._saved_timeout

  class JavascriptExecutor(object):
    """Abstract base class for JavaScript injection.

    Derived classes should override Execute method."""
    def Execute(self, script):
      pass

  class JavascriptExecutorInTab(JavascriptExecutor):
    """Wrapper for injecting JavaScript in a tab."""
    def __init__(self, ui_test, tab_index=0, windex=0, frame_xpath=''):
      """Initialize.

        Refer to ExecuteJavascript() for the complete argument list
        description.

      Args:
        ui_test: a PyUITest object
      """
      self._ui_test = ui_test
      self.windex = windex
      self.tab_index = tab_index
      self.frame_xpath = frame_xpath

    def Execute(self, script):
      """Execute script in the tab."""
      return self._ui_test.ExecuteJavascript(script,
                                             self.tab_index,
                                             self.windex,
                                             self.frame_xpath)

  class JavascriptExecutorInRenderView(JavascriptExecutor):
    """Wrapper for injecting JavaScript in an extension view."""
    def __init__(self, ui_test, view, frame_xpath=''):
      """Initialize.

        Refer to ExecuteJavascriptInRenderView() for the complete argument list
        description.

      Args:
        ui_test: a PyUITest object
      """
      self._ui_test = ui_test
      self.view = view
      self.frame_xpath = frame_xpath

    def Execute(self, script):
      """Execute script in the render view."""
      return self._ui_test.ExecuteJavascriptInRenderView(script,
                                                         self.view,
                                                         self.frame_xpath)

  def _GetResultFromJSONRequestDiagnostics(self):
    """Same as _GetResultFromJSONRequest without throwing a timeout exception.

    This method is used to diagnose if a command returns without causing a
    timout exception to be thrown.  This should be used for debugging purposes
    only.

    Returns:
      True if the request returned; False if it timed out.
    """
    result = self._SendJSONRequest(-1,
             json.dumps({'command': 'GetBrowserInfo',}),
             self._automation_timeout)
    if not result:
      # The diagnostic command did not complete, Chrome is probably in a bad
      # state
      return False
    return True

  def _GetResultFromJSONRequest(self, cmd_dict, windex=0, timeout=-1):
    """Issue call over the JSON automation channel and fetch output.

    This method packages the given dictionary into a json string, sends it
    over the JSON automation channel, loads the json output string returned,
    and returns it back as a dictionary.

    Args:
      cmd_dict: the command dictionary. It must have a 'command' key
                Sample:
                  {
                    'command': 'SetOmniboxText',
                    'text': text,
                  }
      windex: 0-based window index on which to work. Default: 0 (first window)
              Use -ve windex or None if the automation command does not apply
              to a browser window. Example: for chromeos login

      timeout: request timeout (in milliseconds)

    Returns:
      a dictionary for the output returned by the automation channel.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    if timeout == -1:  # Default
      timeout = self._automation_timeout
    if windex is None:  # Do not target any window
      windex = -1
    result = self._SendJSONRequest(windex, json.dumps(cmd_dict), timeout)
    if not result:
      additional_info = 'No information available.'
      # Windows does not support os.kill until Python 2.7.
      if not self.IsWin() and _BROWSER_PID:
        browser_pid_exists = True
        # Does the browser PID exist?
        try:
          # Does not actually kill the process
          os.kill(int(_BROWSER_PID), 0)
        except OSError:
          browser_pid_exists = False
        if browser_pid_exists:
          if self._GetResultFromJSONRequestDiagnostics():
            # Browser info, worked, that means this hook had a problem
            additional_info = ('The browser process ID %d still exists. '
                               'PyAuto was able to obtain browser info. It '
                               'is possible this hook is broken.'
                               % _BROWSER_PID)
          else:
            additional_info = ('The browser process ID %d still exists. '
                               'PyAuto was not able to obtain browser info. '
                               'It is possible the browser is hung.'
                               % _BROWSER_PID)
        else:
          additional_info = ('The browser process ID %d no longer exists. '
                             'Perhaps the browser crashed.' % _BROWSER_PID)
      elif not _BROWSER_PID:
        additional_info = ('The browser PID was not obtained. Does this test '
                           'have a unique startup configuration?')
      # Mask private data if it is in the JSON dictionary
      cmd_dict_copy = copy.copy(cmd_dict)
      if 'password' in cmd_dict_copy.keys():
        cmd_dict_copy['password'] = '**********'
      if 'username' in cmd_dict_copy.keys():
        cmd_dict_copy['username'] = 'removed_username'
      raise JSONInterfaceError('Automation call %s received empty response.  '
                               'Additional information:\n%s' % (cmd_dict_copy,
                               additional_info))
    ret_dict = json.loads(result)
    if ret_dict.has_key('error'):
      if ret_dict.get('is_interface_timeout'):
        raise AutomationCommandTimeout(ret_dict['error'])
      elif ret_dict.get('is_interface_error'):
        raise JSONInterfaceError(ret_dict['error'])
      else:
        raise AutomationCommandFail(ret_dict['error'])
    return ret_dict

  def NavigateToURL(self, url, windex=0, tab_index=None, navigation_count=1):
    """Navigate the given tab to the given URL.

    Note that this method also activates the corresponding tab/window if it's
    not active already. Blocks until |navigation_count| navigations have
    completed.

    Args:
      url: The URL to which to navigate, can be a string or GURL object.
      windex: The index of the browser window to work on. Defaults to the first
          window.
      tab_index: The index of the tab to work on. Defaults to the active tab.
      navigation_count: the number of navigations to wait for. Defaults to 1.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    if isinstance(url, GURL):
      url = url.spec()
    if tab_index is None:
      tab_index = self.GetActiveTabIndex(windex)
    cmd_dict = {
        'command': 'NavigateToURL',
        'url': url,
        'windex': windex,
        'tab_index': tab_index,
        'navigation_count': navigation_count,
    }
    self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def NavigateToURLAsync(self, url, windex=0, tab_index=None):
    """Initiate a URL navigation.

    A wrapper for NavigateToURL with navigation_count set to 0.
    """
    self.NavigateToURL(url, windex, tab_index, 0)

  def ApplyAccelerator(self, accelerator, windex=0):
    """Apply the accelerator with the given id.

    Note that this method schedules the accelerator, but does not wait for it to
    actually finish doing anything.

    Args:
      accelerator: The accelerator id, IDC_BACK, IDC_NEWTAB, etc. The list of
          ids can be found at chrome/app/chrome_command_ids.h.
      windex: The index of the browser window to work on. Defaults to the first
          window.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """

    cmd_dict = {
        'command': 'ApplyAccelerator',
        'accelerator': accelerator,
        'windex': windex,
    }
    self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def RunCommand(self, accelerator, windex=0):
    """Apply the accelerator with the given id and wait for it to finish.

    This is like ApplyAccelerator except that it waits for the command to finish
    executing.

    Args:
      accelerator: The accelerator id. The list of ids can be found at
          chrome/app/chrome_command_ids.h.
      windex: The index of the browser window to work on. Defaults to the first
          window.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
        'command': 'RunCommand',
        'accelerator': accelerator,
        'windex': windex,
    }
    self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def IsMenuCommandEnabled(self, accelerator, windex=0):
    """Check if a command is enabled for a window.

    Returns true if the command with the given accelerator id is enabled on the
    given window.

    Args:
      accelerator: The accelerator id. The list of ids can be found at
          chrome/app/chrome_command_ids.h.
      windex: The index of the browser window to work on. Defaults to the first
          window.

    Returns:
      True if the command is enabled for the given window.
    """
    cmd_dict = {
        'command': 'IsMenuCommandEnabled',
        'accelerator': accelerator,
        'windex': windex,
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None).get('enabled')

  def TabGoForward(self, tab_index=0, windex=0):
    """Navigate a tab forward in history.

    Equivalent to clicking the Forward button in the UI. Activates the tab as a
    side effect.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    self.ActivateTab(tab_index, windex)
    self.RunCommand(IDC_FORWARD, windex)

  def TabGoBack(self, tab_index=0, windex=0):
    """Navigate a tab backwards in history.

    Equivalent to clicking the Back button in the UI. Activates the tab as a
    side effect.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    self.ActivateTab(tab_index, windex)
    self.RunCommand(IDC_BACK, windex)

  def ReloadTab(self, tab_index=0, windex=0):
    """Reload the given tab.

    Blocks until the page has reloaded.

    Args:
      tab_index: The index of the tab to reload. Defaults to 0.
      windex: The index of the browser window to work on. Defaults to the first
          window.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    self.ActivateTab(tab_index, windex)
    self.RunCommand(IDC_RELOAD, windex)

  def CloseTab(self, tab_index=0, windex=0, wait_until_closed=True):
    """Close the given tab.

    Note: Be careful closing the last tab in a window as it may close the
        browser.

    Args:
      tab_index: The index of the tab to reload. Defaults to 0.
      windex: The index of the browser window to work on. Defaults to the first
          window.
      wait_until_closed: Whether to block until the tab finishes closing.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
        'command': 'CloseTab',
        'tab_index': tab_index,
        'windex': windex,
        'wait_until_closed': wait_until_closed,
    }
    self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def WaitForTabToBeRestored(self, tab_index=0, windex=0, timeout=-1):
    """Wait for the given tab to be restored.

    Args:
      tab_index: The index of the tab to reload. Defaults to 0.
      windex: The index of the browser window to work on. Defaults to the first
          window.
      timeout: Timeout in milliseconds.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
        'command': 'CloseTab',
        'tab_index': tab_index,
        'windex': windex,
    }
    self._GetResultFromJSONRequest(cmd_dict, windex=None, timeout=timeout)

  def ReloadActiveTab(self, windex=0):
    """Reload an active tab.

    Warning: Depending on the concept of an active tab is dangerous as it can
    change during the test. Use ReloadTab and supply a tab_index explicitly.

    Args:
      windex: The index of the browser window to work on. Defaults to the first
          window.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    self.ReloadTab(self.GetActiveTabIndex(windex), windex)

  def GetActiveTabIndex(self, windex=0):
    """Get the index of the currently active tab in the given browser window.

    Warning: Depending on the concept of an active tab is dangerous as it can
    change during the test. Supply the tab_index explicitly, if possible.

    Args:
      windex: The index of the browser window to work on. Defaults to the first
          window.

    Returns:
      An integer index for the currently active tab.
    """
    cmd_dict = {
        'command': 'GetActiveTabIndex',
        'windex': windex,
    }
    return self._GetResultFromJSONRequest(cmd_dict,
                                          windex=None).get('tab_index')

  def ActivateTab(self, tab_index=0, windex=0):
    """Activates the given tab in the specified window.

    Warning: Depending on the concept of an active tab is dangerous as it can
    change during the test. Instead use functions that accept a tab_index
    explicitly.

    Args:
      tab_index: Integer index of the tab to activate; defaults to 0.
      windex: Integer index of the browser window to use; defaults to the first
          window.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
        'command': 'ActivateTab',
        'tab_index': tab_index,
        'windex': windex,
    }
    self.BringBrowserToFront(windex)
    self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def BringBrowserToFront(self, windex=0):
    """Activate the browser's window and bring it to front.

    Args:
      windex: Integer index of the browser window to use; defaults to the first
          window.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
        'command': 'BringBrowserToFront',
        'windex': windex,
    }
    self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def GetBrowserWindowCount(self):
    """Get the browser window count.

    Args:
      None.

    Returns:
      Integer count of the number of browser windows. Includes popups.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {'command': 'GetBrowserWindowCount'}
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)['count']

  def OpenNewBrowserWindow(self, show):
    """Create a new browser window.

    Args:
      show: Boolean indicating whether to show the window.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
        'command': 'OpenNewBrowserWindow',
        'show': show,
    }
    self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def CloseBrowserWindow(self, windex=0):
    """Create a new browser window.

    Args:
      windex: Index of the browser window to close; defaults to 0.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
        'command': 'CloseBrowserWindow',
        'windex': windex,
    }
    self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def AppendTab(self, url, windex=0):
    """Append a new tab.

    Creates a new tab at the end of given browser window and activates
    it. Blocks until the specified |url| is loaded.

    Args:
      url: The url to load, can be string or a GURL object.
      windex: The index of the browser window to work on. Defaults to the first
          window.

    Returns:
      True if the url loads successfully in the new tab. False otherwise.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    if isinstance(url, GURL):
      url = url.spec()
    cmd_dict = {
        'command': 'AppendTab',
        'url': url,
        'windex': windex,
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None).get('result')

  def GetTabCount(self, windex=0):
    """Gets the number of tab in the given browser window.

    Args:
      windex: Integer index of the browser window to use; defaults to the first
          window.

    Returns:
      The tab count.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
        'command': 'GetTabCount',
        'windex': windex,
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)['tab_count']

  def GetTabInfo(self, tab_index=0, windex=0):
    """Gets information about the specified tab.

    Args:
      tab_index: Integer index of the tab to activate; defaults to 0.
      windex: Integer index of the browser window to use; defaults to the first
          window.

    Returns:
      A dictionary containing information about the tab.
      Example:
        { u'title': "Hello World",
          u'url': "http://foo.bar", }

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
        'command': 'GetTabInfo',
        'tab_index': tab_index,
        'windex': windex,
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def GetActiveTabTitle(self, windex=0):
    """Gets the title of the active tab.

    Warning: Depending on the concept of an active tab is dangerous as it can
    change during the test. Use GetTabInfo and supply a tab_index explicitly.

    Args:
      windex: Integer index of the browser window to use; defaults to the first
          window.

    Returns:
      The tab title as a string.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    return self.GetTabInfo(self.GetActiveTabIndex(windex), windex)['title']

  def GetActiveTabURL(self, windex=0):
    """Gets the URL of the active tab.

    Warning: Depending on the concept of an active tab is dangerous as it can
    change during the test. Use GetTabInfo and supply a tab_index explicitly.

    Args:
      windex: Integer index of the browser window to use; defaults to the first
          window.

    Returns:
      The tab URL as a GURL object.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    return GURL(str(self.GetTabInfo(self.GetActiveTabIndex(windex),
                                    windex)['url']))

  def ActionOnSSLBlockingPage(self, tab_index=0, windex=0, proceed=True):
    """Take action on an interstitial page.

    Calling this when an interstitial page is not showing is an error.

    Args:
      tab_index: Integer index of the tab to activate; defaults to 0.
      windex: Integer index of the browser window to use; defaults to the first
          window.
      proceed: Whether to proceed to the URL or not.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
        'command': 'ActionOnSSLBlockingPage',
        'tab_index': tab_index,
        'windex': windex,
        'proceed': proceed,
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def GetBookmarkModel(self, windex=0):
    """Return the bookmark model as a BookmarkModel object.

    This is a snapshot of the bookmark model; it is not a proxy and
    does not get updated as the bookmark model changes.
    """
    bookmarks_as_json = self._GetBookmarksAsJSON(windex)
    if not bookmarks_as_json:
      raise JSONInterfaceError('Could not resolve browser proxy.')
    return bookmark_model.BookmarkModel(bookmarks_as_json)

  def _GetBookmarksAsJSON(self, windex=0):
    """Get bookmarks as a JSON dictionary; used by GetBookmarkModel()."""
    cmd_dict = {
        'command': 'GetBookmarksAsJSON',
        'windex': windex,
    }
    self.WaitForBookmarkModelToLoad(windex)
    return self._GetResultFromJSONRequest(cmd_dict,
                                          windex=None)['bookmarks_as_json']

  def WaitForBookmarkModelToLoad(self, windex=0):
    """Gets the status of the bookmark bar as a dictionary.

    Args:
      windex: Integer index of the browser window to use; defaults to the first
          window.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
        'command': 'WaitForBookmarkModelToLoad',
        'windex': windex,
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def GetBookmarkBarStatus(self, windex=0):
    """Gets the status of the bookmark bar as a dictionary.

    Args:
      windex: Integer index of the browser window to use; defaults to the first
          window.

    Returns:
      A dictionary.
      Example:
        { u'visible': True,
          u'animating': False,
          u'detached': False, }

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
        'command': 'GetBookmarkBarStatus',
        'windex': windex,
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def GetBookmarkBarStatus(self, windex=0):
    """Gets the status of the bookmark bar as a dictionary.

    Args:
      windex: Integer index of the browser window to use; defaults to the first
          window.

    Returns:
      A dictionary.
      Example:
        { u'visible': True,
          u'animating': False,
          u'detached': False, }

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
        'command': 'GetBookmarkBarStatus',
        'windex': windex,
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def GetBookmarkBarStatus(self, windex=0):
    """Gets the status of the bookmark bar as a dictionary.

    Args:
      windex: Integer index of the browser window to use; defaults to the first
          window.

    Returns:
      A dictionary.
      Example:
        { u'visible': True,
          u'animating': False,
          u'detached': False, }

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
        'command': 'GetBookmarkBarStatus',
        'windex': windex,
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def GetBookmarkBarVisibility(self, windex=0):
    """Returns the visibility of the bookmark bar.

    Args:
      windex: Integer index of the browser window to use; defaults to the first
          window.

    Returns:
      True if the bookmark bar is visible, false otherwise.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    return self.GetBookmarkBarStatus(windex)['visible']

  def AddBookmarkGroup(self, parent_id, index, title, windex=0):
    """Adds a bookmark folder.

    Args:
      parent_id: The parent bookmark folder.
      index: The location in the parent's list to insert this bookmark folder.
      title: The name of the bookmark folder.
      windex: Integer index of the browser window to use; defaults to the first
          window.

    Returns:
      True if the bookmark bar is detached, false otherwise.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    if isinstance(parent_id, basestring):
      parent_id = int(parent_id)
    cmd_dict = {
        'command': 'AddBookmark',
        'parent_id': parent_id,
        'index': index,
        'title': title,
        'is_folder': True,
        'windex': windex,
    }
    self.WaitForBookmarkModelToLoad(windex)
    self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def AddBookmarkURL(self, parent_id, index, title, url, windex=0):
    """Add a bookmark URL.

    Args:
      parent_id: The parent bookmark folder.
      index: The location in the parent's list to insert this bookmark.
      title: The name of the bookmark.
      url: The url of the bookmark.
      windex: Integer index of the browser window to use; defaults to the first
          window.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    if isinstance(parent_id, basestring):
      parent_id = int(parent_id)
    cmd_dict = {
        'command': 'AddBookmark',
        'parent_id': parent_id,
        'index': index,
        'title': title,
        'url': url,
        'is_folder': False,
        'windex': windex,
    }
    self.WaitForBookmarkModelToLoad(windex)
    self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def ReparentBookmark(self, id, new_parent_id, index, windex=0):
    """Move a bookmark.

    Args:
      id: The bookmark to move.
      new_parent_id: The new parent bookmark folder.
      index: The location in the parent's list to insert this bookmark.
      windex: Integer index of the browser window to use; defaults to the first
          window.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    if isinstance(id, basestring):
      id = int(id)
    if isinstance(new_parent_id, basestring):
      new_parent_id = int(new_parent_id)
    cmd_dict = {
        'command': 'ReparentBookmark',
        'id': id,
        'new_parent_id': new_parent_id,
        'index': index,
        'windex': windex,
    }
    self.WaitForBookmarkModelToLoad(windex)
    self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def SetBookmarkTitle(self, id, title, windex=0):
    """Change the title of a bookmark.

    Args:
      id: The bookmark to rename.
      title: The new title for the bookmark.
      windex: Integer index of the browser window to use; defaults to the first
          window.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    if isinstance(id, basestring):
      id = int(id)
    cmd_dict = {
        'command': 'SetBookmarkTitle',
        'id': id,
        'title': title,
        'windex': windex,
    }
    self.WaitForBookmarkModelToLoad(windex)
    self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def SetBookmarkURL(self, id, url, windex=0):
    """Change the URL of a bookmark.

    Args:
      id: The bookmark to change.
      url: The new url for the bookmark.
      windex: Integer index of the browser window to use; defaults to the first
          window.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    if isinstance(id, basestring):
      id = int(id)
    cmd_dict = {
        'command': 'SetBookmarkURL',
        'id': id,
        'url': url,
        'windex': windex,
    }
    self.WaitForBookmarkModelToLoad(windex)
    self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def RemoveBookmark(self, id, windex=0):
    """Remove a bookmark.

    Args:
      id: The bookmark to remove.
      windex: Integer index of the browser window to use; defaults to the first
          window.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    if isinstance(id, basestring):
      id = int(id)
    cmd_dict = {
        'command': 'RemoveBookmark',
        'id': id,
        'windex': windex,
    }
    self.WaitForBookmarkModelToLoad(windex)
    self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def GetDownloadsInfo(self, windex=0):
    """Return info about downloads.

    This includes all the downloads recognized by the history system.

    Returns:
      an instance of downloads_info.DownloadInfo
    """
    return download_info.DownloadInfo(
        self._GetResultFromJSONRequest({'command': 'GetDownloadsInfo'},
                                       windex=windex))

  def GetOmniboxInfo(self, windex=0):
    """Return info about Omnibox.

    This represents a snapshot of the omnibox.  If you expect changes
    you need to call this method again to get a fresh snapshot.
    Note that this DOES NOT shift focus to the omnibox; you've to ensure that
    the omnibox is in focus or else you won't get any interesting info.

    It's OK to call this even when the omnibox popup is not showing.  In this
    case however, there won't be any matches, but other properties (like the
    current text in the omnibox) will still be fetched.

    Due to the nature of the omnibox, this function is sensitive to mouse
    focus.  DO NOT HOVER MOUSE OVER OMNIBOX OR CHANGE WINDOW FOCUS WHEN USING
    THIS METHOD.

    Args:
      windex: the index of the browser window to work on.
              Default: 0 (first window)

    Returns:
      an instance of omnibox_info.OmniboxInfo
    """
    return omnibox_info.OmniboxInfo(
        self._GetResultFromJSONRequest({'command': 'GetOmniboxInfo'},
                                       windex=windex))

  def SetOmniboxText(self, text, windex=0):
    """Enter text into the omnibox. This shifts focus to the omnibox.

    Args:
      text: the text to be set.
      windex: the index of the browser window to work on.
              Default: 0 (first window)
    """
    # Ensure that keyword data is loaded from the profile.
    # This would normally be triggered by the user inputting this text.
    self._GetResultFromJSONRequest({'command': 'LoadSearchEngineInfo'})
    cmd_dict = {
        'command': 'SetOmniboxText',
        'text': text,
    }
    self._GetResultFromJSONRequest(cmd_dict, windex=windex)

  # TODO(ace): Remove this hack, update bug 62783.
  def WaitUntilOmniboxReadyHack(self, windex=0):
    """Wait until the omnibox is ready for input.

    This is a hack workaround for linux platform, which returns from
    synchronous window creation methods before the omnibox is fully functional.

    No-op on non-linux platforms.

    Args:
      windex: the index of the browser to work on.
    """
    if self.IsLinux():
      return self.WaitUntil(
          lambda : self.GetOmniboxInfo(windex).Properties('has_focus'))

  def WaitUntilOmniboxQueryDone(self, windex=0):
    """Wait until omnibox has finished populating results.

    Uses WaitUntil() so the wait duration is capped by the timeout values
    used by automation, which WaitUntil() uses.

    Args:
      windex: the index of the browser window to work on.
              Default: 0 (first window)
    """
    return self.WaitUntil(
        lambda : not self.GetOmniboxInfo(windex).IsQueryInProgress())

  def OmniboxMovePopupSelection(self, count, windex=0):
    """Move omnibox popup selection up or down.

    Args:
      count: number of rows by which to move.
             -ve implies down, +ve implies up
      windex: the index of the browser window to work on.
              Default: 0 (first window)
    """
    cmd_dict = {
        'command': 'OmniboxMovePopupSelection',
        'count': count,
    }
    self._GetResultFromJSONRequest(cmd_dict, windex=windex)

  def OmniboxAcceptInput(self, windex=0):
    """Accepts the current string of text in the omnibox.

    This is equivalent to clicking or hiting enter on a popup selection.
    Blocks until the page loads.

    Args:
      windex: the index of the browser window to work on.
              Default: 0 (first window)
    """
    cmd_dict = {
        'command': 'OmniboxAcceptInput',
    }
    self._GetResultFromJSONRequest(cmd_dict, windex=windex)

  def GetCookie(self, url, windex=0):
    """Get the value of the cookie at url in context of the specified browser.

    Args:
      url: Either a GURL object or url string specifing the cookie url.
      windex: The index of the browser window to work on. Defaults to the first
          window.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    if isinstance(url, GURL):
      url = url.spec()
    cmd_dict = {
        'command': 'GetCookiesInBrowserContext',
        'url': url,
        'windex': windex,
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)['cookies']

  def DeleteCookie(self, url, cookie_name, windex=0):
    """Delete the cookie at url with name cookie_name.

    Args:
      url: Either a GURL object or url string specifing the cookie url.
      cookie_name: The name of the cookie to delete as a string.
      windex: The index of the browser window to work on. Defaults to the first
          window.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    if isinstance(url, GURL):
      url = url.spec()
    cmd_dict = {
        'command': 'DeleteCookieInBrowserContext',
        'url': url,
        'cookie_name': cookie_name,
        'windex': windex,
    }
    self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def SetCookie(self, url, value, windex=0):
    """Set the value of the cookie at url to value in the context of a browser.

    Args:
      url: Either a GURL object or url string specifing the cookie url.
      value: A string to set as the cookie's value.
      windex: The index of the browser window to work on. Defaults to the first
          window.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    if isinstance(url, GURL):
      url = url.spec()
    cmd_dict = {
        'command': 'SetCookieInBrowserContext',
        'url': url,
        'value': value,
        'windex': windex,
    }
    self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def GetSearchEngineInfo(self, windex=0):
    """Return info about search engines.

    Args:
      windex: The window index, default is 0.

    Returns:
      An ordered list of dictionaries describing info about each search engine.

      Example:
        [ { u'display_url': u'{google:baseURL}search?q=%s',
            u'host': u'www.google.com',
            u'in_default_list': True,
            u'is_default': True,
            u'is_valid': True,
            u'keyword': u'google.com',
            u'path': u'/search',
            u'short_name': u'Google',
            u'supports_replacement': True,
            u'url': u'{google:baseURL}search?q={searchTerms}'},
          { u'display_url': u'http://search.yahoo.com/search?p=%s',
            u'host': u'search.yahoo.com',
            u'in_default_list': True,
            u'is_default': False,
            u'is_valid': True,
            u'keyword': u'yahoo.com',
            u'path': u'/search',
            u'short_name': u'Yahoo!',
            u'supports_replacement': True,
            u'url': u'http://search.yahoo.com/search?p={searchTerms}'},
    """
    # Ensure that the search engine profile is loaded into data model.
    self._GetResultFromJSONRequest({'command': 'LoadSearchEngineInfo'},
                                   windex=windex)
    cmd_dict = {'command': 'GetSearchEngineInfo'}
    return self._GetResultFromJSONRequest(
        cmd_dict, windex=windex)['search_engines']

  def AddSearchEngine(self, title, keyword, url, windex=0):
    """Add a search engine, as done through the search engines UI.

    Args:
      title: name for search engine.
      keyword: keyword, used to initiate a custom search from omnibox.
      url: url template for this search engine's query.
           '%s' is replaced by search query string when used to search.
      windex: The window index, default is 0.
    """
    # Ensure that the search engine profile is loaded into data model.
    self._GetResultFromJSONRequest({'command': 'LoadSearchEngineInfo'},
                                   windex=windex)
    cmd_dict = {'command': 'AddOrEditSearchEngine',
                'new_title': title,
                'new_keyword': keyword,
                'new_url': url}
    self._GetResultFromJSONRequest(cmd_dict, windex=windex)

  def EditSearchEngine(self, keyword, new_title, new_keyword, new_url,
                       windex=0):
    """Edit info for existing search engine.

    Args:
      keyword: existing search engine keyword.
      new_title: new name for this search engine.
      new_keyword: new keyword for this search engine.
      new_url: new url for this search engine.
      windex: The window index, default is 0.
    """
    # Ensure that the search engine profile is loaded into data model.
    self._GetResultFromJSONRequest({'command': 'LoadSearchEngineInfo'},
                                   windex=windex)
    cmd_dict = {'command': 'AddOrEditSearchEngine',
                'keyword': keyword,
                'new_title': new_title,
                'new_keyword': new_keyword,
                'new_url': new_url}
    self._GetResultFromJSONRequest(cmd_dict, windex=windex)

  def DeleteSearchEngine(self, keyword, windex=0):
    """Delete search engine with given keyword.

    Args:
      keyword: the keyword string of the search engine to delete.
      windex: The window index, default is 0.
    """
    # Ensure that the search engine profile is loaded into data model.
    self._GetResultFromJSONRequest({'command': 'LoadSearchEngineInfo'},
                                   windex=windex)
    cmd_dict = {'command': 'PerformActionOnSearchEngine', 'keyword': keyword,
                'action': 'delete'}
    self._GetResultFromJSONRequest(cmd_dict, windex=windex)

  def MakeSearchEngineDefault(self, keyword, windex=0):
    """Make search engine with given keyword the default search.

    Args:
      keyword: the keyword string of the search engine to make default.
      windex: The window index, default is 0.
    """
    # Ensure that the search engine profile is loaded into data model.
    self._GetResultFromJSONRequest({'command': 'LoadSearchEngineInfo'},
                                   windex=windex)
    cmd_dict = {'command': 'PerformActionOnSearchEngine', 'keyword': keyword,
                'action': 'default'}
    self._GetResultFromJSONRequest(cmd_dict, windex=windex)

  def GetLocalStatePrefsInfo(self):
    """Return info about preferences.

    This represents a snapshot of the local state preferences. If you expect
    local state preferences to have changed, you need to call this method again
    to get a fresh snapshot.

    Returns:
      an instance of prefs_info.PrefsInfo
    """
    return prefs_info.PrefsInfo(
        self._GetResultFromJSONRequest({'command': 'GetLocalStatePrefsInfo'},
                                       windex=None))

  def SetLocalStatePrefs(self, path, value):
    """Set local state preference for the given path.

    Preferences are stored by Chromium as a hierarchical dictionary.
    dot-separated paths can be used to refer to a particular preference.
    example: "session.restore_on_startup"

    Some preferences are managed, that is, they cannot be changed by the
    user. It's up to the user to know which ones can be changed. Typically,
    the options available via Chromium preferences can be changed.

    Args:
      path: the path the preference key that needs to be changed
            example: "session.restore_on_startup"
            One of the equivalent names in chrome/common/pref_names.h could
            also be used.
      value: the value to be set. It could be plain values like int, bool,
             string or complex ones like list.
             The user has to ensure that the right value is specified for the
             right key. It's useful to dump the preferences first to determine
             what type is expected for a particular preference path.
    """
    cmd_dict = {
      'command': 'SetLocalStatePrefs',
      'windex': 0,
      'path': path,
      'value': value,
    }
    self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def GetPrefsInfo(self, windex=0):
    """Return info about preferences.

    This represents a snapshot of the preferences. If you expect preferences
    to have changed, you need to call this method again to get a fresh
    snapshot.

    Args:
      windex: The window index, default is 0.
    Returns:
      an instance of prefs_info.PrefsInfo
    """
    cmd_dict = {
      'command': 'GetPrefsInfo',
      'windex': windex,
    }
    return prefs_info.PrefsInfo(
        self._GetResultFromJSONRequest(cmd_dict, windex=None))

  def SetPrefs(self, path, value, windex=0):
    """Set preference for the given path.

    Preferences are stored by Chromium as a hierarchical dictionary.
    dot-separated paths can be used to refer to a particular preference.
    example: "session.restore_on_startup"

    Some preferences are managed, that is, they cannot be changed by the
    user. It's up to the user to know which ones can be changed. Typically,
    the options available via Chromium preferences can be changed.

    Args:
      path: the path the preference key that needs to be changed
            example: "session.restore_on_startup"
            One of the equivalent names in chrome/common/pref_names.h could
            also be used.
      value: the value to be set. It could be plain values like int, bool,
             string or complex ones like list.
             The user has to ensure that the right value is specified for the
             right key. It's useful to dump the preferences first to determine
             what type is expected for a particular preference path.
      windex: window index to work on. Defaults to 0 (first window).
    """
    cmd_dict = {
      'command': 'SetPrefs',
      'windex': windex,
      'path': path,
      'value': value,
    }
    self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def SendWebkitKeyEvent(self, key_type, key_code, tab_index=0, windex=0):
    """Send a webkit key event to the browser.

    Args:
      key_type: the raw key type such as 0 for up and 3 for down.
      key_code: the hex value associated with the keypress (virtual key code).
      tab_index: tab index to work on. Defaults to 0 (first tab).
      windex: window index to work on. Defaults to 0 (first window).
    """
    cmd_dict = {
      'command': 'SendWebkitKeyEvent',
      'type': key_type,
      'text': '',
      'isSystemKey': False,
      'unmodifiedText': '',
      'nativeKeyCode': 0,
      'windowsKeyCode': key_code,
      'modifiers': 0,
      'windex': windex,
      'tab_index': tab_index,
    }
    # Sending request for key event.
    self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def SendWebkitCharEvent(self, char, tab_index=0, windex=0):
    """Send a webkit char to the browser.

    Args:
      char: the char value to be sent to the browser.
      tab_index: tab index to work on. Defaults to 0 (first tab).
      windex: window index to work on. Defaults to 0 (first window).
    """
    cmd_dict = {
      'command': 'SendWebkitKeyEvent',
      'type': 2,  # kCharType
      'text': char,
      'isSystemKey': False,
      'unmodifiedText': char,
      'nativeKeyCode': 0,
      'windowsKeyCode': ord((char).upper()),
      'modifiers': 0,
      'windex': windex,
      'tab_index': tab_index,
    }
    # Sending request for a char.
    self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def SetDownloadShelfVisible(self, is_visible, windex=0):
    """Set download shelf visibility for the specified browser window.

    Args:
      is_visible: A boolean indicating the desired shelf visibility.
      windex: The window index, defaults to 0 (the first window).

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
      'command': 'SetDownloadShelfVisible',
      'is_visible': is_visible,
      'windex': windex,
    }
    self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def IsDownloadShelfVisible(self, windex=0):
    """Determine whether the download shelf is visible in the given window.

    Args:
      windex: The window index, defaults to 0 (the first window).

    Returns:
      A boolean indicating the shelf visibility.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
      'command': 'IsDownloadShelfVisible',
      'windex': windex,
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)['is_visible']

  def GetDownloadDirectory(self, tab_index=None, windex=0):
    """Get the path to the download directory.

    Warning: Depending on the concept of an active tab is dangerous as it can
    change during the test. Always supply a tab_index explicitly.

    Args:
      tab_index: The index of the tab to work on. Defaults to the active tab.
      windex: The index of the browser window to work on. Defaults to 0.

    Returns:
      The path to the download directory as a FilePath object.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    if tab_index is None:
      tab_index = self.GetActiveTabIndex(windex)
    cmd_dict = {
      'command': 'GetDownloadDirectory',
      'tab_index': tab_index,
      'windex': windex,
    }
    return FilePath(str(self._GetResultFromJSONRequest(cmd_dict,
                                                       windex=None)['path']))

  def WaitForAllDownloadsToComplete(self, pre_download_ids=[], windex=0,
                                    timeout=-1):
    """Wait for all pending downloads to complete.

    This function assumes that any downloads to wait for have already been
    triggered and have started (it is ok if those downloads complete before this
    function is called).

    Args:
      pre_download_ids: A list of numbers representing the IDs of downloads that
                        exist *before* downloads to wait for have been
                        triggered. Defaults to []; use GetDownloadsInfo() to get
                        these IDs (only necessary if a test previously
                        downloaded files).
      windex: The window index, defaults to 0 (the first window).
      timeout: The maximum amount of time (in milliseconds) to wait for
               downloads to complete.
    """
    cmd_dict = {
      'command': 'WaitForAllDownloadsToComplete',
      'pre_download_ids': pre_download_ids,
    }
    self._GetResultFromJSONRequest(cmd_dict, windex=windex, timeout=timeout)

  def PerformActionOnDownload(self, id, action, window_index=0):
    """Perform the given action on the download with the given id.

    Args:
      id: The id of the download.
      action: The action to perform on the download.
              Possible actions:
                'open': Opens the download (waits until it has completed first).
                'toggle_open_files_like_this': Toggles the 'Always Open Files
                    Of This Type' option.
                'remove': Removes the file from downloads (not from disk).
                'decline_dangerous_download': Equivalent to 'Discard' option
                    after downloading a dangerous download (ex. an executable).
                'save_dangerous_download': Equivalent to 'Save' option after
                    downloading a dangerous file.
                'pause': Pause the download.  If the download completed before
                    this call or is already paused, it's a no-op.
                'resume': Resume the download.  If the download completed before
                    this call or was not paused, it's a no-op.
                'cancel': Cancel the download.
      window_index: The window index, default is 0.

    Returns:
      A dictionary representing the updated download item (except in the case
      of 'decline_dangerous_download', 'toggle_open_files_like_this', and
      'remove', which return an empty dict).
      Example dictionary:
      { u'PercentComplete': 100,
        u'file_name': u'file.txt',
        u'full_path': u'/path/to/file.txt',
        u'id': 0,
        u'is_otr': False,
        u'is_paused': False,
        u'is_temporary': False,
        u'open_when_complete': False,
        u'referrer_url': u'',
        u'state': u'COMPLETE',
        u'danger_type': u'DANGEROUS_FILE',
        u'url':  u'file://url/to/file.txt'
      }
    """
    cmd_dict = {  # Prepare command for the json interface
      'command': 'PerformActionOnDownload',
      'id': id,
      'action': action
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=window_index)

  def DownloadAndWaitForStart(self, file_url, windex=0):
    """Trigger download for the given url and wait for downloads to start.

    It waits for download by looking at the download info from Chrome, so
    anything which isn't registered by the history service won't be noticed.
    This is not thread-safe, but it's fine to call this method to start
    downloading multiple files in parallel. That is after starting a
    download, it's fine to start another one even if the first one hasn't
    completed.
    """
    try:
      num_downloads = len(self.GetDownloadsInfo(windex).Downloads())
    except JSONInterfaceError:
      num_downloads = 0

    self.NavigateToURL(file_url, windex)  # Trigger download.
    # It might take a while for the download to kick in, hold on until then.
    self.assertTrue(self.WaitUntil(
        lambda: len(self.GetDownloadsInfo(windex).Downloads()) >
                num_downloads))

  def SetWindowDimensions(
      self, x=None, y=None, width=None, height=None, windex=0):
    """Set window dimensions.

    All args are optional and current values will be preserved.
    Arbitrarily large values will be handled gracefully by the browser.

    Args:
      x: window origin x
      y: window origin y
      width: window width
      height: window height
      windex: window index to work on. Defaults to 0 (first window)
    """
    cmd_dict = {  # Prepare command for the json interface
      'command': 'SetWindowDimensions',
    }
    if x:
      cmd_dict['x'] = x
    if y:
      cmd_dict['y'] = y
    if width:
      cmd_dict['width'] = width
    if height:
      cmd_dict['height'] = height
    self._GetResultFromJSONRequest(cmd_dict, windex=windex)

  def WaitForInfobarCount(self, count, windex=0, tab_index=0):
    """Wait until infobar count becomes |count|.

    Note: Wait duration is capped by the automation timeout.

    Args:
      count: requested number of infobars
      windex: window index.  Defaults to 0 (first window)
      tab_index: tab index  Defaults to 0 (first tab)

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    # TODO(phajdan.jr): We need a solid automation infrastructure to handle
    # these cases. See crbug.com/53647.
    def _InfobarCount():
      windows = self.GetBrowserInfo()['windows']
      if windex >= len(windows):  # not enough windows
        return -1
      tabs = windows[windex]['tabs']
      if tab_index >= len(tabs):  # not enough tabs
        return -1
      return len(tabs[tab_index]['infobars'])

    return self.WaitUntil(_InfobarCount, expect_retval=count)

  def PerformActionOnInfobar(
      self, action, infobar_index, windex=0, tab_index=0):
    """Perform actions on an infobar.

    Args:
      action: the action to be performed.
              Actions depend on the type of the infobar.  The user needs to
              call the right action for the right infobar.
              Valid inputs are:
              - "dismiss": closes the infobar (for all infobars)
              - "accept", "cancel": click accept / cancel (for confirm infobars)
              - "allow", "deny": click allow / deny (for media stream infobars)
      infobar_index: 0-based index of the infobar on which to perform the action
      windex: 0-based window index  Defaults to 0 (first window)
      tab_index: 0-based tab index.  Defaults to 0 (first tab)

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
      'command': 'PerformActionOnInfobar',
      'action': action,
      'infobar_index': infobar_index,
      'tab_index': tab_index,
    }
    if action not in ('dismiss', 'accept', 'allow', 'deny', 'cancel'):
      raise JSONInterfaceError('Invalid action %s' % action)
    self._GetResultFromJSONRequest(cmd_dict, windex=windex)

  def GetBrowserInfo(self):
    """Return info about the browser.

    This includes things like the version number, the executable name,
    executable path, pid info about the renderer/plugin/extension processes,
    window dimensions. (See sample below)

    For notification pid info, see 'GetActiveNotifications'.

    Returns:
      a dictionary

      Sample:
      { u'browser_pid': 93737,
        # Child processes are the processes for plugins and other workers.
        u'child_process_path': u'.../Chromium.app/Contents/'
                                'Versions/6.0.412.0/Chromium Helper.app/'
                                'Contents/MacOS/Chromium Helper',
        u'child_processes': [ { u'name': u'Shockwave Flash',
                                u'pid': 93766,
                                u'type': u'Plug-in'}],
        u'extension_views': [ {
          u'name': u'Webpage Screenshot',
          u'pid': 93938,
          u'extension_id': u'dgcoklnmbeljaehamekjpeidmbicddfj',
          u'url': u'chrome-extension://dgcoklnmbeljaehamekjpeidmbicddfj/'
                    'bg.html',
          u'loaded': True,
          u'view': {
            u'render_process_id': 2,
            u'render_view_id': 1},
          u'view_type': u'EXTENSION_BACKGROUND_PAGE'}]
        u'properties': {
          u'BrowserProcessExecutableName': u'Chromium',
          u'BrowserProcessExecutablePath': u'Chromium.app/Contents/MacOS/'
                                            'Chromium',
          u'ChromeVersion': u'6.0.412.0',
          u'HelperProcessExecutableName': u'Chromium Helper',
          u'HelperProcessExecutablePath': u'Chromium Helper.app/Contents/'
                                            'MacOS/Chromium Helper',
          u'command_line_string': "COMMAND_LINE_STRING --WITH-FLAGS",
          u'branding': 'Chromium',
          u'is_official': False,}
        # The order of the windows and tabs listed here will be the same as
        # what shows up on screen.
        u'windows': [ { u'index': 0,
                        u'height': 1134,
                        u'incognito': False,
                        u'profile_path': u'Default',
                        u'fullscreen': False,
                        u'visible_page_actions':
                          [u'dgcoklnmbeljaehamekjpeidmbicddfj',
                           u'osfcklnfasdofpcldmalwpicslasdfgd']
                        u'selected_tab': 0,
                        u'tabs': [ {
                          u'index': 0,
                          u'infobars': [],
                          u'pinned': True,
                          u'renderer_pid': 93747,
                          u'url': u'http://www.google.com/' }, {
                          u'index': 1,
                          u'infobars': [],
                          u'pinned': False,
                          u'renderer_pid': 93919,
                          u'url': u'https://chrome.google.com/'}, {
                          u'index': 2,
                          u'infobars': [ {
                            u'buttons': [u'Allow', u'Deny'],
                            u'link_text': u'Learn more',
                            u'text': u'slides.html5rocks.com wants to track '
                                      'your physical location',
                            u'type': u'confirm_infobar'}],
                          u'pinned': False,
                          u'renderer_pid': 93929,
                          u'url': u'http://slides.html5rocks.com/#slide14'},
                            ],
                        u'type': u'tabbed',
                        u'width': 925,
                        u'x': 26,
                        u'y': 44}]}

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {  # Prepare command for the json interface
      'command': 'GetBrowserInfo',
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def IsAura(self):
    """Is this Aura?"""
    return self.GetBrowserInfo()['properties']['aura']

  def GetProcessInfo(self):
    """Returns information about browser-related processes that currently exist.

    This will also return information about other currently-running browsers
    besides just Chrome.

    Returns:
      A dictionary containing browser-related process information as identified
      by class MemoryDetails in src/chrome/browser/memory_details.h.  The
      dictionary contains a single key 'browsers', mapped to a list of
      dictionaries containing information about each browser process name.
      Each of those dictionaries contains a key 'processes', mapped to a list
      of dictionaries containing the specific information for each process
      with the given process name.

      The memory values given in |committed_mem| and |working_set_mem| are in
      KBytes.

      Sample:
      { 'browsers': [ { 'name': 'Chromium',
                        'process_name': 'chrome',
                        'processes': [ { 'child_process_type': 'Browser',
                                         'committed_mem': { 'image': 0,
                                                            'mapped': 0,
                                                            'priv': 0},
                                         'is_diagnostics': False,
                                         'num_processes': 1,
                                         'pid': 7770,
                                         'product_name': '',
                                         'renderer_type': 'Unknown',
                                         'titles': [],
                                         'version': '',
                                         'working_set_mem': { 'priv': 43672,
                                                              'shareable': 0,
                                                              'shared': 59251}},
                                       { 'child_process_type': 'Tab',
                                         'committed_mem': { 'image': 0,
                                                            'mapped': 0,
                                                            'priv': 0},
                                         'is_diagnostics': False,
                                         'num_processes': 1,
                                         'pid': 7791,
                                         'product_name': '',
                                         'renderer_type': 'Tab',
                                         'titles': ['about:blank'],
                                         'version': '',
                                         'working_set_mem': { 'priv': 16768,
                                                              'shareable': 0,
                                                              'shared': 26256}},
                                       ...<more processes>...]}]}

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {  # Prepare command for the json interface.
      'command': 'GetProcessInfo',
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def GetNavigationInfo(self, tab_index=0, windex=0):
    """Get info about the navigation state of a given tab.

    Args:
      tab_index: The tab index, default is 0.
      window_index: The window index, default is 0.

    Returns:
      a dictionary.
      Sample:

      { u'favicon_url': u'https://www.google.com/favicon.ico',
        u'page_type': u'NORMAL_PAGE',
        u'ssl': { u'displayed_insecure_content': False,
                  u'ran_insecure_content': False,
                  u'security_style': u'SECURITY_STYLE_AUTHENTICATED'}}

      Values for security_style can be:
        SECURITY_STYLE_UNKNOWN
        SECURITY_STYLE_UNAUTHENTICATED
        SECURITY_STYLE_AUTHENTICATION_BROKEN
        SECURITY_STYLE_AUTHENTICATED

      Values for page_type can be:
        NORMAL_PAGE
        ERROR_PAGE
        INTERSTITIAL_PAGE
    """
    cmd_dict = {  # Prepare command for the json interface
      'command': 'GetNavigationInfo',
      'tab_index': tab_index,
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=windex)

  def GetSecurityState(self, tab_index=0, windex=0):
    """Get security details for a given tab.

    Args:
      tab_index: The tab index, default is 0.
      window_index: The window index, default is 0.

    Returns:
      a dictionary.
      Sample:
      { "security_style": SECURITY_STYLE_AUTHENTICATED,
        "ssl_cert_status": 3,  // bitmask of status flags
        "insecure_content_status": 1,  // bitmask of status flags
      }
    """
    cmd_dict = {  # Prepare command for the json interface
      'command': 'GetSecurityState',
      'tab_index': tab_index,
      'windex': windex,
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def GetHistoryInfo(self, search_text='', windex=0):
    """Return info about browsing history.

    Args:
      search_text: the string to search in history.  Defaults to empty string
                   which means that all history would be returned. This is
                   functionally equivalent to searching for a text in the
                   chrome://history UI. So partial matches work too.
                   When non-empty, the history items returned will contain a
                   "snippet" field corresponding to the snippet visible in
                   the chrome://history/ UI.
      windex: index of the browser window, defaults to 0.

    Returns:
      an instance of history_info.HistoryInfo
    """
    cmd_dict = {  # Prepare command for the json interface
      'command': 'GetHistoryInfo',
      'search_text': search_text,
    }
    return history_info.HistoryInfo(
        self._GetResultFromJSONRequest(cmd_dict, windex=windex))

  def InstallExtension(self, extension_path, with_ui=False, from_webstore=None,
                       windex=0, tab_index=0):
    """Installs an extension from the given path.

    The path must be absolute and may be a crx file or an unpacked extension
    directory. Returns the extension ID if successfully installed and loaded.
    Otherwise, throws an exception. The extension must not already be installed.

    Args:
      extension_path: The absolute path to the extension to install. If the
                      extension is packed, it must have a .crx extension.
      with_ui: Whether the extension install confirmation UI should be shown.
      from_webstore: If True, forces a .crx extension to be recognized as one
          from the webstore. Can be used to force install an extension with
          'experimental' permissions.
      windex: Integer index of the browser window to use; defaults to 0
              (first window).

    Returns:
      The ID of the installed extension.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
        'command': 'InstallExtension',
        'path': extension_path,
        'with_ui': with_ui,
        'windex': windex,
        'tab_index': tab_index,
    }

    if from_webstore:
      cmd_dict['from_webstore'] = True
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)['id']

  def GetExtensionsInfo(self, windex=0):
    """Returns information about all installed extensions.

    Args:
      windex: Integer index of the browser window to use; defaults to 0
              (first window).

    Returns:
      A list of dictionaries representing each of the installed extensions.
      Example:
      [ { u'api_permissions': [u'bookmarks', u'experimental', u'tabs'],
          u'background_url': u'',
          u'description': u'Bookmark Manager',
          u'effective_host_permissions': [u'chrome://favicon/*',
                                          u'chrome://resources/*'],
          u'host_permissions': [u'chrome://favicon/*', u'chrome://resources/*'],
          u'id': u'eemcgdkfndhakfknompkggombfjjjeno',
          u'is_component': True,
          u'is_internal': False,
          u'name': u'Bookmark Manager',
          u'options_url': u'',
          u'public_key': u'MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDQcByy+eN9jza\
                           zWF/DPn7NW47sW7lgmpk6eKc0BQM18q8hvEM3zNm2n7HkJv/R6f\
                           U+X5mtqkDuKvq5skF6qqUF4oEyaleWDFhd1xFwV7JV+/DU7bZ00\
                           w2+6gzqsabkerFpoP33ZRIw7OviJenP0c0uWqDWF8EGSyMhB3tx\
                           qhOtiQIDAQAB',
          u'version': u'0.1' },
        { u'api_permissions': [...],
          u'background_url': u'chrome-extension://\
                               lkdedmbpkaiahjjibfdmpoefffnbdkli/\
                               background.html',
          u'description': u'Extension which lets you read your Facebook news \
                            feed and wall. You can also post status updates.',
          u'effective_host_permissions': [...],
          u'host_permissions': [...],
          u'id': u'lkdedmbpkaiahjjibfdmpoefffnbdkli',
          u'name': u'Facebook for Google Chrome',
          u'options_url': u'',
          u'public_key': u'...',
          u'version': u'2.0.9'
          u'is_enabled': True,
          u'allowed_in_incognito': True} ]
    """
    cmd_dict = {  # Prepare command for the json interface
      'command': 'GetExtensionsInfo',
      'windex': windex,
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)['extensions']

  def UninstallExtensionById(self, id, windex=0):
    """Uninstall the extension with the given id.

    Args:
      id: The string id of the extension.
      windex: Integer index of the browser window to use; defaults to 0
              (first window).

    Returns:
      True, if the extension was successfully uninstalled, or
      False, otherwise.
    """
    cmd_dict = {  # Prepare command for the json interface
      'command': 'UninstallExtensionById',
      'id': id,
      'windex': windex,
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)['success']

  def SetExtensionStateById(self, id, enable, allow_in_incognito, windex=0):
    """Set extension state: enable/disable, allow/disallow in incognito mode.

    Args:
      id: The string id of the extension.
      enable: A boolean, enable extension.
      allow_in_incognito: A boolean, allow extension in incognito.
      windex: Integer index of the browser window to use; defaults to 0
              (first window).
    """
    cmd_dict = {  # Prepare command for the json interface
      'command': 'SetExtensionStateById',
      'id': id,
      'enable': enable,
      'allow_in_incognito': allow_in_incognito,
      'windex': windex,
    }
    self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def TriggerPageActionById(self, id, tab_index=0, windex=0):
    """Trigger page action asynchronously in the active tab.

    The page action icon must be displayed before invoking this function.

    Args:
      id: The string id of the extension.
      tab_index: Integer index of the tab to use; defaults to 0 (first tab).
      windex: Integer index of the browser window to use; defaults to 0
              (first window).
    """
    cmd_dict = {  # Prepare command for the json interface
      'command': 'TriggerPageActionById',
      'id': id,
      'windex': windex,
      'tab_index': tab_index,
    }
    self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def TriggerBrowserActionById(self, id, tab_index=0, windex=0):
    """Trigger browser action asynchronously in the active tab.

    Args:
      id: The string id of the extension.
      tab_index: Integer index of the tab to use; defaults to 0 (first tab).
      windex: Integer index of the browser window to use; defaults to 0
              (first window).
    """
    cmd_dict = {  # Prepare command for the json interface
      'command': 'TriggerBrowserActionById',
      'id': id,
      'windex': windex,
      'tab_index': tab_index,
    }
    self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def UpdateExtensionsNow(self, windex=0):
    """Auto-updates installed extensions.

    Waits until all extensions are updated, loaded, and ready for use.
    This is equivalent to clicking the "Update extensions now" button on the
    chrome://extensions page.

    Args:
      windex: Integer index of the browser window to use; defaults to 0
              (first window).

    Raises:
      pyauto_errors.JSONInterfaceError if the automation returns an error.
    """
    cmd_dict = {  # Prepare command for the json interface.
      'command': 'UpdateExtensionsNow',
      'windex': windex,
    }
    self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def WaitUntilExtensionViewLoaded(self, name=None, extension_id=None,
                                   url=None, view_type=None):
    """Wait for a loaded extension view matching all the given properties.

    If no matching extension views are found, wait for one to be loaded.
    If there are more than one matching extension view, return one at random.
    Uses WaitUntil so timeout is capped by automation timeout.
    Refer to extension_view dictionary returned in GetBrowserInfo()
    for sample input/output values.

    Args:
      name: (optional) Name of the extension.
      extension_id: (optional) ID of the extension.
      url: (optional) URL of the extension view.
      view_type: (optional) Type of the extension view.
        ['EXTENSION_BACKGROUND_PAGE'|'EXTENSION_POPUP'|'EXTENSION_INFOBAR'|
         'EXTENSION_DIALOG']

    Returns:
      The 'view' property of the extension view.
      None, if no view loaded.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation returns an error.
    """
    def _GetExtensionViewLoaded():
      extension_views = self.GetBrowserInfo()['extension_views']
      for extension_view in extension_views:
        if ((name and name != extension_view['name']) or
            (extension_id and extension_id != extension_view['extension_id']) or
            (url and url != extension_view['url']) or
            (view_type and view_type != extension_view['view_type'])):
          continue
        if extension_view['loaded']:
          return extension_view['view']
      return False

    if self.WaitUntil(lambda: _GetExtensionViewLoaded()):
      return _GetExtensionViewLoaded()
    return None

  def WaitUntilExtensionViewClosed(self, view):
    """Wait for the given extension view to to be closed.

    Uses WaitUntil so timeout is capped by automation timeout.
    Refer to extension_view dictionary returned by GetBrowserInfo()
    for sample input value.

    Args:
      view: 'view' property of extension view.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation returns an error.
    """
    def _IsExtensionViewClosed():
      extension_views = self.GetBrowserInfo()['extension_views']
      for extension_view in extension_views:
        if view == extension_view['view']:
          return False
      return True

    return self.WaitUntil(lambda: _IsExtensionViewClosed())

  def GetPluginsInfo(self, windex=0):
    """Return info about plugins.

    This is the info available from about:plugins

    Returns:
      an instance of plugins_info.PluginsInfo
    """
    return plugins_info.PluginsInfo(
        self._GetResultFromJSONRequest({'command': 'GetPluginsInfo'},
                                       windex=windex))

  def EnablePlugin(self, path):
    """Enable the plugin at the given path.

    Use GetPluginsInfo() to fetch path info about a plugin.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
      'command': 'EnablePlugin',
      'path': path,
    }
    self._GetResultFromJSONRequest(cmd_dict)

  def DisablePlugin(self, path):
    """Disable the plugin at the given path.

    Use GetPluginsInfo() to fetch path info about a plugin.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
      'command': 'DisablePlugin',
      'path': path,
    }
    self._GetResultFromJSONRequest(cmd_dict)

  def GetTabContents(self, tab_index=0, window_index=0):
    """Get the html contents of a tab (a la "view source").

    As an implementation detail, this saves the html in a file, reads
    the file into a buffer, then deletes it.

    Args:
      tab_index: tab index, defaults to 0.
      window_index: window index, defaults to 0.
    Returns:
      html content of a page as a string.
    """
    tempdir = tempfile.mkdtemp()
    # Make it writable by chronos on chromeos
    os.chmod(tempdir, 0777)
    filename = os.path.join(tempdir, 'content.html')
    cmd_dict = {  # Prepare command for the json interface
      'command': 'SaveTabContents',
      'tab_index': tab_index,
      'filename': filename
    }
    self._GetResultFromJSONRequest(cmd_dict, windex=window_index)
    try:
      f = open(filename)
      all_data = f.read()
      f.close()
      return all_data
    finally:
      shutil.rmtree(tempdir, ignore_errors=True)

  def AddSavedPassword(self, password_dict, windex=0):
    """Adds the given username-password combination to the saved passwords.

    Args:
      password_dict: a dictionary that represents a password. Example:
      { 'username_value': 'user@example.com',        # Required
        'password_value': 'test.password',           # Required
        'signon_realm': 'https://www.example.com/',  # Required
        'time': 1279317810.0,                        # Can get from time.time()
        'origin_url': 'https://www.example.com/login',
        'username_element': 'username',              # The HTML element
        'password_element': 'password',              # The HTML element
        'submit_element': 'submit',                  # The HTML element
        'action_target': 'https://www.example.com/login/',
        'blacklist': False }
      windex: window index; defaults to 0 (first window).

    *Blacklist notes* To blacklist a site, add a blacklist password with the
    following dictionary items: origin_url, signon_realm, username_element,
    password_element, action_target, and 'blacklist': True. Then all sites that
    have password forms matching those are blacklisted.

    Returns:
      True if adding the password succeeded, false otherwise. In incognito
      mode, adding the password should fail.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {  # Prepare command for the json interface
      'command': 'AddSavedPassword',
      'password': password_dict
    }
    return self._GetResultFromJSONRequest(
        cmd_dict, windex=windex)['password_added']

  def RemoveSavedPassword(self, password_dict, windex=0):
    """Removes the password matching the provided password dictionary.

    Args:
      password_dict: A dictionary that represents a password.
                     For an example, see the dictionary in AddSavedPassword.
      windex: The window index, default is 0 (first window).
    """
    cmd_dict = {  # Prepare command for the json interface
      'command': 'RemoveSavedPassword',
      'password': password_dict
    }
    self._GetResultFromJSONRequest(cmd_dict, windex=windex)

  def GetSavedPasswords(self):
    """Return the passwords currently saved.

    Returns:
      A list of dictionaries representing each password. For an example
      dictionary see AddSavedPassword documentation. The overall structure will
      be:
      [ {password1 dictionary}, {password2 dictionary} ]
    """
    cmd_dict = {  # Prepare command for the json interface
      'command': 'GetSavedPasswords'
    }
    return self._GetResultFromJSONRequest(cmd_dict)['passwords']

  def SetTheme(self, crx_file_path, windex=0):
    """Installs the given theme synchronously.

    A theme file is a file with a .crx suffix, like an extension.  The theme
    file must be specified with an absolute path.  This method call waits until
    the theme is installed and will trigger the "theme installed" infobar.
    If the install is unsuccessful, will throw an exception.

    Uses InstallExtension().

    Returns:
      The ID of the installed theme.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    return self.InstallExtension(crx_file_path, True, windex)

  def GetActiveNotifications(self):
    """Gets a list of the currently active/shown HTML5 notifications.

    Returns:
      a list containing info about each active notification, with the
      first item in the list being the notification on the bottom of the
      notification stack. The 'content_url' key can refer to a URL or a data
      URI. The 'pid' key-value pair may be invalid if the notification is
      closing.

    SAMPLE:
    [ { u'content_url': u'data:text/html;charset=utf-8,%3C!DOCTYPE%l%3E%0Atm...'
        u'display_source': 'www.corp.google.com',
        u'origin_url': 'http://www.corp.google.com/',
        u'pid': 8505},
      { u'content_url': 'http://www.gmail.com/special_notification.html',
        u'display_source': 'www.gmail.com',
        u'origin_url': 'http://www.gmail.com/',
        u'pid': 9291}]

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    return [x for x in self.GetAllNotifications() if 'pid' in x]

  def GetAllNotifications(self):
    """Gets a list of all active and queued HTML5 notifications.

    An active notification is one that is currently shown to the user. Chrome's
    notification system will limit the number of notifications shown (currently
    by only allowing a certain percentage of the screen to be taken up by them).
    A notification will be queued if there are too many active notifications.
    Once other notifications are closed, another will be shown from the queue.

    Returns:
      a list containing info about each notification, with the first
      item in the list being the notification on the bottom of the
      notification stack. The 'content_url' key can refer to a URL or a data
      URI. The 'pid' key-value pair will only be present for active
      notifications.

    SAMPLE:
    [ { u'content_url': u'data:text/html;charset=utf-8,%3C!DOCTYPE%l%3E%0Atm...'
        u'display_source': 'www.corp.google.com',
        u'origin_url': 'http://www.corp.google.com/',
        u'pid': 8505},
      { u'content_url': 'http://www.gmail.com/special_notification.html',
        u'display_source': 'www.gmail.com',
        u'origin_url': 'http://www.gmail.com/'}]

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
      'command': 'GetAllNotifications',
    }
    return self._GetResultFromJSONRequest(cmd_dict)['notifications']

  def CloseNotification(self, index):
    """Closes the active HTML5 notification at the given index.

    Args:
      index: the index of the notification to close. 0 refers to the
             notification on the bottom of the notification stack.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
      'command': 'CloseNotification',
      'index': index,
    }
    return self._GetResultFromJSONRequest(cmd_dict)

  def WaitForNotificationCount(self, count):
    """Waits for the number of active HTML5 notifications to reach the given
    count.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
      'command': 'WaitForNotificationCount',
      'count': count,
    }
    self._GetResultFromJSONRequest(cmd_dict)

  def FindInPage(self, search_string, forward=True,
                 match_case=False, find_next=False,
                 tab_index=0, windex=0, timeout=-1):
    """Find the match count for the given search string and search parameters.
    This is equivalent to using the find box.

    Args:
      search_string: The string to find on the page.
      forward: Boolean to set if the search direction is forward or backwards
      match_case: Boolean to set for case sensitive search.
      find_next: Boolean to set to continue the search or start from beginning.
      tab_index: The tab index, default is 0.
      windex: The window index, default is 0.
      timeout: request timeout (in milliseconds), default is -1.

    Returns:
      number of matches found for the given search string and parameters
    SAMPLE:
    { u'match_count': 10,
      u'match_left': 100,
      u'match_top': 100,
      u'match_right': 200,
      u'match_bottom': 200}

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
      'command': 'FindInPage',
      'tab_index' : tab_index,
      'search_string' : search_string,
      'forward' : forward,
      'match_case' : match_case,
      'find_next' : find_next,
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=windex,
                                          timeout=timeout)

  def OpenFindInPage(self, windex=0):
    """Opens the "Find in Page" box.

    Args:
      windex: Index of the window; defaults to 0.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
      'command': 'OpenFindInPage',
      'windex' : windex,
    }
    self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def IsFindInPageVisible(self, windex=0):
    """Returns the visibility of the "Find in Page" box.

    Args:
      windex: Index of the window; defaults to 0.

    Returns:
      A boolean indicating the visibility state of the "Find in Page" box.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
      'command': 'IsFindInPageVisible',
      'windex' : windex,
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)['is_visible']


  def AddDomEventObserver(self, event_name='', automation_id=-1,
                          recurring=False):
    """Adds a DomEventObserver associated with the AutomationEventQueue.

    An app raises a matching event in Javascript by calling:
    window.domAutomationController.sendWithId(automation_id, event_name)

    Args:
      event_name: The event name to watch for. By default an event is raised
                  for any message.
      automation_id: The Automation Id of the sent message. By default all
                     messages sent from the window.domAutomationController are
                     observed. Note that other PyAuto functions also send
                     messages through window.domAutomationController with
                     arbirary Automation Ids and they will be observed.
      recurring: If False the observer will be removed after it generates one
                 event, otherwise it will continue observing and generating
                 events until explicity removed with RemoveEventObserver(id).

    Returns:
      The id of the created observer, which can be used with GetNextEvent(id)
      and RemoveEventObserver(id).

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
      'command': 'AddDomEventObserver',
      'event_name': event_name,
      'automation_id': automation_id,
      'recurring': recurring,
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)['observer_id']

  def AddDomMutationObserver(self, mutation_type, xpath,
                             attribute='textContent', expected_value=None,
                             automation_id=44444,
                             exec_js=None, **kwargs):
    """Sets up an event observer watching for a specific DOM mutation.

    Creates an observer that raises an event when a mutation of the given type
    occurs on a DOM node specified by |selector|.

    Args:
      mutation_type: One of 'add', 'remove', 'change', or 'exists'.
      xpath: An xpath specifying the DOM node to watch. The node must already
          exist if |mutation_type| is 'change'.
      attribute: Attribute to match |expected_value| against, if given. Defaults
          to 'textContent'.
      expected_value: Optional regular expression to match against the node's
          textContent attribute after the mutation. Defaults to None.
      automation_id: The automation_id used to route the observer javascript
          messages. Defaults to 44444.
      exec_js: A callable of the form f(self, js, **kwargs) used to inject the
          MutationObserver javascript. Defaults to None, which uses
          PyUITest.ExecuteJavascript.

      Any additional keyword arguments are passed on to ExecuteJavascript and
      can be used to select the tab where the DOM MutationObserver is created.

    Returns:
      The id of the created observer, which can be used with GetNextEvent(id)
      and RemoveEventObserver(id).

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
      pyauto_errors.JavascriptRuntimeError if the injected javascript
          MutationObserver returns an error.
    """
    assert mutation_type in ('add', 'remove', 'change', 'exists'), \
        'Unexpected value "%s" for mutation_type.' % mutation_type
    cmd_dict = {
      'command': 'AddDomEventObserver',
      'event_name': '__dom_mutation_observer__:$(id)',
      'automation_id': automation_id,
      'recurring': False,
    }
    observer_id = (
        self._GetResultFromJSONRequest(cmd_dict, windex=None)['observer_id'])
    expected_string = ('null' if expected_value is None else '"%s"' %
                       expected_value.replace('"', r'\"'))
    jsfile = os.path.join(os.path.abspath(os.path.dirname(__file__)),
                          'dom_mutation_observer.js')
    with open(jsfile, 'r') as f:
      js = ('(' + f.read() + ')(%d, %d, "%s", "%s", "%s", %s);' %
            (automation_id, observer_id, mutation_type,
             xpath.replace('"', r'\"'), attribute, expected_string))
    exec_js = exec_js or PyUITest.ExecuteJavascript
    try:
      jsreturn = exec_js(self, js, **kwargs)
    except JSONInterfaceError:
      raise JSONInterfaceError('Failed to inject DOM mutation observer.')
    if jsreturn != 'success':
      self.RemoveEventObserver(observer_id)
      raise JavascriptRuntimeError(jsreturn)
    return observer_id

  def WaitForDomNode(self, xpath, attribute='textContent',
                     expected_value=None, exec_js=None, timeout=-1,
                     msg='Expected DOM node failed to appear.', **kwargs):
    """Waits until a node specified by an xpath exists in the DOM.

    NOTE: This does NOT poll. It returns as soon as the node appears, or
      immediately if the node already exists.

    Args:
      xpath: An xpath specifying the DOM node to watch.
      attribute: Attribute to match |expected_value| against, if given. Defaults
          to 'textContent'.
      expected_value: Optional regular expression to match against the node's
          textContent attribute. Defaults to None.
      exec_js: A callable of the form f(self, js, **kwargs) used to inject the
          MutationObserver javascript. Defaults to None, which uses
          PyUITest.ExecuteJavascript.
      msg: An optional error message used if a JSONInterfaceError is caught
          while waiting for the DOM node to appear.
      timeout: Time to wait for the node to exist before raising an exception,
          defaults to the default automation timeout.

      Any additional keyword arguments are passed on to ExecuteJavascript and
      can be used to select the tab where the DOM MutationObserver is created.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
      pyauto_errors.JavascriptRuntimeError if the injected javascript
          MutationObserver returns an error.
    """
    observer_id = self.AddDomMutationObserver('exists', xpath, attribute,
                                              expected_value, exec_js=exec_js,
                                              **kwargs)
    try:
      self.GetNextEvent(observer_id, timeout=timeout)
    except JSONInterfaceError:
      raise JSONInterfaceError(msg)

  def GetNextEvent(self, observer_id=-1, blocking=True, timeout=-1):
    """Waits for an observed event to occur.

    The returned event is removed from the Event Queue. If there is already a
    matching event in the queue it is returned immediately, otherwise the call
    blocks until a matching event occurs. If blocking is disabled and no
    matching event is in the queue this function will immediately return None.

    Args:
      observer_id: The id of the observer to wait for, matches any event by
                   default.
      blocking: If True waits until there is a matching event in the queue,
                if False and there is no event waiting in the queue returns None
                immediately.
      timeout: Time to wait for a matching event, defaults to the default
               automation timeout.

    Returns:
      Event response dictionary, or None if blocking is disabled and there is no
      matching event in the queue.
      SAMPLE:
      { 'observer_id': 1,
        'name': 'login completed',
        'type': 'raised_event'}

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
      'command': 'GetNextEvent',
      'observer_id' : observer_id,
      'blocking' : blocking,
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None,
                                          timeout=timeout)

  def RemoveEventObserver(self, observer_id):
    """Removes an Event Observer from the AutomationEventQueue.

    Expects a valid observer_id.

    Args:
      observer_id: The id of the observer to remove.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
      'command': 'RemoveEventObserver',
      'observer_id' : observer_id,
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def ClearEventQueue(self):
    """Removes all events currently in the AutomationEventQueue.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
      'command': 'ClearEventQueue',
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def WaitUntilNavigationCompletes(self, tab_index=0, windex=0):
    """Wait until the specified tab is done navigating.

    It is safe to call ExecuteJavascript() as soon as the call returns. If
    there is no outstanding navigation the call will return immediately.

    Args:
      tab_index: index of the tab.
      windex: index of the window.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
      'command': 'WaitUntilNavigationCompletes',
      'tab_index': tab_index,
      'windex': windex,
    }
    return self._GetResultFromJSONRequest(cmd_dict)

  def ExecuteJavascript(self, js, tab_index=0, windex=0, frame_xpath=''):
    """Executes a script in the specified frame of a tab.

    By default, execute the script in the top frame of the first tab in the
    first window. The invoked javascript function must send a result back via
    the domAutomationController.send function, or this function will never
    return.

    Args:
      js: script to be executed.
      windex: index of the window.
      tab_index: index of the tab.
      frame_xpath: XPath of the frame to execute the script.  Default is no
      frame. Example: '//frames[1]'.

    Returns:
      a value that was sent back via the domAutomationController.send method

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
      'command': 'ExecuteJavascript',
      'javascript' : js,
      'windex' : windex,
      'tab_index' : tab_index,
      'frame_xpath' : frame_xpath,
    }
    result = self._GetResultFromJSONRequest(cmd_dict)['result']
    # Wrap result in an array before deserializing because valid JSON has an
    # array or an object as the root.
    json_string = '[' + result + ']'
    return json.loads(json_string)[0]

  def ExecuteJavascriptInRenderView(self, js, view, frame_xpath=''):
    """Executes a script in the specified frame of an render view.

    The invoked javascript function must send a result back via the
    domAutomationController.send function, or this function will never return.

    Args:
      js: script to be executed.
      view: A dictionary representing a unique id for the render view as
      returned for example by.
      self.GetBrowserInfo()['extension_views'][]['view'].
      Example:
      { 'render_process_id': 1,
        'render_view_id' : 2}

      frame_xpath: XPath of the frame to execute the script. Default is no
      frame. Example:
      '//frames[1]'

    Returns:
      a value that was sent back via the domAutomationController.send method

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
      'command': 'ExecuteJavascriptInRenderView',
      'javascript' : js,
      'view' : view,
      'frame_xpath' : frame_xpath,
    }
    result = self._GetResultFromJSONRequest(cmd_dict, windex=None)['result']
    # Wrap result in an array before deserializing because valid JSON has an
    # array or an object as the root.
    json_string = '[' + result + ']'
    return json.loads(json_string)[0]

  def ExecuteJavascriptInOOBEWebUI(self, js, frame_xpath=''):
    """Executes a script in the specified frame of the OOBE WebUI.

    By default, execute the script in the top frame of the OOBE window. This
    also works for all OOBE pages, including the enterprise enrollment
    screen and login page. The invoked javascript function must send a result
    back via the domAutomationController.send function, or this function will
    never return.

    Args:
      js: Script to be executed.
      frame_xpath: XPath of the frame to execute the script. Default is no
          frame. Example: '//frames[1]'

    Returns:
      A value that was sent back via the domAutomationController.send method.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
      'command': 'ExecuteJavascriptInOOBEWebUI',

      'javascript': js,
      'frame_xpath': frame_xpath,
    }
    result = self._GetResultFromJSONRequest(cmd_dict, windex=None)['result']
    # Wrap result in an array before deserializing because valid JSON has an
    # array or an object as the root.
    return json.loads('[' + result + ']')[0]


  def GetDOMValue(self, expr, tab_index=0, windex=0, frame_xpath=''):
    """Executes a Javascript expression and returns the value.

    This is a wrapper for ExecuteJavascript, eliminating the need to
    explicitly call domAutomationController.send function.

    Args:
      expr: expression value to be returned.
      tab_index: index of the tab.
      windex: index of the window.
      frame_xpath: XPath of the frame to execute the script.  Default is no
      frame. Example: '//frames[1]'.

    Returns:
      a string that was sent back via the domAutomationController.send method.
    """
    js = 'window.domAutomationController.send(%s);' % expr
    return self.ExecuteJavascript(js, tab_index, windex, frame_xpath)

  def CallJavascriptFunc(self, function, args=[], tab_index=0, windex=0):
    """Executes a script which calls a given javascript function.

    The invoked javascript function must send a result back via the
    domAutomationController.send function, or this function will never return.

    Defaults to first tab in first window.

    Args:
      function: name of the function.
      args: list of all the arguments to pass into the called function. These
            should be able to be converted to a string using the |str| function.
      tab_index: index of the tab within the given window.
      windex: index of the window.

    Returns:
      a string that was sent back via the domAutomationController.send method
    """
    converted_args = map(lambda arg: json.dumps(arg), args)
    js = '%s(%s)' % (function, ', '.join(converted_args))
    logging.debug('Executing javascript: %s', js)
    return self.ExecuteJavascript(js, tab_index, windex)

  def HeapProfilerDump(self, process_type, reason, tab_index=0, windex=0):
    """Dumps a heap profile. It works only on Linux and ChromeOS.

    We need an environment variable "HEAPPROFILE" set to a directory and a
    filename prefix, for example, "/tmp/prof".  In a case of this example,
    heap profiles will be dumped into "/tmp/prof.(pid).0002.heap",
    "/tmp/prof.(pid).0003.heap", and so on.  Nothing happens when this
    function is called without the env.

    Also, this requires the --enable-memory-benchmarking command line flag.

    Args:
      process_type: A string which is one of 'browser' or 'renderer'.
      reason: A string which describes the reason for dumping a heap profile.
              The reason will be included in the logged message.
              Examples:
                'To check memory leaking'
                'For PyAuto tests'
      tab_index: tab index to work on if 'process_type' == 'renderer'.
          Defaults to 0 (first tab).
      windex: window index to work on if 'process_type' == 'renderer'.
          Defaults to 0 (first window).

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    assert process_type in ('browser', 'renderer')
    if self.IsLinux():  # IsLinux() also implies IsChromeOS().
      js = """
          if (!chrome.memoryBenchmarking ||
              !chrome.memoryBenchmarking.isHeapProfilerRunning()) {
            domAutomationController.send('memory benchmarking disabled');
          } else {
            chrome.memoryBenchmarking.heapProfilerDump("%s", "%s");
            domAutomationController.send('success');
          }
      """ % (process_type, reason.replace('"', '\\"'))
      result = self.ExecuteJavascript(js, tab_index, windex)
      if result != 'success':
        raise JSONInterfaceError('Heap profiler dump failed: ' + result)
    else:
      logging.warn('Heap-profiling is not supported in this OS.')

  def GetNTPThumbnails(self):
    """Return a list of info about the sites in the NTP most visited section.
    SAMPLE:
      [{ u'title': u'Google',
         u'url': u'http://www.google.com'},
       {
         u'title': u'Yahoo',
         u'url': u'http://www.yahoo.com'}]
    """
    return self._GetNTPInfo()['most_visited']

  def GetNTPThumbnailIndex(self, thumbnail):
    """Returns the index of the given NTP thumbnail, or -1 if it is not shown.

    Args:
      thumbnail: a thumbnail dict received from |GetNTPThumbnails|
    """
    thumbnails = self.GetNTPThumbnails()
    for i in range(len(thumbnails)):
      if thumbnails[i]['url'] == thumbnail['url']:
        return i
    return -1

  def RemoveNTPThumbnail(self, thumbnail):
    """Removes the NTP thumbnail and returns true on success.

    Args:
      thumbnail: a thumbnail dict received from |GetNTPThumbnails|
    """
    self._CheckNTPThumbnailShown(thumbnail)
    cmd_dict = {
      'command': 'RemoveNTPMostVisitedThumbnail',
      'url': thumbnail['url']
    }
    self._GetResultFromJSONRequest(cmd_dict)

  def RestoreAllNTPThumbnails(self):
    """Restores all the removed NTP thumbnails.
    Note:
      the default thumbnails may come back into the Most Visited sites
      section after doing this
    """
    cmd_dict = {
      'command': 'RestoreAllNTPMostVisitedThumbnails'
    }
    self._GetResultFromJSONRequest(cmd_dict)

  def GetNTPDefaultSites(self):
    """Returns a list of URLs for all the default NTP sites, regardless of
    whether they are showing or not.

    These sites are the ones present in the NTP on a fresh install of Chrome.
    """
    return self._GetNTPInfo()['default_sites']

  def RemoveNTPDefaultThumbnails(self):
    """Removes all thumbnails for default NTP sites, regardless of whether they
    are showing or not."""
    cmd_dict = { 'command': 'RemoveNTPMostVisitedThumbnail' }
    for site in self.GetNTPDefaultSites():
      cmd_dict['url'] = site
      self._GetResultFromJSONRequest(cmd_dict)

  def GetNTPRecentlyClosed(self):
    """Return a list of info about the items in the NTP recently closed section.
    SAMPLE:
      [{
         u'type': u'tab',
         u'url': u'http://www.bing.com',
         u'title': u'Bing',
         u'timestamp': 2139082.03912,  # Seconds since epoch (Jan 1, 1970)
         u'direction': u'ltr'},
       {
         u'type': u'window',
         u'timestamp': 2130821.90812,
         u'tabs': [
         {
           u'type': u'tab',
           u'url': u'http://www.cnn.com',
           u'title': u'CNN',
           u'timestamp': 2129082.12098,
           u'direction': u'ltr'}]},
       {
         u'type': u'tab',
         u'url': u'http://www.altavista.com',
         u'title': u'Altavista',
         u'timestamp': 21390820.12903,
         u'direction': u'rtl'}]
    """
    return self._GetNTPInfo()['recently_closed']

  def GetNTPApps(self):
    """Retrieves information about the apps listed on the NTP.

    In the sample data below, the "launch_type" will be one of the following
    strings: "pinned", "regular", "fullscreen", "window", or "unknown".

    SAMPLE:
    [
      {
        u'app_launch_index': 2,
        u'description': u'Web Store',
        u'icon_big': u'chrome://theme/IDR_APP_DEFAULT_ICON',
        u'icon_small': u'chrome://favicon/https://chrome.google.com/webstore',
        u'id': u'ahfgeienlihckogmohjhadlkjgocpleb',
        u'is_component_extension': True,
        u'is_disabled': False,
        u'launch_container': 2,
        u'launch_type': u'regular',
        u'launch_url': u'https://chrome.google.com/webstore',
        u'name': u'Chrome Web Store',
        u'options_url': u'',
      },
      {
        u'app_launch_index': 1,
        u'description': u'A countdown app',
        u'icon_big': (u'chrome-extension://aeabikdlfbfeihglecobdkdflahfgcpd/'
                      u'countdown128.png'),
        u'icon_small': (u'chrome://favicon/chrome-extension://'
                        u'aeabikdlfbfeihglecobdkdflahfgcpd/'
                        u'launchLocalPath.html'),
        u'id': u'aeabikdlfbfeihglecobdkdflahfgcpd',
        u'is_component_extension': False,
        u'is_disabled': False,
        u'launch_container': 2,
        u'launch_type': u'regular',
        u'launch_url': (u'chrome-extension://aeabikdlfbfeihglecobdkdflahfgcpd/'
                        u'launchLocalPath.html'),
        u'name': u'Countdown',
        u'options_url': u'',
      }
    ]

    Returns:
      A list of dictionaries in which each dictionary contains the information
      for a single app that appears in the "Apps" section of the NTP.
    """
    return self._GetNTPInfo()['apps']

  def _GetNTPInfo(self):
    """Get info about the New Tab Page (NTP).

    This does not retrieve the actual info displayed in a particular NTP; it
    retrieves the current state of internal data that would be used to display
    an NTP.  This includes info about the apps, the most visited sites,
    the recently closed tabs and windows, and the default NTP sites.

    SAMPLE:
    {
      u'apps': [ ... ],
      u'most_visited': [ ... ],
      u'recently_closed': [ ... ],
      u'default_sites': [ ... ]
    }

    Returns:
      A dictionary containing all the NTP info. See details about the different
      sections in their respective methods: GetNTPApps(), GetNTPThumbnails(),
      GetNTPRecentlyClosed(), and GetNTPDefaultSites().

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
      'command': 'GetNTPInfo',
    }
    return self._GetResultFromJSONRequest(cmd_dict)

  def _CheckNTPThumbnailShown(self, thumbnail):
    if self.GetNTPThumbnailIndex(thumbnail) == -1:
      raise NTPThumbnailNotShownError()

  def LaunchApp(self, app_id, windex=0):
    """Opens the New Tab Page and launches the specified app from it.

    This method will not return until after the contents of a new tab for the
    launched app have stopped loading.

    Args:
      app_id: The string ID of the app to launch.
      windex: The index of the browser window to work on.  Defaults to 0 (the
              first window).

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    self.AppendTab(GURL('chrome://newtab'), windex)  # Also activates this tab.
    cmd_dict = {
      'command': 'LaunchApp',
      'id': app_id,
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=windex)

  def SetAppLaunchType(self, app_id, launch_type, windex=0):
    """Sets the launch type for the specified app.

    Args:
      app_id: The string ID of the app whose launch type should be set.
      launch_type: The string launch type, which must be one of the following:
                   'pinned': Launch in a pinned tab.
                   'regular': Launch in a regular tab.
                   'fullscreen': Launch in a fullscreen tab.
                   'window': Launch in a new browser window.
      windex: The index of the browser window to work on.  Defaults to 0 (the
              first window).

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    self.assertTrue(launch_type in ('pinned', 'regular', 'fullscreen',
                                    'window'),
                    msg='Unexpected launch type value: "%s"' % launch_type)
    cmd_dict = {
      'command': 'SetAppLaunchType',
      'id': app_id,
      'launch_type': launch_type,
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=windex)

  def GetV8HeapStats(self, tab_index=0, windex=0):
    """Returns statistics about the v8 heap in the renderer process for a tab.

    Args:
      tab_index: The tab index, default is 0.
      window_index: The window index, default is 0.

    Returns:
      A dictionary containing v8 heap statistics. Memory values are in bytes.
      Example:
        { 'renderer_id': 6223,
          'v8_memory_allocated': 21803776,
          'v8_memory_used': 10565392 }
    """
    cmd_dict = {  # Prepare command for the json interface.
      'command': 'GetV8HeapStats',
      'tab_index': tab_index,
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=windex)

  def GetFPS(self, tab_index=0, windex=0):
    """Returns the current FPS associated with the renderer process for a tab.

    FPS is the rendered frames per second.

    Args:
      tab_index: The tab index, default is 0.
      window_index: The window index, default is 0.

    Returns:
      A dictionary containing FPS info.
      Example:
        { 'renderer_id': 23567,
          'routing_id': 1,
          'fps': 29.404298782348633 }
    """
    cmd_dict = {  # Prepare command for the json interface.
      'command': 'GetFPS',
      'tab_index': tab_index,
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=windex)

  def IsFullscreenForBrowser(self, windex=0):
    """Returns true if the window is currently fullscreen and was initially
    transitioned to fullscreen by a browser (vs tab) mode transition."""
    return self._GetResultFromJSONRequest(
      { 'command': 'IsFullscreenForBrowser' },
      windex=windex).get('result')

  def IsFullscreenForTab(self, windex=0):
    """Returns true if fullscreen has been caused by a tab."""
    return self._GetResultFromJSONRequest(
      { 'command': 'IsFullscreenForTab' },
      windex=windex).get('result')

  def IsMouseLocked(self, windex=0):
    """Returns true if the mouse is currently locked."""
    return self._GetResultFromJSONRequest(
      { 'command': 'IsMouseLocked' },
      windex=windex).get('result')

  def IsMouseLockPermissionRequested(self, windex=0):
    """Returns true if the user is currently prompted to give permision for
    mouse lock."""
    return self._GetResultFromJSONRequest(
      { 'command': 'IsMouseLockPermissionRequested' },
      windex=windex).get('result')

  def IsFullscreenPermissionRequested(self, windex=0):
    """Returns true if the user is currently prompted to give permision for
    fullscreen."""
    return self._GetResultFromJSONRequest(
      { 'command': 'IsFullscreenPermissionRequested' },
      windex=windex).get('result')

  def IsFullscreenBubbleDisplayed(self, windex=0):
    """Returns true if the fullscreen and mouse lock bubble is currently
    displayed."""
    return self._GetResultFromJSONRequest(
      { 'command': 'IsFullscreenBubbleDisplayed' },
      windex=windex).get('result')

  def IsFullscreenBubbleDisplayingButtons(self, windex=0):
    """Returns true if the fullscreen and mouse lock bubble is currently
    displayed and presenting buttons."""
    return self._GetResultFromJSONRequest(
      { 'command': 'IsFullscreenBubbleDisplayingButtons' },
      windex=windex).get('result')

  def AcceptCurrentFullscreenOrMouseLockRequest(self, windex=0):
    """Activate the accept button on the fullscreen and mouse lock bubble."""
    return self._GetResultFromJSONRequest(
      { 'command': 'AcceptCurrentFullscreenOrMouseLockRequest' },
      windex=windex)

  def DenyCurrentFullscreenOrMouseLockRequest(self, windex=0):
    """Activate the deny button on the fullscreen and mouse lock bubble."""
    return self._GetResultFromJSONRequest(
      { 'command': 'DenyCurrentFullscreenOrMouseLockRequest' },
      windex=windex)

  def KillRendererProcess(self, pid):
    """Kills the given renderer process.

    This will return only after the browser has received notice of the renderer
    close.

    Args:
      pid: the process id of the renderer to kill

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
        'command': 'KillRendererProcess',
        'pid': pid
    }
    return self._GetResultFromJSONRequest(cmd_dict)

  def NewWebDriver(self, port=0):
    """Returns a new remote WebDriver instance.

    Args:
      port: The port to start WebDriver on; by default the service selects an
            open port. It is an error to request a port number and request a
            different port later.

    Returns:
      selenium.webdriver.remote.webdriver.WebDriver instance
    """
    from chrome_driver_factory import ChromeDriverFactory
    global _CHROME_DRIVER_FACTORY
    if _CHROME_DRIVER_FACTORY is None:
      _CHROME_DRIVER_FACTORY = ChromeDriverFactory(port=port)
    self.assertTrue(_CHROME_DRIVER_FACTORY.GetPort() == port or port == 0,
                    msg='Requested a WebDriver on a specific port while already'
                        ' running on a different port.')
    return _CHROME_DRIVER_FACTORY.NewChromeDriver(self)

  def CreateNewAutomationProvider(self, channel_id):
    """Creates a new automation provider.

    The provider will open a named channel in server mode.
    Args:
      channel_id: the channel_id to open the server channel with
    """
    cmd_dict = {
        'command': 'CreateNewAutomationProvider',
        'channel_id': channel_id
    }
    self._GetResultFromJSONRequest(cmd_dict)

  def OpenNewBrowserWindowWithNewProfile(self):
    """Creates a new multi-profiles user, and then opens and shows a new
    tabbed browser window with the new profile.

    This is equivalent to 'Add new user' action with multi-profiles.

    To account for crbug.com/108761 on Win XP, this call polls until the
    profile count increments by 1.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    num_profiles = len(self.GetMultiProfileInfo()['profiles'])
    cmd_dict = {  # Prepare command for the json interface
      'command': 'OpenNewBrowserWindowWithNewProfile'
    }
    self._GetResultFromJSONRequest(cmd_dict, windex=None)
    # TODO(nirnimesh): Remove when crbug.com/108761 is fixed
    self.WaitUntil(
        lambda: len(self.GetMultiProfileInfo()['profiles']),
        expect_retval=(num_profiles + 1))

  def OpenProfileWindow(self, path, num_loads=1):
   """Open browser window for an existing profile.

   This is equivalent to picking a profile from the multi-profile menu.

   Multi-profile should be enabled and the requested profile should already
   exist. Creates a new window for the given profile. Use
   OpenNewBrowserWindowWithNewProfile() to create a new profile.

   Args:
     path: profile path of the profile to be opened.
     num_loads: the number of loads to wait for, when a new browser window
                is created.  Useful when restoring a window with many tabs.
   """
   cmd_dict = {  # Prepare command for the json interface
    'command': 'OpenProfileWindow',
    'path': path,
    'num_loads': num_loads,
   }
   return self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def GetMultiProfileInfo(self):
    """Fetch info about all multi-profile users.

    Returns:
      A dictionary.
      Sample:
      {
        'enabled': True,
        'profiles': [{'name': 'First user',
                      'path': '/tmp/.org.chromium.Chromium.Tyx17X/Default'},
                     {'name': 'User 1',
                      'path': '/tmp/.org.chromium.Chromium.Tyx17X/profile_1'}],
      }

      Profiles will be listed in the same order as visible in preferences.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {  # Prepare command for the json interface
      'command': 'GetMultiProfileInfo'
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def RefreshPolicies(self):
    """Refreshes all the available policy providers.

    Each policy provider will reload its policy source and push the updated
    policies. This call waits for the new policies to be applied; any policies
    installed before this call is issued are guaranteed to be ready after it
    returns.
    """
    # TODO(craigdh): Determine the root cause of RefreshPolicies' flakiness.
    #                See crosbug.com/30221
    timeout = PyUITest.ActionTimeoutChanger(self, 3 * 60 * 1000)
    cmd_dict = { 'command': 'RefreshPolicies' }
    self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def SubmitForm(self, form_id, tab_index=0, windex=0, frame_xpath=''):
    """Submits the given form ID, and returns after it has been submitted.

    Args:
      form_id: the id attribute of the form to submit.

    Returns: true on success.
    """
    js = """
        document.getElementById("%s").submit();
        window.addEventListener("unload", function() {
          window.domAutomationController.send("done");
        });
    """ % form_id
    if self.ExecuteJavascript(js, tab_index, windex, frame_xpath) != 'done':
      return False
    # Wait until the form is submitted and the page completes loading.
    return self.WaitUntil(
        lambda: self.GetDOMValue('document.readyState',
                                 tab_index, windex, frame_xpath),
        expect_retval='complete')

  def SimulateAsanMemoryBug(self):
    """Simulates a memory bug for Address Sanitizer to catch.

    Address Sanitizer (if it was built it) will catch the bug and abort
    the process.
    This method returns immediately before it actually causes a crash.
    """
    cmd_dict = { 'command': 'SimulateAsanMemoryBug' }
    self._GetResultFromJSONRequest(cmd_dict, windex=None)

  ## ChromeOS section

  def GetLoginInfo(self):
    """Returns information about login and screen locker state.

    This includes things like whether a user is logged in, the username
    of the logged in user, and whether the screen is locked.

    Returns:
      A dictionary.
      Sample:
      { u'is_guest': False,
        u'is_owner': True,
        u'email': u'example@gmail.com',
        u'user_image': 2,  # non-negative int, 'profile', 'file'
        u'is_screen_locked': False,
        u'login_ui_type': 'nativeui', # or 'webui'
        u'is_logged_in': True}

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = { 'command': 'GetLoginInfo' }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def WaitForSessionManagerRestart(self, function):
    """Call a function and wait for the ChromeOS session_manager to restart.

    Args:
      function: The function to call.
    """
    assert callable(function)
    pgrep_process = subprocess.Popen(['pgrep', 'session_manager'],
                                     stdout=subprocess.PIPE)
    old_pid = pgrep_process.communicate()[0].strip()
    function()
    return self.WaitUntil(lambda: self._IsSessionManagerReady(old_pid))

  def _WaitForInodeChange(self, path, function):
    """Call a function and wait for the specified file path to change.

    Args:
      path: The file path to check for changes.
      function: The function to call.
    """
    assert callable(function)
    old_inode = os.stat(path).st_ino
    function()
    return self.WaitUntil(lambda: self._IsInodeNew(path, old_inode))

  def ShowCreateAccountUI(self):
    """Go to the account creation page.

    This is the same as clicking the "Create Account" link on the
    ChromeOS login screen. Does not actually create a new account.
    Should be displaying the login screen to work.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = { 'command': 'ShowCreateAccountUI' }
    # See note below under LoginAsGuest(). ShowCreateAccountUI() logs
    # the user in as guest in order to access the account creation page.
    assert self._WaitForInodeChange(
        self._named_channel_id,
        lambda: self._GetResultFromJSONRequest(cmd_dict, windex=None)), \
        'Chrome did not reopen the testing channel after login as guest.'
    self.SetUp()

  def SkipToLogin(self, skip_image_selection=True):
    """Skips OOBE to the login screen.

    Assumes that we're at the beginning of OOBE.

    Args:
      skip_image_selection: Boolean indicating whether the user image selection
                            screen should also be skipped.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = { 'command': 'SkipToLogin',
                 'skip_image_selection': skip_image_selection }
    result = self._GetResultFromJSONRequest(cmd_dict, windex=None)
    assert result['next_screen'] == 'login', 'Unexpected wizard transition'

  def GetOOBEScreenInfo(self):
    """Queries info about the current OOBE screen.

    Returns:
      A dictionary with the following keys:

      'screen_name': The title of the current OOBE screen as a string.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = { 'command': 'GetOOBEScreenInfo' }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def AcceptOOBENetworkScreen(self):
    """Accepts OOBE network screen and advances to the next one.

    Assumes that we're already at the OOBE network screen.

    Returns:
      A dictionary with the following keys:

      'next_screen': The title of the next OOBE screen as a string.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = { 'command': 'AcceptOOBENetworkScreen' }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def AcceptOOBEEula(self, accepted, usage_stats_reporting=False):
    """Accepts OOBE EULA and advances to the next screen.

    Assumes that we're already at the OOBE EULA screen.

    Args:
      accepted: Boolean indicating whether the EULA should be accepted.
      usage_stats_reporting: Boolean indicating whether UMA should be enabled.

    Returns:
      A dictionary with the following keys:

      'next_screen': The title of the next OOBE screen as a string.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = { 'command': 'AcceptOOBEEula',
                 'accepted': accepted,
                 'usage_stats_reporting': usage_stats_reporting }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def CancelOOBEUpdate(self):
    """Skips update on OOBE and advances to the next screen.

    Returns:
      A dictionary with the following keys:

      'next_screen': The title of the next OOBE screen as a string.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = { 'command': 'CancelOOBEUpdate' }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def PickUserImage(self, image):
    """Chooses image for the newly created user.

    Should be called immediately after login.

    Args:
      image_type: type of user image to choose. Possible values:
        - "profile": Google profile image
        - non-negative int: one of the default images

    Returns:
      A dictionary with the following keys:

      'next_screen': The title of the next OOBE screen as a string.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = { 'command': 'PickUserImage',
                 'image': image }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def LoginAsGuest(self):
    """Login to chromeos as a guest user.

    Waits until logged in.
    Should be displaying the login screen to work.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = { 'command': 'LoginAsGuest' }
    # Currently, logging in as guest causes session_manager to
    # restart Chrome, which will close the testing channel.
    # We need to call SetUp() again to reconnect to the new channel.
    assert self._WaitForInodeChange(
        self._named_channel_id,
        lambda: self._GetResultFromJSONRequest(cmd_dict, windex=None)), \
        'Chrome did not reopen the testing channel after login as guest.'
    self.SetUp()

  def Login(self, username, password, timeout=120 * 1000):
    """Login to chromeos.

    Waits until logged in and browser is ready.
    Should be displaying the login screen to work.

    Note that in case of webui auth-extension-based login, gaia auth errors
    will not be noticed here, because the browser has no knowledge of it. In
    this case the GetNextEvent automation command will always time out.

    Args:
      username: the username to log in as.
      password: the user's password.
      timeout: timeout in ms; defaults to two minutes.

    Returns:
      An error string if an error occured.
      None otherwise.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    self._GetResultFromJSONRequest({'command': 'AddLoginEventObserver'},
                                   windex=None)
    cmd_dict = {
        'command': 'SubmitLoginForm',
        'username': username,
        'password': password,
    }
    self._GetResultFromJSONRequest(cmd_dict, windex=None)
    self.AddDomEventObserver('loginfail', automation_id=4444)
    try:
      if self.GetNextEvent(timeout=timeout).get('name') == 'loginfail':
        raise JSONInterfaceError('Login denied by auth server.')
    except JSONInterfaceError as e:
      raise JSONInterfaceError('Login failed. Perhaps Chrome crashed, '
                               'failed to start, or the login flow is '
                               'broken? Error message: %s' % str(e))

  def Logout(self):
    """Log out from ChromeOS and wait for session_manager to come up.

    This is equivalent to pressing the 'Sign out' button from the
    aura shell tray when logged in.

    Should be logged in to work. Re-initializes the automation channel
    after logout.
    """
    clear_profile_orig = self.get_clear_profile()
    self.set_clear_profile(False)
    assert self.GetLoginInfo()['is_logged_in'], \
        'Trying to log out when already logged out.'
    def _SignOut():
      cmd_dict = { 'command': 'SignOut' }
      self._GetResultFromJSONRequest(cmd_dict, windex=None)
    assert self.WaitForSessionManagerRestart(_SignOut), \
        'Session manager did not restart after logout.'
    self.__SetUp()
    self.set_clear_profile(clear_profile_orig)

  def LockScreen(self):
    """Locks the screen on chromeos.

    Waits until screen is locked.
    Should be logged in and screen should not be locked to work.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = { 'command': 'LockScreen' }
    self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def UnlockScreen(self, password):
    """Unlocks the screen on chromeos, authenticating the user's password first.

    Waits until screen is unlocked.
    Screen locker should be active for this to work.

    Returns:
      An error string if an error occured.
      None otherwise.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
        'command': 'UnlockScreen',
        'password': password,
    }
    result = self._GetResultFromJSONRequest(cmd_dict, windex=None)
    return result.get('error_string')

  def SignoutInScreenLocker(self):
    """Signs out of chromeos using the screen locker's "Sign out" feature.

    Effectively the same as clicking the "Sign out" link on the screen locker.
    Screen should be locked for this to work.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = { 'command': 'SignoutInScreenLocker' }
    assert self.WaitForSessionManagerRestart(
        lambda: self._GetResultFromJSONRequest(cmd_dict, windex=None)), \
        'Session manager did not restart after logout.'
    self.__SetUp()

  def GetBatteryInfo(self):
    """Get details about battery state.

    Returns:
      A dictionary with the following keys:

      'battery_is_present': bool
      'line_power_on': bool
      if 'battery_is_present':
        'battery_percentage': float (0 ~ 100)
        'battery_fully_charged': bool
        if 'line_power_on':
          'battery_time_to_full': int (seconds)
        else:
          'battery_time_to_empty': int (seconds)

      If it is still calculating the time left, 'battery_time_to_full'
      and 'battery_time_to_empty' will be absent.

      Use 'battery_fully_charged' instead of 'battery_percentage'
      or 'battery_time_to_full' to determine whether the battery
      is fully charged, since the percentage is only approximate.

      Sample:
        { u'battery_is_present': True,
          u'line_power_on': False,
          u'battery_time_to_empty': 29617,
          u'battery_percentage': 100.0,
          u'battery_fully_charged': False }

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = { 'command': 'GetBatteryInfo' }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def GetPanelInfo(self):
    """Get details about open ChromeOS panels.

    A panel is actually a type of browser window, so all of
    this information is also available using GetBrowserInfo().

    Returns:
      A dictionary.
      Sample:
      [{ 'incognito': False,
         'renderer_pid': 4820,
         'title': u'Downloads',
         'url': u'chrome://active-downloads/'}]

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    panels = []
    for browser in self.GetBrowserInfo()['windows']:
      if browser['type'] != 'panel':
        continue

      panel = {}
      panels.append(panel)
      tab = browser['tabs'][0]
      panel['incognito'] = browser['incognito']
      panel['renderer_pid'] = tab['renderer_pid']
      panel['title'] = self.GetActiveTabTitle(browser['index'])
      panel['url'] = tab['url']

    return panels

  def RestoreOnline(self):
    """Returns the device from offline mode if GoOffline was used."""

    assert PyUITest.IsChromeOS()

    # Restores etherent connection
    stdout, stderr = self.RunSuperuserActionOnChromeOS('TeardownBackchannel')

    if hasattr(self, 'bc_cellular_enabled') and self.bc_cellular_enabled:
      self.ToggleNetworkDevice('cellular', True)
    if hasattr(self, 'bc_wifi_enabled') and self.bc_wifi_enabled:
      self.ToggleNetworkDevice('wifi', True)

    assert 'RuntimeError' not in stderr, stderr

  def GoOffline(self):
    """Puts device in offline mode.

    The device is put into offline mode by disabling all network interfaces
    but keeping the the wired ethernet interface up and faking shill/flimflam
    into thinking there is no ethernet interface by renaming the interface.
    This is so we can keep ssh connections over the wired connection alive.
    """
    assert PyUITest.IsChromeOS()
    net_info = self.GetNetworkInfo()
    self.bc_wifi_enabled = net_info.get('wifi_enabled')
    self.bc_cellular_enabled = net_info.get('cellular_enabled')

    if self.bc_cellular_enabled:
      self.ToggleNetworkDevice('cellular', False)
    if self.bc_wifi_enabled:
      self.ToggleNetworkDevice('wifi', False)

    stdout, stderr = self.RunSuperuserActionOnChromeOS('SetupBackchannel')
    assert 'RuntimeError' not in stderr, stderr

  def GetNetworkInfo(self):
    """Get details about ethernet, wifi, and cellular networks on chromeos.

    Returns:
      A dictionary.
      Sample:
      { u'cellular_available': True,
        u'cellular_enabled': False,
        u'connected_ethernet': u'/service/ethernet_abcd',
        u'connected_wifi': u'/service/wifi_abcd_1234_managed_none',
        u'ethernet_available': True,
        u'ethernet_enabled': True,
        u'ethernet_networks':
            { u'/service/ethernet_abcd':
                { u'device_path': u'/device/abcdeth',
                  u'name': u'',
                  u'service_path':
                  u'/profile/default/ethernet_abcd',
                  u'status': u'Connected'}
              u'network_type': pyautolib.TYPE_ETHERNET },
        u'remembered_wifi':
            { u'/service/wifi_abcd_1234_managed_none':
                { u'device_path': u'',
                  u'encrypted': False,
                  u'encryption': u'',
                  u'name': u'WifiNetworkName1',
                  u'status': u'Unknown',
                  u'strength': 0},
              u'network_type': pyautolib.TYPE_WIFI
            },
        u'wifi_available': True,
        u'wifi_enabled': True,
        u'wifi_networks':
            { u'/service/wifi_abcd_1234_managed_none':
                { u'device_path': u'/device/abcdwifi',
                  u'encrypted': False,
                  u'encryption': u'',
                  u'name': u'WifiNetworkName1',
                  u'status': u'Connected',
                  u'strength': 76},
              u'/service/wifi_abcd_1234_managed_802_1x':
                  { u'encrypted': True,
                    u'encryption': u'8021X',
                    u'name': u'WifiNetworkName2',
                    u'status': u'Idle',
                    u'strength': 79}
              u'network_type': pyautolib.TYPE_WIFI }}


    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = { 'command': 'GetNetworkInfo' }
    network_info = self._GetResultFromJSONRequest(cmd_dict, windex=None)

    # Remembered networks do not have /service/ prepended to the service path
    # even though wifi_networks does.  We want this prepended to allow for
    # consistency and easy string comparison with wifi_networks.
    remembered_wifi = {}
    network_info['remembered_wifi'] = dict([('/service/' + k, v) for k, v in
      network_info['remembered_wifi'].iteritems()])

    return network_info

  def GetConnectedWifi(self):
    """Returns the SSID of the currently connected wifi network.

    Returns:
      The SSID of the connected network or None if we're not connected.
    """
    service_list = self.GetNetworkInfo()
    connected_service_path = service_list.get('connected_wifi')
    if 'wifi_networks' in service_list and \
       connected_service_path in service_list['wifi_networks']:
       return service_list['wifi_networks'][connected_service_path]['name']

  def GetServicePath(self, ssid, encryption=None, timeout=30):
    """Waits until the SSID is observed and returns its service path.

    Args:
      ssid: String defining the SSID we are searching for.
      encryption: Encryption type of the network; either None to return the
                  first instance of network that matches the ssid, '' for
                  an empty network, 'PSK', 'WEP' or '8021X'.
      timeout: Duration to wait for ssid to appear.

    Returns:
      The service path or None if SSID does not exist after timeout period.
    """
    def _GetServicePath():
      service_list = self.GetNetworkInfo().get('wifi_networks', [])
      for service_path, service_obj in service_list.iteritems():
        if not (isinstance(service_obj, dict) and
                'encryption' in service_obj and
                'name' in service_obj):
          continue

        service_encr = 'PSK' if service_obj['encryption'] in ['WPA', 'RSN']\
                       else service_obj['encryption']

        if service_obj['name'] == ssid and \
           (encryption == None or service_encr == encryption):
          return service_path
      self.NetworkScan()
      return None

    service_path = self.WaitUntil(_GetServicePath, timeout=timeout,
                                  retry_sleep=1, return_retval=True)
    return service_path or None

  def NetworkScan(self):
    """Causes ChromeOS to scan for available wifi networks.

    Blocks until scanning is complete.

    Returns:
      The new list of networks obtained from GetNetworkInfo().

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = { 'command': 'NetworkScan' }
    self._GetResultFromJSONRequest(cmd_dict, windex=None)
    return self.GetNetworkInfo()

  def ToggleNetworkDevice(self, device, enable):
    """Enable or disable a network device on ChromeOS.

    Valid device names are ethernet, wifi, cellular.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
        'command': 'ToggleNetworkDevice',
        'device': device,
        'enable': enable,
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)

  PROXY_TYPE_DIRECT = 1
  PROXY_TYPE_MANUAL = 2
  PROXY_TYPE_PAC = 3

  def WaitUntilWifiNetworkAvailable(self, ssid, timeout=60, is_hidden=False):
    """Waits until the given network is available.

    Routers that are just turned on may take up to 1 minute upon turning them
    on to broadcast their SSID.

    Args:
      ssid: SSID of the service we want to connect to.
      timeout: timeout (in seconds)

    Raises:
      Exception if timeout duration has been hit before wifi router is seen.

    Returns:
      True, when the wifi network is seen within the timout period.
      False, otherwise.
    """
    def _GotWifiNetwork():
      # Returns non-empty array if desired SSID is available.
      try:
        return [wifi for wifi in
                self.NetworkScan().get('wifi_networks', {}).values()
                if wifi.get('name') == ssid]
      except pyauto_errors.JSONInterfaceError:
        # Temporary fix until crosbug.com/14174 is fixed.
        # NetworkScan is only used in updating the list of networks so errors
        # thrown by it are not critical to the results of wifi tests that use
        # this method.
        return False

    # The hidden AP's will always be on, thus we will assume it is ready to
    # connect to.
    if is_hidden:
      return bool(_GotWifiNetwork())

    return self.WaitUntil(_GotWifiNetwork, timeout=timeout, retry_sleep=1)

  def ResetProxySettingsOnChromeOS(self):
    """Public wrapper around proxysettings teardown functions."""
    self.SetSharedProxies(False)
    proxy_dict = {
        'mode': 'direct'
    }
    self.SetProxySettingOnChromeOS(proxy_dict)

  def SetProxySettingOnChromeOS(self, proxy_config):
    """Set the proxy config of the current network.

    Owner must be logged in for these to persist.
    If user is not logged in or is logged in as non-owner or guest,
    proxy settings do not persist across browser restarts or login/logout.

    Args:
      proxy_config:   A dictionary following the format described in
                      prefs/proxy_config_dictionary.h.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
        'command': 'SetProxySettings',
        'proxy_config': json.dumps(proxy_config)
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def SetSharedProxies(self, value):
    """Allows proxies on the shared networks.

    Args:
      value: True/False to set and clear respectively.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
        'command': 'SetSharedProxies',
        'value': value,
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def ForgetAllRememberedNetworks(self):
    """Forgets all networks that the device has marked as remembered."""
    for service in self.GetNetworkInfo()['remembered_wifi']:
      self.ForgetWifiNetwork(service)

  def ForgetWifiNetwork(self, service_path):
    """Forget a remembered network by its service path.

    This function is equivalent to clicking the 'Forget Network' button in the
    chrome://settings/internet page.  This function does not indicate whether
    or not forget succeeded or failed.  It is up to the caller to call
    GetNetworkInfo to check the updated remembered_wifi list to verify the
    service has been removed.

    Args:
      service_path: Flimflam path that defines the remembered network.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    # Usually the service_path is prepended with '/service/', such as when the
    # service path is retrieved from GetNetworkInfo.  ForgetWifiNetwork works
    # only for service paths where this has already been stripped.
    service_path = service_path.split('/service/')[-1]
    cmd_dict = {
        'command': 'ForgetWifiNetwork',
        'service_path': service_path,
    }
    self._GetResultFromJSONRequest(cmd_dict, windex=None, timeout=50000)

  def ConnectToCellularNetwork(self):
    """Connects to the available cellular network.

    Blocks until connection succeeds or fails.

    Returns:
      An error string if an error occured.
      None otherwise.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    # Every device should only have one cellular network present, so we can
    # scan for it.
    cellular_networks = self.NetworkScan().get('cellular_networks', {}).keys()
    self.assertTrue(cellular_networks, 'Could not find cellular service.')
    service_path = cellular_networks[0]

    cmd_dict = {
        'command': 'ConnectToCellularNetwork',
        'service_path': service_path,
    }
    result = self._GetResultFromJSONRequest(
        cmd_dict, windex=None, timeout=50000)
    return result.get('error_string')

  def DisconnectFromCellularNetwork(self):
    """Disconnect from the connected cellular network.

    Blocks until disconnect is complete.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
        'command': 'DisconnectFromCellularNetwork',
    }
    self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def ConnectToWifiNetwork(self, service_path, password='', shared=True):
    """Connect to a wifi network by its service path.

    Blocks until connection succeeds or fails.

    Args:
      service_path: Flimflam path that defines the wifi network.
      password: Passphrase for connecting to the wifi network.
      shared: Boolean value specifying whether the network should be shared.

    Returns:
      An error string if an error occured.
      None otherwise.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
        'command': 'ConnectToWifiNetwork',
        'service_path': service_path,
        'password': password,
        'shared': shared,
    }
    result = self._GetResultFromJSONRequest(
        cmd_dict, windex=None, timeout=50000)
    return result.get('error_string')

  def ConnectToHiddenWifiNetwork(self, ssid, security, password='',
                                 shared=True, save_credentials=False):
    """Connect to a wifi network by its service path.

    Blocks until connection succeeds or fails.

    Args:
      ssid: The SSID of the network to connect to.
      security: The network's security type. One of: 'SECURITY_NONE',
                'SECURITY_WEP', 'SECURITY_WPA', 'SECURITY_RSN', 'SECURITY_8021X'
      password: Passphrase for connecting to the wifi network.
      shared: Boolean value specifying whether the network should be shared.
      save_credentials: Boolean value specifying whether 802.1x credentials are
                        saved.

    Returns:
      An error string if an error occured.
      None otherwise.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    assert security in ('SECURITY_NONE', 'SECURITY_WEP', 'SECURITY_WPA',
                        'SECURITY_RSN', 'SECURITY_8021X')
    cmd_dict = {
        'command': 'ConnectToHiddenWifiNetwork',
        'ssid': ssid,
        'security': security,
        'password': password,
        'shared': shared,
        'save_credentials': save_credentials,
    }
    result = self._GetResultFromJSONRequest(
        cmd_dict, windex=None, timeout=50000)
    return result.get('error_string')

  def DisconnectFromWifiNetwork(self):
    """Disconnect from the connected wifi network.

    Blocks until disconnect is complete.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
        'command': 'DisconnectFromWifiNetwork',
    }
    self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def AddPrivateNetwork(self,
                        hostname,
                        service_name,
                        provider_type,
                        username,
                        password,
                        cert_id='',
                        key=''):
    """Add and connect to a private network.

    Blocks until connection succeeds or fails. This is equivalent to
    'Add Private Network' in the network menu UI.

    Args:
      hostname: Server hostname for the private network.
      service_name: Service name that defines the private network. Do not
                    add multiple services with the same name.
      provider_type: Types are L2TP_IPSEC_PSK and L2TP_IPSEC_USER_CERT.
                     Provider type OPEN_VPN is not yet supported.
                     Type names returned by GetPrivateNetworkInfo will
                     also work.
      username: Username for connecting to the virtual network.
      password: Passphrase for connecting to the virtual network.
      cert_id: Certificate id for a L2TP_IPSEC_USER_CERT network.
      key: Pre-shared key for a L2TP_IPSEC_PSK network.

    Returns:
      An error string if an error occured.
      None otherwise.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
        'command': 'AddPrivateNetwork',
        'hostname': hostname,
        'service_name': service_name,
        'provider_type': provider_type,
        'username': username,
        'password': password,
        'cert_id': cert_id,
        'key': key,
    }
    result = self._GetResultFromJSONRequest(
        cmd_dict, windex=None, timeout=50000)
    return result.get('error_string')

  def GetPrivateNetworkInfo(self):
    """Get details about private networks on chromeos.

    Returns:
      A dictionary including information about all remembered virtual networks
      as well as the currently connected virtual network, if any.
      Sample:
      { u'connected': u'/service/vpn_123_45_67_89_test_vpn'}
        u'/service/vpn_123_45_67_89_test_vpn':
          { u'username': u'vpn_user',
            u'name': u'test_vpn',
            u'hostname': u'123.45.67.89',
            u'key': u'abcde',
            u'cert_id': u'',
            u'password': u'zyxw123',
            u'provider_type': u'L2TP_IPSEC_PSK'},
        u'/service/vpn_111_11_11_11_test_vpn2':
          { u'username': u'testerman',
            u'name': u'test_vpn2',
            u'hostname': u'111.11.11.11',
            u'key': u'fghijklm',
            u'cert_id': u'',
            u'password': u'789mnop',
            u'provider_type': u'L2TP_IPSEC_PSK'},

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = { 'command': 'GetPrivateNetworkInfo' }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def ConnectToPrivateNetwork(self, service_path):
    """Connect to a remembered private network by its service path.

    Blocks until connection succeeds or fails. The network must have been
    previously added with all necessary connection details.

    Args:
      service_path: Service name that defines the private network.

    Returns:
      An error string if an error occured.
      None otherwise.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
        'command': 'ConnectToPrivateNetwork',
        'service_path': service_path,
    }
    result = self._GetResultFromJSONRequest(
        cmd_dict, windex=None, timeout=50000)
    return result.get('error_string')

  def DisconnectFromPrivateNetwork(self):
    """Disconnect from the active private network.

    Expects a private network to be active.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
        'command': 'DisconnectFromPrivateNetwork',
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def EnableSpokenFeedback(self, enabled):
    """Enables or disables spoken feedback accessibility mode.

    Args:
      enabled: Boolean value indicating the desired state of spoken feedback.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
        'command': 'EnableSpokenFeedback',
        'enabled': enabled,
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def IsSpokenFeedbackEnabled(self):
    """Check whether spoken feedback accessibility mode is enabled.

    Returns:
      True if spoken feedback is enabled, False otherwise.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = { 'command': 'IsSpokenFeedbackEnabled', }
    result = self._GetResultFromJSONRequest(cmd_dict, windex=None)
    return result.get('spoken_feedback')

  def GetTimeInfo(self, windex=0):
    """Gets info about the ChromeOS status bar clock.

    Set the 24-hour clock by using:
      self.SetPrefs('settings.clock.use_24hour_clock', True)

    Returns:
      a dictionary.
      Sample:
      {u'display_date': u'Tuesday, July 26, 2011',
       u'display_time': u'4:30',
       u'timezone': u'America/Los_Angeles'}

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = { 'command': 'GetTimeInfo' }
    if self.GetLoginInfo()['is_logged_in']:
      return self._GetResultFromJSONRequest(cmd_dict, windex=windex)
    else:
      return self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def SetTimezone(self, timezone):
    """Sets the timezone on ChromeOS. A user must be logged in.

    The timezone is the relative path to the timezone file in
    /usr/share/zoneinfo. For example, /usr/share/zoneinfo/America/Los_Angeles is
    'America/Los_Angeles'. For a list of valid timezones see
    'chrome/browser/chromeos/system/timezone_settings.cc'.

    This method does not return indication of success or failure.
    If the timezone is it falls back to a valid timezone.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = {
        'command': 'SetTimezone',
        'timezone': timezone,
    }
    self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def UpdateCheck(self):
    """Checks for a ChromeOS update. Blocks until finished updating.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = { 'command': 'UpdateCheck' }
    self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def GetVolumeInfo(self):
    """Gets the volume and whether the device is muted.

    Returns:
      a tuple.
      Sample:
      (47.763456790123456, False)

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = { 'command': 'GetVolumeInfo' }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def SetVolume(self, volume):
    """Sets the volume on ChromeOS. Only valid if not muted.

    Args:
      volume: The desired volume level as a percent from 0 to 100.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    assert volume >= 0 and volume <= 100
    cmd_dict = {
        'command': 'SetVolume',
        'volume': float(volume),
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def SetMute(self, mute):
    """Sets whether ChromeOS is muted or not.

    Args:
      mute: True to mute, False to unmute.

    Raises:
      pyauto_errors.JSONInterfaceError if the automation call returns an error.
    """
    cmd_dict = { 'command': 'SetMute' }
    cmd_dict = {
        'command': 'SetMute',
        'mute': mute,
    }
    return self._GetResultFromJSONRequest(cmd_dict, windex=None)

  # HTML Terminal

  def OpenCrosh(self):
    """Open crosh.

    Equivalent to pressing Ctrl-Alt-t.
    Opens in the last active (non-incognito) window.

    Waits long enough for crosh to load, but does not wait for the crosh
    prompt. Use WaitForHtermText() for that.
    """
    cmd_dict = { 'command': 'OpenCrosh' }
    self._GetResultFromJSONRequest(cmd_dict, windex=None)

  def WaitForHtermText(self, text, msg=None, tab_index=0, windex=0):
    """Waits for the given text in a hterm tab.

    Can be used to wait for the crosh> prompt or ssh prompt.

    This does not poll. It uses dom mutation observers to wait
    for the given text to show up.

    Args:
      text: the text to wait for. Can be a regex.
      msg: the failure message to emit if the text could not be found.
      tab_index: the tab for the hterm tab. Default: 0.
      windex: the window index for the hterm tab. Default: 0.
    """
    self.WaitForDomNode(
        xpath='//*[contains(text(), "%s")]' % text, frame_xpath='//iframe',
        msg=msg, tab_index=tab_index, windex=windex)

  def GetHtermRowsText(self, start, end, tab_index=0, windex=0):
    """Fetch rows from a html terminal tab.

    Works for both crosh and ssh tab.
    Uses term_.getRowsText(start, end) javascript call.

    Args:
      start: start line number (0-based).
      end: the end line (one beyond the line of interest).
      tab_index: the tab for the hterm tab. Default: 0.
      windex: the window index for the hterm tab. Default: 0.
    """
    return self.ExecuteJavascript(
        'domAutomationController.send(term_.getRowsText(%d, %d))' % (
            start, end),
        tab_index=tab_index, windex=windex)

  def SendKeysToHterm(self, text, tab_index=0, windex=0):
    """Send keys to a html terminal tab.

    Works for both crosh and ssh tab.
    Uses term_.onVTKeystroke(str) javascript call.

    Args:
      text: the text to send.
      tab_index: the tab for the hterm tab. Default: 0.
      windex: the window index for the hterm tab. Default: 0.
    """
    return self.ExecuteJavascript(
        'term_.onVTKeystroke("%s");'
        'domAutomationController.send("done")' % text,
        tab_index=tab_index, windex=windex)


  def GetMemoryStatsChromeOS(self, duration):
    """Identifies and returns different kinds of current memory usage stats.

    This function samples values each second for |duration| seconds, then
    outputs the min, max, and ending values for each measurement type.

    Args:
      duration: The number of seconds to sample data before outputting the
          minimum, maximum, and ending values for each measurement type.

    Returns:
      A dictionary containing memory usage information.  Each measurement type
      is associated with the min, max, and ending values from among all
      sampled values.  Values are specified in KB.
      {
        'gem_obj': {  # GPU memory usage.
          'min': ...,
          'max': ...,
          'end': ...,
        },
        'gtt': { ... },  # GPU memory usage (graphics translation table).
        'mem_free': { ... },  # CPU free memory.
        'mem_available': { ... },  # CPU available memory.
        'mem_shared': { ... },  # CPU shared memory.
        'mem_cached': { ... },  # CPU cached memory.
        'mem_anon': { ... },  # CPU anon memory (active + inactive).
        'mem_file': { ... },  # CPU file memory (active + inactive).
        'mem_slab': { ... },  # CPU slab memory.
        'browser_priv': { ... },  # Chrome browser private memory.
        'browser_shared': { ... },  # Chrome browser shared memory.
        'gpu_priv': { ... },  # Chrome GPU private memory.
        'gpu_shared': { ... },  # Chrome GPU shared memory.
        'renderer_priv': { ... },  # Total private memory of all renderers.
        'renderer_shared': { ... },  # Total shared memory of all renderers.
      }
    """
    logging.debug('Sampling memory information for %d seconds...' % duration)
    stats = {}

    for _ in xrange(duration):
      # GPU memory.
      gem_obj_path = '/sys/kernel/debug/dri/0/i915_gem_objects'
      if os.path.exists(gem_obj_path):
        p = subprocess.Popen('grep bytes %s' % gem_obj_path,
                             stdout=subprocess.PIPE, shell=True)
        stdout = p.communicate()[0]

        gem_obj = re.search(
            '\d+ objects, (\d+) bytes\n', stdout).group(1)
        if 'gem_obj' not in stats:
          stats['gem_obj'] = []
        stats['gem_obj'].append(int(gem_obj) / 1024.0)

      gtt_path = '/sys/kernel/debug/dri/0/i915_gem_gtt'
      if os.path.exists(gtt_path):
        p = subprocess.Popen('grep bytes %s' % gtt_path,
                             stdout=subprocess.PIPE, shell=True)
        stdout = p.communicate()[0]

        gtt = re.search(
            'Total [\d]+ objects, ([\d]+) bytes', stdout).group(1)
        if 'gtt' not in stats:
          stats['gtt'] = []
        stats['gtt'].append(int(gtt) / 1024.0)

      # CPU memory.
      stdout = ''
      with open('/proc/meminfo') as f:
        stdout = f.read()
      mem_free = re.search('MemFree:\s*([\d]+) kB', stdout).group(1)

      if 'mem_free' not in stats:
        stats['mem_free'] = []
      stats['mem_free'].append(int(mem_free))

      mem_dirty = re.search('Dirty:\s*([\d]+) kB', stdout).group(1)
      mem_active_file = re.search(
          'Active\(file\):\s*([\d]+) kB', stdout).group(1)
      mem_inactive_file = re.search(
          'Inactive\(file\):\s*([\d]+) kB', stdout).group(1)

      with open('/proc/sys/vm/min_filelist_kbytes') as f:
        mem_min_file = f.read()

      # Available memory =
      #     MemFree + ActiveFile + InactiveFile - DirtyMem - MinFileMem
      if 'mem_available' not in stats:
        stats['mem_available'] = []
      stats['mem_available'].append(
          int(mem_free) + int(mem_active_file) + int(mem_inactive_file) -
          int(mem_dirty) - int(mem_min_file))

      mem_shared = re.search('Shmem:\s*([\d]+) kB', stdout).group(1)
      if 'mem_shared' not in stats:
        stats['mem_shared'] = []
      stats['mem_shared'].append(int(mem_shared))

      mem_cached = re.search('Cached:\s*([\d]+) kB', stdout).group(1)
      if 'mem_cached' not in stats:
        stats['mem_cached'] = []
      stats['mem_cached'].append(int(mem_cached))

      mem_anon_active = re.search('Active\(anon\):\s*([\d]+) kB',
                                  stdout).group(1)
      mem_anon_inactive = re.search('Inactive\(anon\):\s*([\d]+) kB',
                                    stdout).group(1)
      if 'mem_anon' not in stats:
        stats['mem_anon'] = []
      stats['mem_anon'].append(int(mem_anon_active) + int(mem_anon_inactive))

      mem_file_active = re.search('Active\(file\):\s*([\d]+) kB',
                                  stdout).group(1)
      mem_file_inactive = re.search('Inactive\(file\):\s*([\d]+) kB',
                                    stdout).group(1)
      if 'mem_file' not in stats:
        stats['mem_file'] = []
      stats['mem_file'].append(int(mem_file_active) + int(mem_file_inactive))

      mem_slab = re.search('Slab:\s*([\d]+) kB', stdout).group(1)
      if 'mem_slab' not in stats:
        stats['mem_slab'] = []
      stats['mem_slab'].append(int(mem_slab))

      # Chrome process memory.
      pinfo = self.GetProcessInfo()['browsers'][0]['processes']
      total_renderer_priv = 0
      total_renderer_shared = 0
      for process in pinfo:
        mem_priv = process['working_set_mem']['priv']
        mem_shared = process['working_set_mem']['shared']
        if process['child_process_type'] == 'Browser':
          if 'browser_priv' not in stats:
            stats['browser_priv'] = []
            stats['browser_priv'].append(int(mem_priv))
          if 'browser_shared' not in stats:
            stats['browser_shared'] = []
            stats['browser_shared'].append(int(mem_shared))
        elif process['child_process_type'] == 'GPU':
          if 'gpu_priv' not in stats:
            stats['gpu_priv'] = []
            stats['gpu_priv'].append(int(mem_priv))
          if 'gpu_shared' not in stats:
            stats['gpu_shared'] = []
            stats['gpu_shared'].append(int(mem_shared))
        elif process['child_process_type'] == 'Tab':
          # Sum the memory of all renderer processes.
          total_renderer_priv += int(mem_priv)
          total_renderer_shared += int(mem_shared)
      if 'renderer_priv' not in stats:
        stats['renderer_priv'] = []
        stats['renderer_priv'].append(int(total_renderer_priv))
      if 'renderer_shared' not in stats:
        stats['renderer_shared'] = []
        stats['renderer_shared'].append(int(total_renderer_shared))

      time.sleep(1)

    # Compute min, max, and ending values to return.
    result = {}
    for measurement_type in stats:
      values = stats[measurement_type]
      result[measurement_type] = {
        'min': min(values),
        'max': max(values),
        'end': values[-1],
      }

    return result

  ## ChromeOS section -- end


class ExtraBrowser(PyUITest):
  """Launches a new browser with some extra flags.

  The new browser is launched with its own fresh profile.
  This class does not apply to ChromeOS.
  """
  def __init__(self, chrome_flags=[], methodName='runTest', **kwargs):
    """Accepts extra chrome flags for launching a new browser instance.

    Args:
      chrome_flags: list of extra flags when launching a new browser.
    """
    assert not PyUITest.IsChromeOS(), \
        'This function cannot be used to launch a new browser in ChromeOS.'
    PyUITest.__init__(self, methodName=methodName, **kwargs)
    self._chrome_flags = chrome_flags
    PyUITest.setUp(self)

  def __del__(self):
    """Tears down the browser and then calls super class's destructor"""
    PyUITest.tearDown(self)
    PyUITest.__del__(self)

  def ExtraChromeFlags(self):
    """Prepares the browser to launch with specified Chrome flags."""
    return PyUITest.ExtraChromeFlags(self) + self._chrome_flags


class _RemoteProxy():
  """Class for PyAuto remote method calls.

  Use this class along with RemoteHost.testRemoteHost to establish a PyAuto
  connection with another machine and make remote PyAuto calls. The RemoteProxy
  mimics a PyAuto object, so all json-style PyAuto calls can be made on it.

  The remote host acts as a dumb executor that receives method call requests,
  executes them, and sends all of the results back to the RemoteProxy, including
  the return value, thrown exceptions, and console output.

  The remote host should be running the same version of PyAuto as the proxy.
  A mismatch could lead to undefined behavior.

  Example usage:
    class MyTest(pyauto.PyUITest):
      def testRemoteExample(self):
        remote = pyauto._RemoteProxy(('127.0.0.1', 7410))
        remote.NavigateToURL('http://www.google.com')
        title = remote.GetActiveTabTitle()
        self.assertEqual(title, 'Google')
  """
  class RemoteException(Exception):
    pass

  def __init__(self, host):
    self.RemoteConnect(host)

  def RemoteConnect(self, host):
    begin = time.time()
    while time.time() - begin < 50:
      self._socket = socket.socket()
      if not self._socket.connect_ex(host):
        break
      time.sleep(0.25)
    else:
      # Make one last attempt, but raise a socket error on failure.
      self._socket = socket.socket()
      self._socket.connect(host)

  def RemoteDisconnect(self):
    if self._socket:
      self._socket.shutdown(socket.SHUT_RDWR)
      self._socket.close()
      self._socket = None

  def CreateTarget(self, target):
    """Registers the methods and creates a remote instance of a target.

    Any RPC calls will then be made on the remote target instance. Note that the
    remote instance will be a brand new instance and will have none of the state
    of the local instance. The target's class should have a constructor that
    takes no arguments.
    """
    self._Call('CreateTarget', target.__class__)
    self._RegisterClassMethods(target)

  def _RegisterClassMethods(self, remote_class):
    # Make remote-call versions of all remote_class methods.
    for method_name, _ in inspect.getmembers(remote_class, inspect.ismethod):
      # Ignore private methods and duplicates.
      if method_name[0] in string.letters and \
        getattr(self, method_name, None) is None:
        setattr(self, method_name, functools.partial(self._Call, method_name))

  def _Call(self, method_name, *args, **kwargs):
    # Send request.
    request = pickle.dumps((method_name, args, kwargs))
    if self._socket.send(request) != len(request):
      raise self.RemoteException('Error sending remote method call request.')

    # Receive response.
    response = self._socket.recv(4096)
    if not response:
      raise self.RemoteException('Client disconnected during method call.')
    result, stdout, stderr, exception = pickle.loads(response)

    # Print any output the client captured, throw any exceptions, and return.
    sys.stdout.write(stdout)
    sys.stderr.write(stderr)
    if exception:
      raise self.RemoteException('%s raised by remote client: %s' %
                                 (exception[0], exception[1]))
    return result


class PyUITestSuite(pyautolib.PyUITestSuiteBase, unittest.TestSuite):
  """Base TestSuite for PyAuto UI tests."""

  def __init__(self, args):
    pyautolib.PyUITestSuiteBase.__init__(self, args)

    # Figure out path to chromium binaries
    browser_dir = os.path.normpath(os.path.dirname(pyautolib.__file__))
    logging.debug('Loading pyauto libs from %s', browser_dir)
    self.InitializeWithPath(pyautolib.FilePath(browser_dir))
    os.environ['PATH'] = browser_dir + os.pathsep + os.environ['PATH']

    unittest.TestSuite.__init__(self)
    cr_source_root = os.path.normpath(os.path.join(
        os.path.dirname(__file__), os.pardir, os.pardir, os.pardir))
    self.SetCrSourceRoot(pyautolib.FilePath(cr_source_root))

    # Start http server, if needed.
    global _OPTIONS
    if _OPTIONS and not _OPTIONS.no_http_server:
      self._StartHTTPServer()
    if _OPTIONS and _OPTIONS.remote_host:
      self._ConnectToRemoteHosts(_OPTIONS.remote_host.split(','))

  def __del__(self):
    # python unittest module is setup such that the suite gets deleted before
    # the test cases, which is odd because our test cases depend on
    # initializtions like exitmanager, autorelease pool provided by the
    # suite. Forcibly delete the test cases before the suite.
    del self._tests
    pyautolib.PyUITestSuiteBase.__del__(self)

    global _HTTP_SERVER
    if _HTTP_SERVER:
      self._StopHTTPServer()

    global _CHROME_DRIVER_FACTORY
    if _CHROME_DRIVER_FACTORY is not None:
      _CHROME_DRIVER_FACTORY.Stop()

  def _StartHTTPServer(self):
    """Start a local file server hosting data files over http://"""
    global _HTTP_SERVER
    assert not _HTTP_SERVER, 'HTTP Server already started'
    http_data_dir = _OPTIONS.http_data_dir
    http_server = pyautolib.SpawnedTestServer(
        pyautolib.SpawnedTestServer.TYPE_HTTP,
        '127.0.0.1',
        pyautolib.FilePath(http_data_dir))
    assert http_server.Start(), 'Could not start http server'
    _HTTP_SERVER = http_server
    logging.debug('Started http server at "%s".', http_data_dir)

  def _StopHTTPServer(self):
    """Stop the local http server."""
    global _HTTP_SERVER
    assert _HTTP_SERVER, 'HTTP Server not yet started'
    assert _HTTP_SERVER.Stop(), 'Could not stop http server'
    _HTTP_SERVER = None
    logging.debug('Stopped http server.')

  def _ConnectToRemoteHosts(self, addresses):
    """Connect to remote PyAuto instances using a RemoteProxy.

    The RemoteHost instances must already be running."""
    global _REMOTE_PROXY
    assert not _REMOTE_PROXY, 'Already connected to a remote host.'
    _REMOTE_PROXY = []
    for address in addresses:
      if address == 'localhost' or address == '127.0.0.1':
        self._StartLocalRemoteHost()
      _REMOTE_PROXY.append(_RemoteProxy((address, 7410)))

  def _StartLocalRemoteHost(self):
    """Start a remote PyAuto instance on the local machine."""
    # Add the path to our main class to the RemoteHost's
    # environment, so it can load that class at runtime.
    import __main__
    main_path = os.path.dirname(__main__.__file__)
    env = os.environ
    if env.get('PYTHONPATH', None):
      env['PYTHONPATH'] += ':' + main_path
    else:
      env['PYTHONPATH'] = main_path

    # Run it!
    subprocess.Popen([sys.executable, os.path.join(os.path.dirname(__file__),
                                                   'remote_host.py')], env=env)


class _GTestTextTestResult(unittest._TextTestResult):
  """A test result class that can print formatted text results to a stream.

  Results printed in conformance with gtest output format, like:
  [ RUN        ] autofill.AutofillTest.testAutofillInvalid: "test desc."
  [         OK ] autofill.AutofillTest.testAutofillInvalid
  [ RUN        ] autofill.AutofillTest.testFillProfile: "test desc."
  [         OK ] autofill.AutofillTest.testFillProfile
  [ RUN        ] autofill.AutofillTest.testFillProfileCrazyCharacters: "Test."
  [         OK ] autofill.AutofillTest.testFillProfileCrazyCharacters
  """
  def __init__(self, stream, descriptions, verbosity):
    unittest._TextTestResult.__init__(self, stream, descriptions, verbosity)

  def _GetTestURI(self, test):
    if sys.version_info[:2] <= (2, 4):
      return '%s.%s' % (unittest._strclass(test.__class__),
                        test._TestCase__testMethodName)
    return '%s.%s.%s' % (test.__class__.__module__,
                         test.__class__.__name__,
                         test._testMethodName)

  def getDescription(self, test):
    return '%s: "%s"' % (self._GetTestURI(test), test.shortDescription())

  def startTest(self, test):
    unittest.TestResult.startTest(self, test)
    self.stream.writeln('[ RUN        ] %s' % self.getDescription(test))

  def addSuccess(self, test):
    unittest.TestResult.addSuccess(self, test)
    self.stream.writeln('[         OK ] %s' % self._GetTestURI(test))

  def addError(self, test, err):
    unittest.TestResult.addError(self, test, err)
    self.stream.writeln('[      ERROR ] %s' % self._GetTestURI(test))

  def addFailure(self, test, err):
    unittest.TestResult.addFailure(self, test, err)
    self.stream.writeln('[     FAILED ] %s' % self._GetTestURI(test))


class PyAutoTextTestRunner(unittest.TextTestRunner):
  """Test Runner for PyAuto tests that displays results in textual format.

  Results are displayed in conformance with gtest output.
  """
  def __init__(self, verbosity=1):
    unittest.TextTestRunner.__init__(self,
                                     stream=sys.stderr,
                                     verbosity=verbosity)

  def _makeResult(self):
    return _GTestTextTestResult(self.stream, self.descriptions, self.verbosity)


# Implementation inspired from unittest.main()
class Main(object):
  """Main program for running PyAuto tests."""

  _options, _args = None, None
  _tests_filename = 'PYAUTO_TESTS'
  _platform_map = {
    'win32':  'win',
    'darwin': 'mac',
    'linux2': 'linux',
    'linux3': 'linux',
    'chromeos': 'chromeos',
  }

  def __init__(self):
    self._ParseArgs()
    self._Run()

  def _ParseArgs(self):
    """Parse command line args."""
    parser = optparse.OptionParser()
    parser.add_option(
        '', '--channel-id', type='string', default='',
        help='Name of channel id, if using named interface.')
    parser.add_option(
        '', '--chrome-flags', type='string', default='',
        help='Flags passed to Chrome.  This is in addition to the usual flags '
             'like suppressing first-run dialogs, enabling automation.  '
             'See chrome/common/chrome_switches.cc for the list of flags '
             'chrome understands.')
    parser.add_option(
        '', '--http-data-dir', type='string',
        default=os.path.join('chrome', 'test', 'data'),
        help='Relative path from which http server should serve files.')
    parser.add_option(
        '-L', '--list-tests', action='store_true', default=False,
        help='List all tests, and exit.')
    parser.add_option(
        '--shard',
        help='Specify sharding params. Example: 1/3 implies split the list of '
             'tests into 3 groups of which this is the 1st.')
    parser.add_option(
        '', '--log-file', type='string', default=None,
        help='Provide a path to a file to which the logger will log')
    parser.add_option(
        '', '--no-http-server', action='store_true', default=False,
        help='Do not start an http server to serve files in data dir.')
    parser.add_option(
        '', '--remote-host', type='string', default=None,
        help='Connect to remote hosts for remote automation. If "localhost" '
            '"127.0.0.1" is specified, a remote host will be launched '
            'automatically on the local machine.')
    parser.add_option(
        '', '--repeat', type='int', default=1,
        help='Number of times to repeat the tests. Useful to determine '
             'flakiness. Defaults to 1.')
    parser.add_option(
        '-S', '--suite', type='string', default='FULL',
        help='Name of the suite to load.  Defaults to "FULL".')
    parser.add_option(
        '-v', '--verbose', action='store_true', default=False,
        help='Make PyAuto verbose.')
    parser.add_option(
        '-D', '--wait-for-debugger', action='store_true', default=False,
        help='Block PyAuto on startup for attaching debugger.')

    self._options, self._args = parser.parse_args()
    global _OPTIONS
    _OPTIONS = self._options  # Export options so other classes can access.

    # Set up logging. All log messages will be prepended with a timestamp.
    format = '%(asctime)s %(levelname)-8s %(message)s'

    level = logging.INFO
    if self._options.verbose:
      level=logging.DEBUG

    logging.basicConfig(level=level, format=format,
                        filename=self._options.log_file)

  def TestsDir(self):
    """Returns the path to dir containing tests.

    This is typically the dir containing the tests description file.
    This method should be overridden by derived class to point to other dirs
    if needed.
    """
    return os.path.dirname(__file__)

  @staticmethod
  def _ImportTestsFromName(name):
    """Get a list of all test names from the given string.

    Args:
      name: dot-separated string for a module, a test case or a test method.
            Examples: omnibox  (a module)
                      omnibox.OmniboxTest  (a test case)
                      omnibox.OmniboxTest.testA  (a test method)

    Returns:
      [omnibox.OmniboxTest.testA, omnibox.OmniboxTest.testB, ...]
    """
    def _GetTestsFromTestCase(class_obj):
      """Return all test method names from given class object."""
      return [class_obj.__name__ + '.' + x for x in dir(class_obj) if
              x.startswith('test')]

    def _GetTestsFromModule(module):
      """Return all test method names from the given module object."""
      tests = []
      for name in dir(module):
        obj = getattr(module, name)
        if (isinstance(obj, (type, types.ClassType)) and
            issubclass(obj, PyUITest) and obj != PyUITest):
          tests.extend([module.__name__ + '.' + x for x in
                        _GetTestsFromTestCase(obj)])
      return tests

    module = None
    # Locate the module
    parts = name.split('.')
    parts_copy = parts[:]
    while parts_copy:
      try:
        module = __import__('.'.join(parts_copy))
        break
      except ImportError:
        del parts_copy[-1]
        if not parts_copy: raise
    # We have the module. Pick the exact test method or class asked for.
    parts = parts[1:]
    obj = module
    for part in parts:
      obj = getattr(obj, part)

    if type(obj) == types.ModuleType:
      return _GetTestsFromModule(obj)
    elif (isinstance(obj, (type, types.ClassType)) and
          issubclass(obj, PyUITest) and obj != PyUITest):
      return [module.__name__ + '.' + x for x in _GetTestsFromTestCase(obj)]
    elif type(obj) == types.UnboundMethodType:
      return [name]
    else:
      logging.warn('No tests in "%s"', name)
      return []

  def _HasTestCases(self, module_string):
    """Determines if we have any PyUITest test case classes in the module
       identified by |module_string|."""
    module = __import__(module_string)
    for name in dir(module):
      obj = getattr(module, name)
      if (isinstance(obj, (type, types.ClassType)) and
          issubclass(obj, PyUITest)):
        return True
    return False

  def _ExpandTestNames(self, args):
    """Returns a list of tests loaded from the given args.

    The given args can be either a module (ex: module1) or a testcase
    (ex: module2.MyTestCase) or a test (ex: module1.MyTestCase.testX)
    or a suite name (ex: @FULL). If empty, the tests in the already imported
    modules are loaded.

    Args:
      args: [module1, module2, module3.testcase, module4.testcase.testX]
            These modules or test cases or tests should be importable.
            Suites can be specified by prefixing @. Example: @FULL

      Returns:
        a list of expanded test names.  Example:
          [
            'module1.TestCase1.testA',
            'module1.TestCase1.testB',
            'module2.TestCase2.testX',
            'module3.testcase.testY',
            'module4.testcase.testX'
          ]
    """

    def _TestsFromDescriptionFile(suite):
      pyauto_tests_file = os.path.join(self.TestsDir(), self._tests_filename)
      if suite:
        logging.debug("Reading %s (@%s)", pyauto_tests_file, suite)
      else:
        logging.debug("Reading %s", pyauto_tests_file)
      if not os.path.exists(pyauto_tests_file):
        logging.warn("%s missing. Cannot load tests.", pyauto_tests_file)
        return []
      else:
        return self._ExpandTestNamesFrom(pyauto_tests_file, suite)

    if not args:  # Load tests ourselves
      if self._HasTestCases('__main__'):    # we are running a test script
        module_name = os.path.splitext(os.path.basename(sys.argv[0]))[0]
        args.append(module_name)   # run the test cases found in it
      else:  # run tests from the test description file
        args = _TestsFromDescriptionFile(self._options.suite)
    else:  # Check args with @ prefix for suites
      out_args = []
      for arg in args:
        if arg.startswith('@'):
          suite = arg[1:]
          out_args += _TestsFromDescriptionFile(suite)
        else:
          out_args.append(arg)
      args = out_args
    return args

  def _ExpandTestNamesFrom(self, filename, suite):
    """Load test names from the given file.

    Args:
      filename: the file to read the tests from
      suite: the name of the suite to load from |filename|.

    Returns:
      a list of test names
      [module.testcase.testX, module.testcase.testY, ..]
    """
    suites = PyUITest.EvalDataFrom(filename)
    platform = sys.platform
    if PyUITest.IsChromeOS():  # check if it's chromeos
      platform = 'chromeos'
    assert platform in self._platform_map, '%s unsupported' % platform
    def _NamesInSuite(suite_name):
      logging.debug('Expanding suite %s', suite_name)
      platforms = suites.get(suite_name)
      names = platforms.get('all', []) + \
              platforms.get(self._platform_map[platform], [])
      ret = []
      # Recursively include suites if any.  Suites begin with @.
      for name in names:
        if name.startswith('@'):  # Include another suite
          ret.extend(_NamesInSuite(name[1:]))
        else:
          ret.append(name)
      return ret

    assert suite in suites, '%s: No such suite in %s' % (suite, filename)
    all_names = _NamesInSuite(suite)
    args = []
    excluded = []
    # Find all excluded tests.  Excluded tests begin with '-'.
    for name in all_names:
      if name.startswith('-'):  # Exclude
        excluded.extend(self._ImportTestsFromName(name[1:]))
      else:
        args.extend(self._ImportTestsFromName(name))
    for name in excluded:
      if name in args:
        args.remove(name)
      else:
        logging.warn('Cannot exclude %s. Not included. Ignoring', name)
    if excluded:
      logging.debug('Excluded %d test(s): %s', len(excluded), excluded)
    return args

  def _Run(self):
    """Run the tests."""
    if self._options.wait_for_debugger:
      raw_input('Attach debugger to process %s and hit <enter> ' % os.getpid())

    suite_args = [sys.argv[0]]
    chrome_flags = self._options.chrome_flags
    # Set CHROME_HEADLESS. It enables crash reporter on posix.
    os.environ['CHROME_HEADLESS'] = '1'
    os.environ['EXTRA_CHROME_FLAGS'] = chrome_flags
    test_names = self._ExpandTestNames(self._args)

    # Shard, if requested (--shard).
    if self._options.shard:
      matched = re.match('(\d+)/(\d+)', self._options.shard)
      if not matched:
        print >>sys.stderr, 'Invalid sharding params: %s' % self._options.shard
        sys.exit(1)
      shard_index = int(matched.group(1)) - 1
      num_shards = int(matched.group(2))
      if shard_index < 0 or shard_index >= num_shards:
        print >>sys.stderr, 'Invalid sharding params: %s' % self._options.shard
        sys.exit(1)
      test_names = pyauto_utils.Shard(test_names, shard_index, num_shards)

    test_names *= self._options.repeat
    logging.debug("Loading %d tests from %s", len(test_names), test_names)
    if self._options.list_tests:  # List tests and exit
      for name in test_names:
        print name
      sys.exit(0)
    pyauto_suite = PyUITestSuite(suite_args)
    loaded_tests = unittest.defaultTestLoader.loadTestsFromNames(test_names)
    pyauto_suite.addTests(loaded_tests)
    verbosity = 1
    if self._options.verbose:
      verbosity = 2
    result = PyAutoTextTestRunner(verbosity=verbosity).run(pyauto_suite)
    del loaded_tests  # Need to destroy test cases before the suite
    del pyauto_suite
    successful = result.wasSuccessful()
    if not successful:
      pyauto_tests_file = os.path.join(self.TestsDir(), self._tests_filename)
      print >>sys.stderr, 'Tests can be disabled by editing %s. ' \
                          'Ref: %s' % (pyauto_tests_file, _PYAUTO_DOC_URL)
    sys.exit(not successful)


if __name__ == '__main__':
  Main()
