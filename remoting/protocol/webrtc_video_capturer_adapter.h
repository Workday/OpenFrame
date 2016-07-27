// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_PROTOCOL_WEBRTC_VIDEO_CAPTURER_ADAPTER_H_
#define REMOTING_PROTOCOL_WEBRTC_VIDEO_CAPTURER_ADAPTER_H_

#include <vector>

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/threading/thread_checker.h"
#include "base/timer/timer.h"
#include "third_party/libjingle/source/talk/media/base/videocapturer.h"
#include "third_party/webrtc/modules/desktop_capture/desktop_capturer.h"

namespace base {
class SingleThreadTaskRunner;
}  // namespace base

namespace webrtc {
class DesktopFrame;
}  // namespace webrtc

namespace remoting {

// This class controls the capture of video frames from the desktop and is used
// to construct a VideoSource as part of the webrtc PeerConnection API.
// WebrtcVideoCapturerAdapter acts as an adapter between webrtc::DesktopCapturer
// and the cricket::VideoCapturer interface, which it implements. It is used
// to construct a cricket::VideoSource for a PeerConnection, to capture frames
// of the desktop. As indicated in the base implementation, Start() and Stop()
// should be called on the same thread.
class WebrtcVideoCapturerAdapter : public cricket::VideoCapturer,
                                   public webrtc::DesktopCapturer::Callback {
 public:
  explicit WebrtcVideoCapturerAdapter(
      scoped_ptr<webrtc::DesktopCapturer> capturer);

  ~WebrtcVideoCapturerAdapter() override;

  // webrtc::DesktopCapturer::Callback implementation.
  webrtc::SharedMemory* CreateSharedMemory(size_t size) override;
  // Converts |frame| to a cricket::CapturedFrame and emits that via
  // SignalFrameCaptured for the base::VideoCapturer implementation to process.
  void OnCaptureCompleted(webrtc::DesktopFrame* frame) override;

  // cricket::VideoCapturer implementation.
  bool GetBestCaptureFormat(const cricket::VideoFormat& desired,
                            cricket::VideoFormat* best_format) override;
  cricket::CaptureState Start(
      const cricket::VideoFormat& capture_format) override;
  bool Pause(bool pause) override;
  void Stop() override;
  bool IsRunning() override;
  bool IsScreencast() const override;
  bool GetPreferredFourccs(std::vector<uint32>* fourccs) override;

 private:
  // Kicks off the next frame capture using |desktop_capturer_|.
  // The captured frame will be passed to OnCaptureCompleted().
  void CaptureNextFrame();

  // |thread_checker_| is bound to the peer connection worker thread.
  base::ThreadChecker thread_checker_;

  // Used to capture frames.
  scoped_ptr<webrtc::DesktopCapturer> desktop_capturer_;

  // Used to schedule periodic screen captures.
  scoped_ptr<base::RepeatingTimer> capture_timer_;

  DISALLOW_COPY_AND_ASSIGN(WebrtcVideoCapturerAdapter);
};

}  // namespace remoting

#endif  // REMOTING_PROTOCOL_WEBRTC_VIDEO_CAPTURER_ADAPTER_H_

