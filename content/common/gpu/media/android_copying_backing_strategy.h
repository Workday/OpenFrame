// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_GPU_MEDIA_ANDROID_COPYING_BACKING_STRATEGY_H_
#define CONTENT_COMMON_GPU_MEDIA_ANDROID_COPYING_BACKING_STRATEGY_H_

#include "base/compiler_specific.h"
#include "content/common/content_export.h"
#include "content/common/gpu/media/android_video_decode_accelerator.h"

namespace gpu {
class CopyTextureCHROMIUMResourceManager;
}

namespace media {
class PictureBuffer;
}

namespace content {

class AVDAStateProvider;

// A BackingStrategy implementation that copies images to PictureBuffer
// textures via gpu texture copy.
class CONTENT_EXPORT AndroidCopyingBackingStrategy
    : public AndroidVideoDecodeAccelerator::BackingStrategy {
 public:
  AndroidCopyingBackingStrategy();
  ~AndroidCopyingBackingStrategy() override;

  // AndroidVideoDecodeAccelerator::BackingStrategy
  void Initialize(AVDAStateProvider*) override;
  void Cleanup(const AndroidVideoDecodeAccelerator::OutputBufferMap&) override;
  uint32 GetNumPictureBuffers() const override;
  uint32 GetTextureTarget() const override;
  scoped_refptr<gfx::SurfaceTexture> CreateSurfaceTexture() override;
  void UseCodecBufferForPictureBuffer(int32 codec_buffer_index,
                                      const media::PictureBuffer&) override;
  void CodecChanged(
      media::VideoCodecBridge*,
      const AndroidVideoDecodeAccelerator::OutputBufferMap&) override;

 private:
  // Used for copy the texture from surface texture to picture buffers.
  scoped_ptr<gpu::CopyTextureCHROMIUMResourceManager> copier_;

  AVDAStateProvider* state_provider_;

  // A container of texture. Used to set a texture to |media_codec_|.
  scoped_refptr<gfx::SurfaceTexture> surface_texture_;

  // The texture id which is set to |surface_texture_|.
  uint32 surface_texture_id_;

  media::VideoCodecBridge* media_codec_;
};

}  // namespace content

#endif  // CONTENT_COMMON_GPU_MEDIA_ANDROID_COPYING_BACKING_STRATEGY_H_
