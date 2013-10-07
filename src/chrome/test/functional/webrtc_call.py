#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import time

# This little construct ensures we can run even if we have a bad version of
# psutil installed. If so, we'll just skip the test that needs it.
_HAS_CORRECT_PSUTIL_VERSION = False
try:
  import psutil
  if 'version_info' in dir(psutil):
    # If psutil has any version info at all, it's recent enough.
    _HAS_CORRECT_PSUTIL_VERSION = True
except ImportError, e:
  pass


# Note: pyauto_functional must come before pyauto.
import pyauto_functional
import pyauto
import pyauto_utils
import webrtc_test_base


class WebrtcCallTest(webrtc_test_base.WebrtcTestBase):
  """Test we can set up a WebRTC call and disconnect it.

  Prerequisites: This test case must run on a machine with a webcam, either
  fake or real, and with some kind of audio device. You must make the
  peerconnection_server target before you run.

  The test case will launch a custom binary
  (peerconnection_server) which will allow two WebRTC clients to find each
  other. For more details, see the source code which is available at the site
  http://code.google.com/p/libjingle/source/browse/ (make sure to browse to
  trunk/talk/examples/peerconnection/server).
  """

  def setUp(self):
    pyauto.PyUITest.setUp(self)
    self.StartPeerConnectionServer()

  def tearDown(self):
    self.StopPeerConnectionServer()

    pyauto.PyUITest.tearDown(self)
    self.assertEquals('', self.CheckErrorsAndCrashes())

  def _SimpleWebrtcCall(self, request_video, request_audio, duration_seconds=0):
    """Tests we can call and hang up with WebRTC.

    This test exercises pretty much the whole happy-case for the WebRTC
    JavaScript API. Currently, it exercises a normal call setup using the API
    defined at http://dev.w3.org/2011/webrtc/editor/webrtc.html. The API is
    still evolving.

    The test will load the supplied HTML file, which in turn will load different
    javascript files depending on which version of the signaling protocol
    we are running.
    The supplied HTML file will be loaded in two tabs and tell the web
    pages to start up WebRTC, which will acquire video and audio devices on the
    system. This will launch a dialog in Chrome which we click past using the
    automation controller. Then, we will order both tabs to connect the server,
    which will make the two tabs aware of each other. Once that is done we order
    one tab to call the other.

    We make sure that the javascript tells us that the call succeeded, lets it
    run for a while and try to hang up the call after that. We verify video is
    playing by using the video detector.

    Args:
      request_video: Whether to request video.
      request_audio: Whether to request audio.
      duration_seconds: The number of seconds to keep the call up before
        shutting it down.
    """
    self._SetupCall(request_video=request_video, request_audio=request_audio)

    if duration_seconds:
      print 'Call up: sleeping %d seconds...' % duration_seconds
      time.sleep(duration_seconds);

    # The hang-up will automatically propagate to the second tab.
    self.HangUp(from_tab_with_index=0)
    self.WaitUntilHangUpVerified(tab_index=1)

    self.Disconnect(tab_index=0)
    self.Disconnect(tab_index=1)

    # Ensure we didn't miss any errors.
    self.AssertNoFailures(tab_index=0)
    self.AssertNoFailures(tab_index=1)

  def testWebrtcCall(self):
    self.LoadTestPageInTwoTabs()
    self._SimpleWebrtcCall(request_video=True, request_audio=True)

  def testWebrtcVideoOnlyCall(self):
    self.LoadTestPageInTwoTabs()
    self._SimpleWebrtcCall(request_video=True, request_audio=False)

  def testWebrtcAudioOnlyCall(self):
    self.LoadTestPageInTwoTabs()
    self._SimpleWebrtcCall(request_video=False, request_audio=True)

  def testWebrtcJsep01CallAndMeasureCpu20Seconds(self):
    if not _HAS_CORRECT_PSUTIL_VERSION:
      print ('WARNING: Can not run cpu/mem measurements with this version of '
             'psutil. You must have at least psutil 0.4.1 installed for the '
             'version of python you are running this test with.')
      return

    self.LoadTestPageInTwoTabs(test_page='webrtc_jsep01_test.html')

    # Prepare CPU measurements.
    renderer_process = self._GetChromeRendererProcess(tab_index=0)
    renderer_process.get_cpu_percent()

    self._SimpleWebrtcCall(request_video=True,
                           request_audio=True,
                           duration_seconds=20)

    cpu_usage = renderer_process.get_cpu_percent(interval=0)
    mem_usage_bytes = renderer_process.get_memory_info()[0]
    mem_usage_kb = float(mem_usage_bytes) / 1024
    pyauto_utils.PrintPerfResult('cpu', 'jsep01_call', cpu_usage, '%')
    pyauto_utils.PrintPerfResult('memory', 'jsep01_call', mem_usage_kb, 'KiB')

  def testLocalPreview(self):
    """Brings up a local preview and ensures video is playing.

    This test will launch a window with a single tab and run a getUserMedia call
    which will give us access to the webcam and microphone. Then the javascript
    code will hook up the webcam data to the local-view video tag. We will
    detect video in that tag using the video detector, and if we see video
    moving the test passes.
    """
    self.LoadTestPageInOneTab()
    self.assertEquals('ok-got-stream', self.GetUserMedia(tab_index=0))
    self._StartDetectingVideo(tab_index=0, video_element='local-view')

    self._WaitForVideo(tab_index=0, expect_playing=True)

  def testHandlesNewGetUserMediaRequestSeparately(self):
    """Ensures WebRTC doesn't allow new requests to piggy-back on old ones."""
    self.LoadTestPageInTwoTabs()

    self.GetUserMedia(tab_index=0)
    self.GetUserMedia(tab_index=1)
    self.Connect("user_1", tab_index=0)
    self.Connect("user_2", tab_index=1)

    self.CreatePeerConnection(tab_index=0)
    self.AddUserMediaLocalStream(tab_index=0)
    self.EstablishCall(from_tab_with_index=0, to_tab_with_index=1)

    self.assertEquals('failed-with-error-PERMISSION_DENIED',
                      self.GetUserMedia(tab_index=0, action='cancel'))
    self.assertEquals('failed-with-error-PERMISSION_DENIED',
                      self.GetUserMedia(tab_index=0, action='dismiss'))

  def _SetupCall(self, request_video, request_audio):
    """Gets user media and establishes a call.

    Assumes that two tabs are already opened with a suitable test page.

    Args:
      request_video: Whether to request video.
      request_audio: Whether to request audio.
    """
    self.assertEquals('ok-got-stream', self.GetUserMedia(
        tab_index=0, request_video=request_video, request_audio=request_audio))
    self.assertEquals('ok-got-stream', self.GetUserMedia(
        tab_index=1, request_video=request_video, request_audio=request_audio))
    self.Connect('user_1', tab_index=0)
    self.Connect('user_2', tab_index=1)

    self.CreatePeerConnection(tab_index=0)
    self.AddUserMediaLocalStream(tab_index=0)
    self.EstablishCall(from_tab_with_index=0, to_tab_with_index=1)

    if request_video:
      self._StartDetectingVideo(tab_index=0, video_element='remote-view')
      self._StartDetectingVideo(tab_index=1, video_element='remote-view')

      self._WaitForVideo(tab_index=0, expect_playing=True)
      self._WaitForVideo(tab_index=1, expect_playing=True)

  def _StartDetectingVideo(self, tab_index, video_element):
    self.assertEquals('ok-started', self.ExecuteJavascript(
        'startDetection("%s", "frame-buffer", 320, 240)' % video_element,
        tab_index=tab_index));

  def _WaitForVideo(self, tab_index, expect_playing):
    # TODO(phoglund): Remove this hack if we manage to get a more stable Linux
    # bot to run these tests.
    if self.IsLinux():
      print "Linux; pretending to wait for video..."
      time.sleep(1)
      return

    expect_retval='video-playing' if expect_playing else 'video-not-playing'

    video_playing = self.WaitUntil(
        function=lambda: self.ExecuteJavascript('isVideoPlaying()',
                                                tab_index=tab_index),
        expect_retval=expect_retval)
    self.assertTrue(video_playing,
                    msg= 'Timed out while waiting for isVideoPlaying to ' +
                         'return ' + expect_retval + '.')

  def _GetChromeRendererProcess(self, tab_index):
    """Returns the Chrome renderer process as a psutil process wrapper."""
    tab_info = self.GetBrowserInfo()['windows'][0]['tabs'][tab_index]
    renderer_id = tab_info['renderer_pid']
    if not renderer_id:
      self.fail('Can not find the tab renderer process.')
    return psutil.Process(renderer_id)


if __name__ == '__main__':
  pyauto_functional.Main()
