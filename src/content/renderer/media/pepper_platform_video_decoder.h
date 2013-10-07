// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_MEDIA_PEPPER_PLATFORM_VIDEO_DECODER_H_
#define CONTENT_RENDERER_MEDIA_PEPPER_PLATFORM_VIDEO_DECODER_H_

#include <vector>

#include "base/compiler_specific.h"
#include "base/memory/ref_counted.h"
#include "base/message_loop/message_loop.h"
#include "media/video/video_decode_accelerator.h"

namespace content {

class PlatformVideoDecoder : public media::VideoDecodeAccelerator,
                             public media::VideoDecodeAccelerator::Client {
 public:
  PlatformVideoDecoder(media::VideoDecodeAccelerator::Client* client,
                       int32 command_buffer_route_id);
  virtual ~PlatformVideoDecoder();

  // PlatformVideoDecoder (a.k.a. VideoDecodeAccelerator) implementation.
  virtual bool Initialize(media::VideoCodecProfile profile) OVERRIDE;
  virtual void Decode(
      const media::BitstreamBuffer& bitstream_buffer) OVERRIDE;
  virtual void AssignPictureBuffers(
      const std::vector<media::PictureBuffer>& buffers) OVERRIDE;
  virtual void ReusePictureBuffer(int32 picture_buffer_id) OVERRIDE;
  virtual void Flush() OVERRIDE;
  virtual void Reset() OVERRIDE;
  virtual void Destroy() OVERRIDE;

  // VideoDecodeAccelerator::Client implementation.
  virtual void ProvidePictureBuffers(uint32 requested_num_of_buffers,
                                     const gfx::Size& dimensions,
                                     uint32 texture_target) OVERRIDE;
  virtual void PictureReady(const media::Picture& picture) OVERRIDE;
  virtual void DismissPictureBuffer(int32 picture_buffer_id) OVERRIDE;
  virtual void NotifyInitializeDone() OVERRIDE;
  virtual void NotifyError(
      media::VideoDecodeAccelerator::Error error) OVERRIDE;
  virtual void NotifyEndOfBitstreamBuffer(int32 bitstream_buffer_id) OVERRIDE;
  virtual void NotifyFlushDone() OVERRIDE;
  virtual void NotifyResetDone() OVERRIDE;

 private:
  // Client lifetime must exceed lifetime of this class.
  // TODO(vrk/fischman): We should take another look at the overall
  // arcitecture of PPAPI Video Decode to make sure lifetime/ownership makes
  // sense, including lifetime of this client.
  media::VideoDecodeAccelerator::Client* client_;

  // Route ID for the command buffer associated with video decoder's context.
  int32 command_buffer_route_id_;

  // Holds a GpuVideoDecodeAcceleratorHost.
  scoped_ptr<media::VideoDecodeAccelerator> decoder_;

  DISALLOW_COPY_AND_ASSIGN(PlatformVideoDecoder);
};

}  // namespace content

#endif  // CONTENT_RENDERER_MEDIA_PEPPER_PLATFORM_VIDEO_DECODER_H_
