// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/protocol/webrtc_video_capturer_adapter.h"

#include "third_party/webrtc/modules/desktop_capture/desktop_frame.h"

namespace remoting {

// Number of frames to be captured per second.
const int kFramesPerSec = 10;

WebrtcVideoCapturerAdapter::WebrtcVideoCapturerAdapter(
    scoped_ptr<webrtc::DesktopCapturer> capturer)
    : desktop_capturer_(capturer.Pass()) {
  DCHECK(desktop_capturer_);

  thread_checker_.DetachFromThread();

  // Disable video adaptation since we don't intend to use it.
  set_enable_video_adapter(false);
}

WebrtcVideoCapturerAdapter::~WebrtcVideoCapturerAdapter() {
  DCHECK(!capture_timer_);
}

webrtc::SharedMemory* WebrtcVideoCapturerAdapter::CreateSharedMemory(
    size_t size) {
  return nullptr;
}

void WebrtcVideoCapturerAdapter::OnCaptureCompleted(
    webrtc::DesktopFrame* frame) {
  scoped_ptr<webrtc::DesktopFrame> owned_frame(frame);

  // Drop the owned_frame if there were no changes.
  if (!owned_frame || owned_frame->updated_region().is_empty()) {
    owned_frame.reset();
    return;
  }

  // Convert the webrtc::DesktopFrame to a cricket::CapturedFrame.
  cricket::CapturedFrame captured_frame;
  captured_frame.width = owned_frame->size().width();
  captured_frame.height = owned_frame->size().height();
  base::TimeTicks current_time = base::TimeTicks::Now();
  captured_frame.time_stamp =
      current_time.ToInternalValue() * base::Time::kNanosecondsPerMicrosecond;
  captured_frame.data = owned_frame->data();

  // The data_size attribute must be set. If multiple formats are supported,
  // this should be set appropriately for each one.
  captured_frame.data_size =
      (captured_frame.width * webrtc::DesktopFrame::kBytesPerPixel * 8 + 7) /
      8 * captured_frame.height;
  captured_frame.fourcc = cricket::FOURCC_ARGB;

  SignalFrameCaptured(this, &captured_frame);
}

bool WebrtcVideoCapturerAdapter::GetBestCaptureFormat(
    const cricket::VideoFormat& desired,
    cricket::VideoFormat* best_format) {
  DCHECK(thread_checker_.CalledOnValidThread());

  // For now, just used the desired width and height.
  best_format->width = desired.width;
  best_format->height = desired.height;
  best_format->fourcc = cricket::FOURCC_ARGB;
  best_format->interval = FPS_TO_INTERVAL(kFramesPerSec);
  return true;
}

cricket::CaptureState WebrtcVideoCapturerAdapter::Start(
    const cricket::VideoFormat& capture_format) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!capture_timer_);
  DCHECK_EQ(capture_format.fourcc, (static_cast<uint32>(cricket::FOURCC_ARGB)));

  if (!desktop_capturer_) {
    VLOG(1) << "WebrtcVideoCapturerAdapter failed to start.";
    return cricket::CS_FAILED;
  }

  // This is required to tell the cricket::VideoCapturer base class what the
  // capture format will be.
  SetCaptureFormat(&capture_format);

  desktop_capturer_->Start(this);

  capture_timer_.reset(new base::RepeatingTimer());
  capture_timer_->Start(FROM_HERE,
                        base::TimeDelta::FromMicroseconds(
                            GetCaptureFormat()->interval /
                            (base::Time::kNanosecondsPerMicrosecond)),
                        this,
                        &WebrtcVideoCapturerAdapter::CaptureNextFrame);

  return cricket::CS_RUNNING;
}

// Similar to the base class implementation with some important differences:
// 1. Does not call either Stop() or Start(), as those would affect the state of
// |desktop_capturer_|.
// 2. Does not support unpausing after stopping the capturer. It is unclear
// if that flow needs to be supported.
bool WebrtcVideoCapturerAdapter::Pause(bool pause) {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (pause) {
    if (capture_state() == cricket::CS_PAUSED)
      return true;

    bool running = capture_state() == cricket::CS_STARTING ||
                   capture_state() == cricket::CS_RUNNING;

    DCHECK_EQ(running, IsRunning());

    if (!running) {
      LOG(ERROR)
          << "Cannot pause WebrtcVideoCapturerAdapter.";
      return false;
    }

    // Stop |capture_timer_| and set capture state to cricket::CS_PAUSED.
    capture_timer_->Stop();
    SetCaptureState(cricket::CS_PAUSED);

    VLOG(1) << "WebrtcVideoCapturerAdapter paused.";

    return true;
  } else {  // Unpausing.
    if (capture_state() != cricket::CS_PAUSED || !GetCaptureFormat() ||
        !capture_timer_) {
      LOG(ERROR) << "Cannot unpause WebrtcVideoCapturerAdapter.";
      return false;
    }

    // Restart |capture_timer_| and set capture state to cricket::CS_RUNNING;
    capture_timer_->Start(FROM_HERE,
                          base::TimeDelta::FromMicroseconds(
                              GetCaptureFormat()->interval /
                              (base::Time::kNanosecondsPerMicrosecond)),
                          this,
                          &WebrtcVideoCapturerAdapter::CaptureNextFrame);
    SetCaptureState(cricket::CS_RUNNING);

    VLOG(1) << "WebrtcVideoCapturerAdapter unpaused.";
  }
  return true;
}

void WebrtcVideoCapturerAdapter::Stop() {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK_NE(capture_state(), cricket::CS_STOPPED);

  capture_timer_.reset();

  SetCaptureFormat(nullptr);
  SetCaptureState(cricket::CS_STOPPED);

  VLOG(1) << "WebrtcVideoCapturerAdapter stopped.";
}


bool WebrtcVideoCapturerAdapter::IsRunning() {
  DCHECK(thread_checker_.CalledOnValidThread());

  return capture_timer_->IsRunning();
}

bool WebrtcVideoCapturerAdapter::IsScreencast() const {
  return true;
}

bool WebrtcVideoCapturerAdapter::GetPreferredFourccs(
    std::vector<uint32>* fourccs) {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (!fourccs)
    return false;
  fourccs->push_back(cricket::FOURCC_ARGB);
  return true;
}

void WebrtcVideoCapturerAdapter::CaptureNextFrame() {
  // If we are paused, then don't capture.
  if (!IsRunning())
    return;

  desktop_capturer_->Capture(webrtc::DesktopRegion());
}

}  // namespace remoting
