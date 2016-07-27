// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/gpu/media/android_copying_backing_strategy.h"

#include "base/bind.h"
#include "base/logging.h"
#include "base/trace_event/trace_event.h"
#include "content/common/gpu/media/avda_return_on_failure.h"
#include "gpu/command_buffer/service/gles2_cmd_copy_texture_chromium.h"
#include "gpu/command_buffer/service/gles2_cmd_decoder.h"
#include "media/base/limits.h"
#include "media/video/picture.h"
#include "ui/gl/android/surface_texture.h"
#include "ui/gl/gl_bindings.h"

namespace content {

// TODO(liberato): It is unclear if we have an issue with deadlock during
// playback if we lower this.  Previously (crbug.com/176036), a deadlock
// could occur during preroll.  More recent tests have shown some
// instability with kNumPictureBuffers==2 with similar symptoms
// during playback.  crbug.com/:531588 .
enum { kNumPictureBuffers = media::limits::kMaxVideoFrames + 1 };

const static GLfloat kIdentityMatrix[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                            0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                            0.0f, 0.0f, 0.0f, 1.0f};

AndroidCopyingBackingStrategy::AndroidCopyingBackingStrategy()
    : state_provider_(nullptr), surface_texture_id_(0), media_codec_(nullptr) {}

AndroidCopyingBackingStrategy::~AndroidCopyingBackingStrategy() {}

void AndroidCopyingBackingStrategy::Initialize(
    AVDAStateProvider* state_provider) {
  state_provider_ = state_provider;
}

void AndroidCopyingBackingStrategy::Cleanup(
    const AndroidVideoDecodeAccelerator::OutputBufferMap&) {
  DCHECK(state_provider_->ThreadChecker().CalledOnValidThread());
  if (copier_)
    copier_->Destroy();

  if (surface_texture_id_)
    glDeleteTextures(1, &surface_texture_id_);
}

uint32 AndroidCopyingBackingStrategy::GetNumPictureBuffers() const {
  return kNumPictureBuffers;
}

uint32 AndroidCopyingBackingStrategy::GetTextureTarget() const {
  return GL_TEXTURE_2D;
}

scoped_refptr<gfx::SurfaceTexture>
AndroidCopyingBackingStrategy::CreateSurfaceTexture() {
  glGenTextures(1, &surface_texture_id_);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_EXTERNAL_OES, surface_texture_id_);

  glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  state_provider_->GetGlDecoder()->RestoreTextureUnitBindings(0);
  state_provider_->GetGlDecoder()->RestoreActiveTexture();

  surface_texture_ = gfx::SurfaceTexture::Create(surface_texture_id_);

  return surface_texture_;
}

void AndroidCopyingBackingStrategy::UseCodecBufferForPictureBuffer(
    int32 codec_buf_index,
    const media::PictureBuffer& picture_buffer) {
  // Make sure that the decoder is available.
  RETURN_ON_FAILURE(state_provider_, state_provider_->GetGlDecoder().get(),
                    "Failed to get gles2 decoder instance.", ILLEGAL_STATE);

  // Render the codec buffer into |surface_texture_|, and switch it to be
  // the front buffer.
  // This ignores the emitted ByteBuffer and instead relies on rendering to
  // the codec's SurfaceTexture and then copying from that texture to the
  // client's PictureBuffer's texture.  This means that each picture's data
  // is written three times: once to the ByteBuffer, once to the
  // SurfaceTexture, and once to the client's texture.  It would be nicer to
  // either:
  // 1) Render directly to the client's texture from MediaCodec (one write);
  //    or
  // 2) Upload the ByteBuffer to the client's texture (two writes).
  // Unfortunately neither is possible:
  // 1) MediaCodec's use of SurfaceTexture is a singleton, and the texture
  //    written to can't change during the codec's lifetime.  b/11990461
  // 2) The ByteBuffer is likely to contain the pixels in a vendor-specific,
  //    opaque/non-standard format.  It's not possible to negotiate the
  //    decoder to emit a specific colorspace, even using HW CSC.  b/10706245
  // So, we live with these two extra copies per picture :(
  {
    TRACE_EVENT0("media", "AVDA::ReleaseOutputBuffer");
    media_codec_->ReleaseOutputBuffer(codec_buf_index, true);
  }

  {
    TRACE_EVENT0("media", "AVDA::UpdateTexImage");
    surface_texture_->UpdateTexImage();
  }

  float transfrom_matrix[16];
  surface_texture_->GetTransformMatrix(transfrom_matrix);

  uint32 picture_buffer_texture_id = picture_buffer.texture_id();

  // Defer initializing the CopyTextureCHROMIUMResourceManager until it is
  // needed because it takes 10s of milliseconds to initialize.
  if (!copier_) {
    copier_.reset(new gpu::CopyTextureCHROMIUMResourceManager());
    copier_->Initialize(state_provider_->GetGlDecoder().get());
  }

  // Here, we copy |surface_texture_id_| to the picture buffer instead of
  // setting new texture to |surface_texture_| by calling attachToGLContext()
  // because:
  // 1. Once we call detachFrameGLContext(), it deletes the texture previously
  //    attached.
  // 2. SurfaceTexture requires us to apply a transform matrix when we show
  //    the texture.
  // TODO(hkuang): get the StreamTexture transform matrix in GPU process
  // instead of using default matrix crbug.com/226218.
  copier_->DoCopyTextureWithTransform(
      state_provider_->GetGlDecoder().get(), GL_TEXTURE_EXTERNAL_OES,
      surface_texture_id_, picture_buffer_texture_id,
      state_provider_->GetSize().width(), state_provider_->GetSize().height(),
      false, false, false, kIdentityMatrix);
}

void AndroidCopyingBackingStrategy::CodecChanged(
    media::VideoCodecBridge* codec,
    const AndroidVideoDecodeAccelerator::OutputBufferMap&) {
  media_codec_ = codec;
}

}  // namespace content
