// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/video_scheduler.h"

#include <algorithm>

#include "base/bind.h"
#include "base/callback.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop_proxy.h"
#include "base/stl_util.h"
#include "base/sys_info.h"
#include "base/time/time.h"
#include "remoting/proto/control.pb.h"
#include "remoting/proto/internal.pb.h"
#include "remoting/proto/video.pb.h"
#include "remoting/protocol/cursor_shape_stub.h"
#include "remoting/protocol/message_decoder.h"
#include "remoting/protocol/util.h"
#include "remoting/protocol/video_stub.h"
#include "third_party/webrtc/modules/desktop_capture/desktop_frame.h"
#include "third_party/webrtc/modules/desktop_capture/mouse_cursor_shape.h"
#include "third_party/webrtc/modules/desktop_capture/screen_capturer.h"

namespace remoting {

// Maximum number of frames that can be processed simultaneously.
// TODO(hclam): Move this value to CaptureScheduler.
static const int kMaxPendingFrames = 2;

VideoScheduler::VideoScheduler(
    scoped_refptr<base::SingleThreadTaskRunner> capture_task_runner,
    scoped_refptr<base::SingleThreadTaskRunner> encode_task_runner,
    scoped_refptr<base::SingleThreadTaskRunner> network_task_runner,
    scoped_ptr<webrtc::ScreenCapturer> capturer,
    scoped_ptr<VideoEncoder> encoder,
    protocol::CursorShapeStub* cursor_stub,
    protocol::VideoStub* video_stub)
    : capture_task_runner_(capture_task_runner),
      encode_task_runner_(encode_task_runner),
      network_task_runner_(network_task_runner),
      capturer_(capturer.Pass()),
      encoder_(encoder.Pass()),
      cursor_stub_(cursor_stub),
      video_stub_(video_stub),
      pending_frames_(0),
      capture_pending_(false),
      did_skip_frame_(false),
      is_paused_(false),
      sequence_number_(0) {
  DCHECK(network_task_runner_->BelongsToCurrentThread());
  DCHECK(capturer_);
  DCHECK(encoder_);
  DCHECK(cursor_stub_);
  DCHECK(video_stub_);
}

// Public methods --------------------------------------------------------------

webrtc::SharedMemory* VideoScheduler::CreateSharedMemory(size_t size) {
  return NULL;
}

void VideoScheduler::OnCaptureCompleted(webrtc::DesktopFrame* frame) {
  DCHECK(capture_task_runner_->BelongsToCurrentThread());

  capture_pending_ = false;

  scoped_ptr<webrtc::DesktopFrame> owned_frame(frame);

  if (frame) {
    scheduler_.RecordCaptureTime(
        base::TimeDelta::FromMilliseconds(frame->capture_time_ms()));
  }

  encode_task_runner_->PostTask(
      FROM_HERE, base::Bind(&VideoScheduler::EncodeFrame, this,
                            base::Passed(&owned_frame), sequence_number_));

  // If a frame was skipped, try to capture it again.
  if (did_skip_frame_) {
    capture_task_runner_->PostTask(
        FROM_HERE, base::Bind(&VideoScheduler::CaptureNextFrame, this));
  }
}

void VideoScheduler::OnCursorShapeChanged(
    webrtc::MouseCursorShape* cursor_shape) {
  DCHECK(capture_task_runner_->BelongsToCurrentThread());

  scoped_ptr<webrtc::MouseCursorShape> owned_cursor(cursor_shape);

  // Do nothing if the scheduler is being stopped.
  if (!capturer_)
    return;

  scoped_ptr<protocol::CursorShapeInfo> cursor_proto(
      new protocol::CursorShapeInfo());
  cursor_proto->set_width(cursor_shape->size.width());
  cursor_proto->set_height(cursor_shape->size.height());
  cursor_proto->set_hotspot_x(cursor_shape->hotspot.x());
  cursor_proto->set_hotspot_y(cursor_shape->hotspot.y());
  cursor_proto->set_data(cursor_shape->data);

  network_task_runner_->PostTask(
      FROM_HERE, base::Bind(&VideoScheduler::SendCursorShape, this,
                            base::Passed(&cursor_proto)));
}

void VideoScheduler::Start() {
  DCHECK(network_task_runner_->BelongsToCurrentThread());

  capture_task_runner_->PostTask(
      FROM_HERE, base::Bind(&VideoScheduler::StartOnCaptureThread, this));
}

void VideoScheduler::Stop() {
  DCHECK(network_task_runner_->BelongsToCurrentThread());

  // Clear stubs to prevent further updates reaching the client.
  cursor_stub_ = NULL;
  video_stub_ = NULL;

  capture_task_runner_->PostTask(FROM_HERE,
      base::Bind(&VideoScheduler::StopOnCaptureThread, this));
}

void VideoScheduler::Pause(bool pause) {
  if (!capture_task_runner_->BelongsToCurrentThread()) {
    DCHECK(network_task_runner_->BelongsToCurrentThread());
    capture_task_runner_->PostTask(
        FROM_HERE, base::Bind(&VideoScheduler::Pause, this, pause));
    return;
  }

  if (is_paused_ != pause) {
    is_paused_ = pause;

    // Restart captures if we're resuming and there are none scheduled.
    if (!is_paused_ && capture_timer_ && !capture_timer_->IsRunning())
      CaptureNextFrame();
  }
}

void VideoScheduler::UpdateSequenceNumber(int64 sequence_number) {
  if (!capture_task_runner_->BelongsToCurrentThread()) {
    DCHECK(network_task_runner_->BelongsToCurrentThread());
    capture_task_runner_->PostTask(
        FROM_HERE, base::Bind(&VideoScheduler::UpdateSequenceNumber,
                              this, sequence_number));
    return;
  }

  sequence_number_ = sequence_number;
}

// Private methods -----------------------------------------------------------

VideoScheduler::~VideoScheduler() {
}

// Capturer thread -------------------------------------------------------------

void VideoScheduler::StartOnCaptureThread() {
  DCHECK(capture_task_runner_->BelongsToCurrentThread());
  DCHECK(!capture_timer_);

  // Start the capturer and let it notify us if cursor shape changes.
  capturer_->SetMouseShapeObserver(this);
  capturer_->Start(this);

  capture_timer_.reset(new base::OneShotTimer<VideoScheduler>());

  // Capture first frame immedately.
  CaptureNextFrame();
}

void VideoScheduler::StopOnCaptureThread() {
  DCHECK(capture_task_runner_->BelongsToCurrentThread());

  // This doesn't deleted already captured frames, so encoder can keep using the
  // frames that were captured previously.
  capturer_.reset();

  // |capture_timer_| must be destroyed on the thread on which it is used.
  capture_timer_.reset();
}

void VideoScheduler::ScheduleNextCapture() {
  DCHECK(capture_task_runner_->BelongsToCurrentThread());

  capture_timer_->Start(FROM_HERE,
                        scheduler_.NextCaptureDelay(),
                        this,
                        &VideoScheduler::CaptureNextFrame);
}

void VideoScheduler::CaptureNextFrame() {
  DCHECK(capture_task_runner_->BelongsToCurrentThread());

  // If we are stopping (|capturer_| is NULL), or paused, then don't capture.
  if (!capturer_ || is_paused_)
    return;

  // Make sure we have at most two outstanding recordings. We can simply return
  // if we can't make a capture now, the next capture will be started by the
  // end of an encode operation.
  if (pending_frames_ >= kMaxPendingFrames || capture_pending_) {
    did_skip_frame_ = true;
    return;
  }

  did_skip_frame_ = false;

  // At this point we are going to perform one capture so save the current time.
  pending_frames_++;
  DCHECK_LE(pending_frames_, kMaxPendingFrames);

  // Before doing a capture schedule for the next one.
  ScheduleNextCapture();

  capture_pending_ = true;

  // And finally perform one capture.
  capturer_->Capture(webrtc::DesktopRegion());
}

void VideoScheduler::FrameCaptureCompleted() {
  DCHECK(capture_task_runner_->BelongsToCurrentThread());

  // Decrement the pending capture count.
  pending_frames_--;
  DCHECK_GE(pending_frames_, 0);

  // If we've skipped a frame capture because too we had too many captures
  // pending then schedule one now.
  if (did_skip_frame_)
    CaptureNextFrame();
}

// Network thread --------------------------------------------------------------

void VideoScheduler::SendVideoPacket(scoped_ptr<VideoPacket> packet) {
  DCHECK(network_task_runner_->BelongsToCurrentThread());

  if (!video_stub_)
    return;

  base::Closure callback;
  if ((packet->flags() & VideoPacket::LAST_PARTITION) != 0)
    callback = base::Bind(&VideoScheduler::VideoFrameSentCallback, this);

  video_stub_->ProcessVideoPacket(packet.Pass(), callback);
}

void VideoScheduler::VideoFrameSentCallback() {
  DCHECK(network_task_runner_->BelongsToCurrentThread());

  if (!video_stub_)
    return;

  capture_task_runner_->PostTask(
      FROM_HERE, base::Bind(&VideoScheduler::FrameCaptureCompleted, this));
}

void VideoScheduler::SendCursorShape(
    scoped_ptr<protocol::CursorShapeInfo> cursor_shape) {
  DCHECK(network_task_runner_->BelongsToCurrentThread());

  if (!cursor_stub_)
    return;

  cursor_stub_->SetCursorShape(*cursor_shape);
}

// Encoder thread --------------------------------------------------------------

void VideoScheduler::EncodeFrame(
    scoped_ptr<webrtc::DesktopFrame> frame,
    int64 sequence_number) {
  DCHECK(encode_task_runner_->BelongsToCurrentThread());

  // If there is nothing to encode then send an empty keep-alive packet.
  if (!frame || frame->updated_region().is_empty()) {
    scoped_ptr<VideoPacket> packet(new VideoPacket());
    packet->set_flags(VideoPacket::LAST_PARTITION);
    packet->set_client_sequence_number(sequence_number);
    network_task_runner_->PostTask(
        FROM_HERE, base::Bind(&VideoScheduler::SendVideoPacket, this,
                              base::Passed(&packet)));
    capture_task_runner_->DeleteSoon(FROM_HERE, frame.release());
    return;
  }

  encoder_->Encode(
      frame.get(), base::Bind(&VideoScheduler::EncodedDataAvailableCallback,
                              this, sequence_number));
  capture_task_runner_->DeleteSoon(FROM_HERE, frame.release());
}

void VideoScheduler::EncodedDataAvailableCallback(
    int64 sequence_number,
    scoped_ptr<VideoPacket> packet) {
  DCHECK(encode_task_runner_->BelongsToCurrentThread());

  packet->set_client_sequence_number(sequence_number);

  bool last = (packet->flags() & VideoPacket::LAST_PACKET) != 0;
  if (last) {
    scheduler_.RecordEncodeTime(
        base::TimeDelta::FromMilliseconds(packet->encode_time_ms()));
  }

  network_task_runner_->PostTask(
      FROM_HERE, base::Bind(&VideoScheduler::SendVideoPacket, this,
                            base::Passed(&packet)));
}

}  // namespace remoting
