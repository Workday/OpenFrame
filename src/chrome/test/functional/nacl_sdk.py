#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import copy
import ctypes
from distutils import version
import fnmatch
import glob
import hashlib
import logging
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import urllib2
import xml.dom.minidom
import zipfile

import pyauto_functional  # Must be imported before pyauto.
import pyauto
import pyauto_utils
import test_utils


class NaClSDKTest(pyauto.PyUITest):
  """Tests for the NaCl SDK."""
  _isExamplesTest = False
  _extracted_sdk_path = None
  _temp_dir = None
  _updated_pepper_versions = []
  _latest_updated_pepper_versions = []
  _settings = {
      'post_sdk_download_url': 'http://code.google.com/chrome/nativeclient/'
          'docs/download.html',
      'post_sdk_zip': 'http://storage.googleapis.com/'
          'nativeclient-mirror/nacl/nacl_sdk/nacl_sdk.zip',
      'min_required_chrome_build': 14,
  }

  def tearDown(self):
    pyauto.PyUITest.tearDown(self)
    if not self._isExamplesTest:
      self._RemoveDownloadedTestFile()

  def testNaClSDK(self):
    """Verify that NaCl SDK is working properly."""
    if not self._HasAllSystemRequirements():
      logging.info('System does not meet the requirements.')
      return
    self._extracted_sdk_path = tempfile.mkdtemp()
    self._VerifyDownloadLinks()
    self._VerifyNaClSDKInstaller()
    self._VerifyInstall()
    self._VerifyUpdate()
    self._LaunchServerAndVerifyExamplesAllPepperVersions()

  def NaClSDKExamples(self):
    """Verify if NaCl SDK examples are working."""
    self._isExamplesTest = True
    nacl_sdk_root = os.environ.get('NACL_SDK_ROOT', None)
    pepper_version = os.environ.get('PEPPER_VER', None)
    if nacl_sdk_root and pepper_version:
      self._LaunchServerAndVerifyExamples('pepper_' + pepper_version,
          nacl_sdk_root)
    else:
      self.fail(msg='Missing pepper version to be checked or SDK path.')

  def _VerifyDownloadLinks(self):
    """Verify the download links.

    Simply verify that NaCl download links exist in html page.
    """
    html = None
    for i in xrange(3):
      try:
        html = urllib2.urlopen(self._settings['post_sdk_download_url']).read()
        break
      except:
        pass
    self.assertTrue(html,
                    msg='Cannot open URL: %s' %
                    self._settings['post_sdk_download_url'])
    sdk_url = self._settings['post_sdk_zip']
    self.assertTrue(sdk_url in html,
                    msg='Missing SDK download URL: %s' % sdk_url)

  def _VerifyNaClSDKInstaller(self):
    """Verify NaCl SDK installer."""
    search_list = [
        'sdk_cache/',
        'sdk_tools/',
    ]
    mac_lin_additional_search_items = [
        'naclsdk',
    ]
    win_additional_search_items = [
        'naclsdk.bat'
    ]
    self._DownloadNaClSDK()
    self._ExtractNaClSDK()
    if pyauto.PyUITest.IsWin():
      self._SearchNaClSDKFile(
          search_list + win_additional_search_items)
    elif pyauto.PyUITest.IsMac() or pyauto.PyUITest.IsLinux():
      self._SearchNaClSDKFile(
          search_list + mac_lin_additional_search_items)
    else:
      self.fail(msg='NaCl SDK does not support this OS.')

  def _VerifyInstall(self):
    """Install NACL sdk."""
    # Executing naclsdk(.bat) list
    if pyauto.PyUITest.IsWin():
      source_file = os.path.join(
          self._extracted_sdk_path, 'nacl_sdk', 'naclsdk.bat')
    elif pyauto.PyUITest.IsMac() or pyauto.PyUITest.IsLinux():
      source_file = os.path.join(
          self._extracted_sdk_path, 'nacl_sdk', 'naclsdk')
      subprocess.call(['chmod', '-R', '755', self._extracted_sdk_path])
    else:
      self.fail(msg='NaCl SDK does not support this OS.')
    subprocess.Popen([source_file, 'list'],
                     stdout=subprocess.PIPE,
                     stderr=subprocess.PIPE).communicate()

  def _VerifyUpdate(self):
    """Update NACL sdk"""
    # Executing naclsdk(.bat) update
    if pyauto.PyUITest.IsWin():
      source_file = os.path.join(self._extracted_sdk_path, 'nacl_sdk',
                                 'naclsdk.bat')
    elif pyauto.PyUITest.IsMac() or pyauto.PyUITest.IsLinux():
      source_file = os.path.join(self._extracted_sdk_path, 'nacl_sdk',
                                 'naclsdk')
    else:
      self.fail(msg='NaCl SDK does not support this OS.')
    # Executing nacl_sdk(.bat) update to get the latest version.
    updated_output = subprocess.Popen([source_file, 'update'],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE).communicate()[0]
    self._updated_pepper_versions.extend(
        re.findall('Updating bundle (pepper_[0-9]{2})', updated_output))
    self._updated_pepper_versions = list(set(self._updated_pepper_versions))
    self._updated_pepper_versions.sort(key=str.lower)
    updated_pepper_versions_len = len(self._updated_pepper_versions)
    self._latest_updated_pepper_versions = filter(
        lambda x: x >= 'pepper_18', self._updated_pepper_versions)

  def _GetURLForExampleName(self, name, toolchain):
    return 'http://localhost:5103/%s/index_%s.html' % (name, toolchain)

  def _GetExampleNamesAndURLs(self, examples_path):
    """Get a list of all examples as (name, url) tuples.

    Args:
      examples_path: The path to the examples directory in the NaCl SDK.
    """
    toolchains = ['newlib', 'glibc', 'pnacl']

    examples = []
    for toolchain in toolchains:
      for example in os.listdir(examples_path):
        html_path = os.path.join(examples_path, example,
            'index_%s.html' % (toolchain,))
        if os.path.exists(html_path):
          example_url = self._GetURLForExampleName(example, toolchain)
          examples.append((example, example_url))
    return examples

  def _LaunchServerAndVerifyExamplesAllPepperVersions(self):
    for pepper_version in self._latest_updated_pepper_versions:
      pepper_path = os.path.join(self._extracted_sdk_path,
          'nacl_sdk', 'pepper_' + str(pepper_version))
      self._LaunchServerAndVerifyExamples(pepper_version, pepper_path)

  def _LaunchServerAndVerifyExamples(self, pepper_version, pepper_path):
    """Start local HTTP server and verify examples."""
    if self._ChromeAndPepperVersionMatch(pepper_version):
      # Close server if it's already open.
      if self._IsURLAlive('http://localhost:5103'):
        self._CloseHTTPServer()

      examples_path = os.path.join(pepper_path, 'examples')

      # Launch local http server.
      proc = subprocess.Popen(['make RUN'], shell=True, cwd=examples_path)
      self.WaitUntil(
          lambda: self._IsURLAlive('http://localhost:5103'),
          timeout=150, retry_sleep=1)

      examples = self._GetExampleNamesAndURLs(examples_path)
      try:
        self._OpenExamplesAndStartTest(examples)
      finally:
        self._CloseHTTPServer(proc)

    else:
      self.pprint('Pepper Version %s does not match the Chrome version %s.'
          % (pepper_version,
          self.GetBrowserInfo()['properties']['ChromeVersion']))

  def _ChromeAndPepperVersionMatch(self, pepper_version='pepper_18'):
    """Determine if chrome and pepper version match"""
    version_number = re.findall('pepper_([0-9]{2})', pepper_version)
    browser_info = self.GetBrowserInfo()
    chrome_version = browser_info['properties']['ChromeVersion']
    chrome_build = int(chrome_version.split('.')[0])
    return int(chrome_build) == int(version_number[0])

  def _RemoveDownloadedTestFile(self):
    """Delete downloaded files and dirs from downloads directory."""
    if self._extracted_sdk_path and os.path.exists(self._extracted_sdk_path):
      self._CloseHTTPServer()

      def _RemoveFile():
        shutil.rmtree(self._extracted_sdk_path, ignore_errors=True)
        return os.path.exists(self._extracted_sdk_path)

      success = self.WaitUntil(_RemoveFile, retry_sleep=2,
                               expect_retval=False)
      self.assertTrue(success,
                      msg='Cannot remove %s' % self._extracted_sdk_path)

    if self._temp_dir:
      pyauto_utils.RemovePath(self._temp_dir)

  def _OpenExamplesAndStartTest(self, examples):
    """Open each example and verify that it's working.

    Args:
      examples: A list of example (name, url) tuples.
    """
    example_verify_funcs = {
        'dlopen': self._VerifyDynamicLibraryOpen,
        'file_io': self._VerifyFileIoExample,
        'geturl': self._VerifyGetURLExample,
        'input_events': self._VerifyInputEventsExample,
        'load_progress': self._VerifyLoadProgressExample,
        'mt_input_events': self._VerifyMultithreadedInputEventsExample,
        'pi_generator': self._VerifyPiGeneratorExample,
        'sine_synth': self._VerifySineSynthExample,
        'websocket': self._VerifyWebSocketExample,
    }

    # Remove examples that we don't yet verify
    examples = [(name, url) for name, url in examples
        if name in example_verify_funcs]

    # Open all examples.
    for name, url in examples:
      self.AppendTab(pyauto.GURL(url))
      self._CheckForCrashes()

    # Verify all examples are working.
    for name, url in examples:
      self._VerifyAnExample(name, url, example_verify_funcs[name])
    self._CheckForCrashes()

    # Close each tab and check for crashes.
    tab_count = self.GetTabCount()
    for index in xrange(tab_count - 1, 0, -1):
      self.CloseTab(tab_index=index)
      self._CheckForCrashes()

  def _VerifyAnExample(self, name, url, verify_func):
    """Verify NaCl example is working.

    Args:
      name: A string name of the example.
      url: A string url of the example.
      verify_func: The function to verify the example.
          Takes (tab_index, name, url) as parameters.
    """
    if not verify_func:
      self.fail(msg='No test available for %s.' % name)

    info = self.GetBrowserInfo()
    tabs = info['windows'][0]['tabs']
    tab_index = None
    for tab in tabs:
      if url == tab['url']:
        self.ActivateTab(tab['index'])
        tab_index = tab['index']
        break

    if tab_index:
      verify_func(tab_index, name, url)

  def _VerifyElementPresent(self, element_id, expected_value, tab_index, msg,
                            attribute='innerHTML', timeout=150):
    """Determine if dom element has the expected value.

    Args:
      element_id: Dom element's id.
      expected_value: String to be matched against the Dom element.
      tab_index: Tab index to work on.
      attribute: Attribute to match |expected_value| against, if
                 given. Defaults to 'innerHTML'.
      timeout: The max timeout (in secs) for which to wait.
    """
    js_code = """
        var output = document.getElementById('%s').%s;
        var result;
        if (output.indexOf('%s') != -1)
          result = 'pass';
        else
          result = 'fail';
        window.domAutomationController.send(result);
    """ % (element_id, attribute, expected_value)
    success = self.WaitUntil(
        lambda: self.ExecuteJavascript(js_code, tab_index),
        timeout=timeout, expect_retval='pass')
    self.assertTrue(success, msg=msg)

  def _CreateJSToSimulateMouseclick(self):
    """Create javascript to simulate mouse click event."""
    js_code = """
        var rightClick = document.createEvent('MouseEvents');
        rightClick.initMouseEvent(
          'mousedown', true, true, document,
          1, 32, 121, 10, 100,
          false, false, false, false,
          2, common.naclModule
        );
        common.naclModule.dispatchEvent(rightClick);
        window.domAutomationController.send('done');
    """
    return js_code

  def _VerifyInputEventsExample(self, tab_index, name, url):
    """Verify Input Events Example.

    Args:
      tab_index: Tab index integer that the example is on.
      name: A string name of the example.
      url: A string url of the example.
    """
    success = self._VerifyElementPresent('eventString', 'DidChangeView',
        tab_index, msg='Example %s failed. URL: %s' % (name, url))

    # Simulate mouse click on event module.
    js_code = self._CreateJSToSimulateMouseclick()
    self.ExecuteJavascript(js_code, tab_index)

    # Check if 'eventString' has handled above mouse click.
    success = self.WaitUntil(
        lambda: re.search('DidHandleInputEvent', self.GetDOMValue(
          'document.getElementById("eventString").innerHTML',
          tab_index)).group(), expect_retval='DidHandleInputEvent')
    self.assertTrue(success, msg='Example %s failed. URL: %s' % (name, url))

  def _VerifyMultithreadedInputEventsExample(self, tab_index, name, url):
    """Verify Input Events Example.

    Args:
      tab_index: Tab index integer that the example is on.
      name: A string name of the example.
      url: A string url of the example.
    """
    success = self.WaitUntil(
        lambda: bool(self.GetDOMValue(
          'document.getElementById("eventString").innerHTML',
          tab_index).find('DidChangeView') + 1))

    self.assertTrue(success, msg='Example %s failed. URL: %s' % (name, url))

    # Simulate mouse click on event module.
    js_code = self._CreateJSToSimulateMouseclick()
    self.ExecuteJavascript(js_code, tab_index)

    # Check if above mouse click is handled.
    success = self._VerifyElementPresent('eventString', 'Mouse event',
        tab_index, msg='Example %s failed. URL: %s' % (name, url))

    # Kill worker thread and queue
    js_code = """
      document.getElementsByTagName('button')[0].click();
      window.domAutomationController.send('done');
    """
    self.ExecuteJavascript(js_code, tab_index)

    # Check if main thread has cancelled queue.
    success = self._VerifyElementPresent('eventString', 'Received cancel',
        tab_index, msg='Example %s failed. URL: %s' % (name, url))

    # Simulate mouse click on event module.
    js_code = self._CreateJSToSimulateMouseclick()
    self.ExecuteJavascript(js_code, tab_index)

    # Check if above mouse click is not handled after killing worker thread.
    def _CheckMouseClickEventStatus():
      return self.GetDOMValue(
        'document.getElementById("eventString").innerHTML',
        tab_index).find('Mouse event', self.GetDOMValue(
        'document.getElementById("eventString").innerHTML', tab_index).find(
        'Received cancel'))

    success = self.WaitUntil(_CheckMouseClickEventStatus, expect_retval=-1)
    self.assertTrue(success, msg='Example %s failed. URL: %s' % (name, url))

  def _VerifyFileIoExample(self, tab_index, name, url):
    """Verify File IO Example.

    Args:
      tab_index: Tab index integer that the example is on.
      name: A string name of the example.
      url: A string url of the example.
    """
    def _CheckStatus(substring_expected, fail_msg):
      self.assertTrue(
          self.WaitUntil(
            lambda: self.GetDOMValue(
              'document.getElementById("statusField").innerHTML', tab_index)\
                  .find(substring_expected) != -1, expect_retval=True),
          msg='Example %s failed. URL: %s. Reason: %s' % (name, url, fail_msg))

    # Give permission to use file system by clicking infobar OK
    infobar_index = test_utils.WaitForInfobarTypeAndGetIndex(self,
        'confirm_infobar', 0, tab_index)
    self.PerformActionOnInfobar('accept', infobar_index, 0, tab_index)
    _CheckStatus('Ready!', 'NaCl module load')

    # Check that deleting non-existing files gives file not found
    js_code = """
      document.getElementById('file_name').value = '/abc';
      document.getElementById('file_editor').value = 'test';
      document.getElementById('delete_but').click();
      window.domAutomationController.send('done');
    """
    self.ExecuteJavascript(js_code, tab_index)
    _CheckStatus('File not found', 'Delete non-existing')

    # Check that saving works
    js_code = """
      document.getElementById('save_but').click();
      window.domAutomationController.send('done');
    """
    self.ExecuteJavascript(js_code, tab_index)
    _CheckStatus('Save successful', 'Save test')

    # Check that we load what we saved
    js_code = """
      document.getElementById('file_editor').value = 'different';
      document.getElementById('load_but').click();
      window.domAutomationController.send('done');
    """
    self.ExecuteJavascript(js_code, tab_index)
    _CheckStatus('Load complete', 'Load test')
    self.assertTrue(
        self.GetDOMValue('document.getElementById("file_editor").value',
          tab_index).find('test') != -1, msg='Loaded wrong text or failed')

    # Check that we delete files successfully
    js_code = """
      document.getElementById('delete_but').click();
      window.domAutomationController.send('done');
    """
    self.ExecuteJavascript(js_code, tab_index)
    _CheckStatus('File deleted', 'Delete test')

    # Check that file is deleted and load produces not found
    js_code = """
      document.getElementById('load_but').click();
      window.domAutomationController.send('done');
    """
    self.ExecuteJavascript(js_code, tab_index)
    _CheckStatus('File not found', 'Load deleted test')

  def _VerifyWebSocketExample(self, tab_index, name, url):
    """Verify Web Socket Open Example.

    Args:
      tab_index: Tab index integer that the example is on.
      name: A string name of the example.
      url: A string url of the example.
    """
    # Check if example is loaded.
    success = self.WaitUntil(
        lambda: self.GetDOMValue(
            'document.getElementById("statusField").innerHTML', tab_index),
            expect_retval='SUCCESS')
    self.assertTrue(success, msg='Example %s failed. URL: %s' % (name, url))

    # Simulate clicking on Connect button to establish a connection.
    js_code = """
      document.getElementsByTagName('input')[1].click();
      window.domAutomationController.send('done');
    """
    self.ExecuteJavascript(js_code, tab_index)

    # Check if connected
    success = self._VerifyElementPresent('log', 'connected', tab_index,
        msg='Example %s failed. URL: %s' % (name, url))

    # Simulate clicking on Send button to send text message in log.
    js_code = """
      document.getElementsByTagName('input')[3].click();
      window.domAutomationController.send('done');
    """
    self.ExecuteJavascript(js_code, tab_index)
    success = self.WaitUntil(
        lambda: bool(re.search('send:', self.GetDOMValue(
            'document.getElementById("log").textContent', tab_index))))
    self.assertTrue(success, msg='Example %s failed. URL: %s' % (name, url))

  def _VerifyDynamicLibraryOpen(self, tab_index, name, url):
    """Verify Dynamic Library Open Example.

    Args:
      tab_index: Tab index integer that the example is on.
      name: A string name of the example.
      url: A string url of the example.
    """
    # Check if example is loaded.
    success = self._VerifyElementPresent('log', 'Eightball loaded!',
        tab_index, msg='Example %s failed. URL: %s' % (name, url))

    # Simulate clicking on ASK button and check answer log for desired answer.
    js_code = """
      document.getElementsByTagName('input')[1].click();
      window.domAutomationController.send('done');
    """
    self.ExecuteJavascript(js_code, tab_index)
    def _CheckAnswerLog():
      return bool(re.search(r'NO|YES|42|MAYBE NOT|DEFINITELY|'
        'ASK ME TOMORROW|MAYBE|PARTLY CLOUDY',
        self.GetDOMValue('document.getElementById("log").innerHTML',
        tab_index)))

    success = self.WaitUntil(_CheckAnswerLog)
    self.assertTrue(success, msg='Example %s failed. URL: %s' % (name, url))

  def _VerifyLoadProgressExample(self, tab_index, name, url):
    """Verify Dynamic Library Open Example.

    Args:
      tab_index: Tab index integer that the example is on.
      name: A string name of the example.
      url: A string url of the example.
    """
    # Check if example loads and displays loading progress.
    success = self.WaitUntil(
        lambda: self.GetDOMValue(
        'document.getElementById("statusField").innerHTML', tab_index),
        timeout=150, expect_retval='SUCCESS')
    self.assertTrue(success, msg='Example %s failed. URL: %s' % (name, url))

    def _CheckLoadProgressStatus():
      return re.search(
          r'(loadstart).+(progress:).+(load).+(loadend).+(lastError:)',
          self.GetDOMValue(
          'document.getElementById("log").innerHTML', tab_index))
    success = self.WaitUntil(_CheckLoadProgressStatus)
    self.assertTrue(success, msg='Example %s failed. URL: %s' % (name, url))

  def _VerifyPiGeneratorExample(self, tab_index, name, url):
    """Verify Pi Generator Example.

    Args:
      tab_index: Tab index integer that the example is on.
      name: A string name of the example.
      url: A string url of the example.
    """
    success = self.WaitUntil(
        lambda: self.GetDOMValue('document.getElementById("pi").value',
            tab_index)[0:3],
        expect_retval='3.1')
    self.assertTrue(success, msg='Example %s failed. URL: %s' % (name, url))

  def _VerifySineSynthExample(self, tab_index, name, url):
    """Verify Sine Wave Synthesizer Example.

    Args:
      tab_index: Tab index integer that the example is on.
      name: A string name of the example.
      url: A string url of the example.
    """
    success = self.WaitUntil(
        lambda: self.GetDOMValue(
                    'document.getElementById("frequency_field").value',
                    tab_index), timeout=150, expect_retval='440')
    self.assertTrue(success, msg='Example %s failed. URL: %s' % (name, url))
    self.ExecuteJavascript(
        'document.body.getElementsByTagName("button")[0].click();'
        'window.domAutomationController.send("done")',
        tab_index)

  def _VerifyGetURLExample(self, tab_index, name, url):
    """Verify GetURL Example.

    Args:
      tab_index: Tab index integer that the example is on.
      name: A string name of the example.
      url: A string url of the example.
    """
    success = self.WaitUntil(
        lambda: self.GetDOMValue(
                    'document.getElementById("statusField").innerHTML',
                    tab_index), timeout=150, expect_retval='SUCCESS')
    self.assertTrue(success, msg='Example %s failed. URL: %s' % (name, url))
    self.ExecuteJavascript(
        'document.getElementById("button").click();'
        'window.domAutomationController.send("done")',
        tab_index)
    success = self._VerifyElementPresent('general_output', 'test passed',
        tab_index, msg='Example %s failed. URL: %s' % (name, url))

  def _CheckForCrashes(self):
    """Check for any browser/tab crashes and hangs."""
    self.assertTrue(self.GetBrowserWindowCount(),
                    msg='Browser crashed, no window is open.')

    info = self.GetBrowserInfo()
    breakpad_folder = info['properties']['DIR_CRASH_DUMPS']
    old_dmp_files = glob.glob(os.path.join(breakpad_folder, '*.dmp'))

    # Verify there're no crash dump files.
    for dmp_file in glob.glob(os.path.join(breakpad_folder, '*.dmp')):
      self.assertTrue(dmp_file in old_dmp_files,
                      msg='Crash dump %s found' % dmp_file)

    # Check for any crashed tabs.
    tabs = info['windows'][0]['tabs']
    for tab in tabs:
      if tab['url'] != 'about:blank':
        if not self.GetDOMValue('document.body.innerHTML', tab['index']):
          self.fail(msg='Tab crashed on %s' % tab['url'])

  def _GetPlatformArchitecture(self):
    """Get platform architecture.

    Returns:
      A string representing the platform architecture.
    """
    if pyauto.PyUITest.IsWin():
      if os.environ['PROGRAMFILES'] == 'C:\\Program Files (x86)':
        return '64bit'
      else:
        return '32bit'
    elif pyauto.PyUITest.IsMac() or pyauto.PyUITest.IsLinux():
      if platform.machine() == 'x86_64':
        return '64bit'
      else:
        return '32bit'
    return '32bit'

  def _HasPathInTree(self, pattern, is_file, root=os.curdir):
    """Recursively checks if a file/directory matching a pattern exists.

    Args:
      pattern: Pattern of file or directory name.
      is_file: True if looking for file, or False if looking for directory.
      root: Directory to start looking.

    Returns:
      True, if root contains the directory name pattern, or
      False otherwise.
    """
    for path, dirs, files in os.walk(os.path.abspath(root)):
      if is_file:
        if len(fnmatch.filter(files, pattern)):
          return True
      else:
        if len(fnmatch.filter(dirs, pattern)):
          return True
    return False

  def _HasAllSystemRequirements(self):
    """Verify NaCl SDK installation system requirements.

    Returns:
        True, if system passed requirements, or
        False otherwise.
    """
    # Check python version.
    if sys.version_info[0:2] < (2, 6):
      return False

    # Check OS requirements.
    if pyauto.PyUITest.IsMac():
      mac_min_version = version.StrictVersion('10.6')
      mac_version = version.StrictVersion(platform.mac_ver()[0])
      if mac_version < mac_min_version:
        return False
    elif pyauto.PyUITest.IsWin():
      if not (self.IsWin7() or self.IsWinVista() or self.IsWinXP()):
        return False
    elif pyauto.PyUITest.IsLinux():
      pass  # TODO(chrisphan): Check Lin requirements.
    else:
      return False

    # Check for Chrome version compatibility.
    # NaCl supports Chrome 10 and higher builds.
    min_required_chrome_build = self._settings['min_required_chrome_build']
    browser_info = self.GetBrowserInfo()
    chrome_version = browser_info['properties']['ChromeVersion']
    chrome_build = int(chrome_version.split('.')[0])
    return chrome_build >= min_required_chrome_build

  def _DownloadNaClSDK(self):
    """Download NaCl SDK."""
    self._temp_dir = tempfile.mkdtemp()
    dl_file = urllib2.urlopen(self._settings['post_sdk_zip'])
    file_path = os.path.join(self._temp_dir, 'nacl_sdk.zip')

    try:
      f = open(file_path, 'wb')
      f.write(dl_file.read())
    except IOError:
      self.fail(msg='Cannot open %s.' % file_path)
    finally:
      f.close()

  def _ExtractNaClSDK(self):
    """Extract NaCl SDK."""
    source_file = os.path.join(self._temp_dir, 'nacl_sdk.zip')
    if zipfile.is_zipfile(source_file):
      zip = zipfile.ZipFile(source_file, 'r')
      zip.extractall(self._extracted_sdk_path)
    else:
      self.fail(msg='%s is not a valid zip file' % source_file)

  def _IsURLAlive(self, url):
    """Test if URL is alive."""
    try:
      urllib2.urlopen(url)
    except:
      return False
    return True

  def _CloseHTTPServer(self, proc=None):
    """Close HTTP server.

    Args:
      proc: Process that opened the HTTP server.
      proc is None when there is no pointer to HTTP server process.
    """
    if not self._IsURLAlive('http://localhost:5103'):
      return
    response = urllib2.urlopen('http://localhost:5103')
    html = response.read()
    if not 'Native Client' in html:
      self.fail(msg='Port 5103 is in use.')

    urllib2.urlopen('http://localhost:5103?quit=1')
    success = self.WaitUntil(
        lambda: self._IsURLAlive('http://localhost:5103'),
        retry_sleep=1, expect_retval=False)
    if not success:
      if not proc:
        self.fail(msg='Failed to close HTTP server.')
      else:
        if proc.poll() == None:
          try:
            proc.kill()
          except:
            self.fail(msg='Failed to close HTTP server.')

  def _SearchNaClSDKFile(self, search_list):
    """Search NaCl SDK file for example files and directories in Windows.

    Args:
      search_list: A list of strings, representing file and
                   directory names for which to search.
    """
    missing_items = []
    for name in search_list:
      is_file = name.find('/') < 0
      if not is_file:
        name = name.replace('/', '')
      if not self._HasPathInTree(name, is_file, self._extracted_sdk_path):
        missing_items.append(name)
    self.assertEqual(len(missing_items), 0,
                     msg='Missing files or directories: %s' %
                         ', '.join(map(str, missing_items)))

  def ExtraChromeFlags(self):
    """Ensures Nacl is enabled.

    Returns:
      A list of extra flags to pass to Chrome when it is launched.
    """
    extra_chrome_flags = [
        '--enable-nacl',
        '--enable-nacl-exception-handling',
        '--nacl-gdb',
      ]
    return pyauto.PyUITest.ExtraChromeFlags(self) + extra_chrome_flags

if __name__ == '__main__':
  pyauto_functional.Main()
