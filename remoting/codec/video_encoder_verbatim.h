// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_CODEC_VIDEO_ENCODER_VERBATIM_H_
#define REMOTING_CODEC_VIDEO_ENCODER_VERBATIM_H_

#include "base/time/time.h"
#include "remoting/codec/video_encoder.h"
#include "remoting/proto/video.pb.h"
#include "third_party/webrtc/modules/desktop_capture/desktop_geometry.h"

namespace remoting {

// VideoEncoderVerbatim implements a VideoEncoder that sends image data as a
// sequence of RGB values, without compression.
class VideoEncoderVerbatim : public VideoEncoder {
 public:
  VideoEncoderVerbatim();
  virtual ~VideoEncoderVerbatim();

  // Sets maximum size of data in video packets. Used by unittests.
  void SetMaxPacketSize(int size);

  // VideoEncoder interface.
  virtual void Encode(
      const webrtc::DesktopFrame* frame,
      const DataAvailableCallback& data_available_callback) OVERRIDE;

 private:
  // Encode a single dirty |rect|.
  void EncodeRect(const webrtc::DesktopFrame* frame,
                  const webrtc::DesktopRect& rect,
                  bool last);

  // Initializes first packet in a sequence of video packets to update screen
  // rectangle |rect|.
  void PrepareUpdateStart(const webrtc::DesktopFrame* frame,
                          const webrtc::DesktopRect& rect,
                          VideoPacket* packet);

  // Allocates a buffer of the specified |size| inside |packet| and returns the
  // pointer to it.
  uint8* GetOutputBuffer(VideoPacket* packet, size_t size);

  // Submit |packet| to |callback_|.
  void SubmitMessage(VideoPacket* packet, size_t rect_index);

  DataAvailableCallback callback_;
  base::Time encode_start_time_;

  // The most recent screen size.
  webrtc::DesktopSize screen_size_;

  int max_packet_size_;
};

}  // namespace remoting

#endif  // REMOTING_CODEC_VIDEO_ENCODER_VERBATIM_H_
