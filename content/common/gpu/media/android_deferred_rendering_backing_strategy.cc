// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/gpu/media/android_deferred_rendering_backing_strategy.h"

#include "base/bind.h"
#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "base/metrics/histogram.h"
#include "content/common/gpu/gpu_channel.h"
#include "content/common/gpu/media/avda_codec_image.h"
#include "content/common/gpu/media/avda_return_on_failure.h"
#include "content/common/gpu/media/avda_shared_state.h"
#include "gpu/command_buffer/service/texture_manager.h"
#include "ui/gl/android/surface_texture.h"
#include "ui/gl/gl_bindings.h"

namespace content {

// TODO(liberato): This is an entirely made-up number.  It depends on how
// many decoded buffers that the MediaCodec is willing to have outstanding
// at any one time.  Only one is guaranteed.  crbug.com/531606.
enum { kNumPictureBuffers = 3 };

AndroidDeferredRenderingBackingStrategy::
    AndroidDeferredRenderingBackingStrategy()
    : state_provider_(nullptr), media_codec_(nullptr) {}

AndroidDeferredRenderingBackingStrategy::
    ~AndroidDeferredRenderingBackingStrategy() {}

void AndroidDeferredRenderingBackingStrategy::Initialize(
    AVDAStateProvider* state_provider) {
  state_provider_ = state_provider;
  shared_state_ = new AVDASharedState();
}

void AndroidDeferredRenderingBackingStrategy::Cleanup(
    const AndroidVideoDecodeAccelerator::OutputBufferMap& buffers) {
  for (const std::pair<int, media::PictureBuffer>& entry : buffers) {
    AVDACodecImage* avImage = GetImageForPicture(entry.second);
    if (avImage) {
      avImage->SetMediaCodecBufferIndex(-1);
      avImage->SetMediaCodec(nullptr);
    }
  }
}

uint32 AndroidDeferredRenderingBackingStrategy::GetNumPictureBuffers() const {
  return kNumPictureBuffers;
}

uint32 AndroidDeferredRenderingBackingStrategy::GetTextureTarget() const {
  return GL_TEXTURE_EXTERNAL_OES;
}

scoped_refptr<gfx::SurfaceTexture>
AndroidDeferredRenderingBackingStrategy::CreateSurfaceTexture() {
  // AVDACodecImage will handle attaching this to a texture later.
  surface_texture_ = gfx::SurfaceTexture::Create(0);
  // Detach from our GL context so that the GLImages can attach.  It will
  // silently fail to delete texture 0.
  surface_texture_->DetachFromGLContext();

  return surface_texture_;
}

gpu::gles2::TextureRef*
AndroidDeferredRenderingBackingStrategy::GetTextureForPicture(
    const media::PictureBuffer& picture_buffer) {
  RETURN_NULL_IF_NULL(state_provider_->GetGlDecoder());
  gpu::gles2::TextureManager* texture_manager =
      state_provider_->GetGlDecoder()->GetContextGroup()->texture_manager();
  RETURN_NULL_IF_NULL(texture_manager);
  gpu::gles2::TextureRef* texture_ref =
      texture_manager->GetTexture(picture_buffer.internal_texture_id());
  RETURN_NULL_IF_NULL(texture_ref);

  return texture_ref;
}

AVDACodecImage* AndroidDeferredRenderingBackingStrategy::GetImageForPicture(
    const media::PictureBuffer& picture_buffer) {
  gpu::gles2::TextureRef* texture_ref = GetTextureForPicture(picture_buffer);
  RETURN_NULL_IF_NULL(texture_ref);
  gl::GLImage* image =
      texture_ref->texture()->GetLevelImage(GetTextureTarget(), 0);
  return static_cast<AVDACodecImage*>(image);
}

void AndroidDeferredRenderingBackingStrategy::SetImageForPicture(
    const media::PictureBuffer& picture_buffer,
    const scoped_refptr<gl::GLImage>& image) {
  gpu::gles2::TextureRef* texture_ref = GetTextureForPicture(picture_buffer);
  RETURN_IF_NULL(texture_ref);

  gpu::gles2::TextureManager* texture_manager =
      state_provider_->GetGlDecoder()->GetContextGroup()->texture_manager();
  RETURN_IF_NULL(texture_manager);

  if (image) {
    // Also set the parameters for the level if we're not clearing
    // the image.
    const gfx::Size size = state_provider_->GetSize();
    texture_manager->SetLevelInfo(texture_ref, GetTextureTarget(), 0, GL_RGBA,
                                  size.width(), size.height(), 1, 0, GL_RGBA,
                                  GL_UNSIGNED_BYTE, gfx::Rect());

    texture_manager->SetLevelImage(texture_ref, GetTextureTarget(), 0,
                                   image.get(), gpu::gles2::Texture::UNBOUND);

    static_cast<AVDACodecImage*>(image.get())
        ->setTexture(texture_ref->texture());
  }
}

void AndroidDeferredRenderingBackingStrategy::UseCodecBufferForPictureBuffer(
    int32 codec_buf_index,
    const media::PictureBuffer& picture_buffer) {
  // Make sure that the decoder is available.
  RETURN_IF_NULL(state_provider_->GetGlDecoder());

  // Notify the AVDACodecImage for picture_buffer that it should use the
  // decoded buffer codec_buf_index to render this frame.
  AVDACodecImage* avImage = GetImageForPicture(picture_buffer);
  RETURN_IF_NULL(avImage);
  DCHECK_EQ(avImage->GetMediaCodecBufferIndex(), -1);
  // Note that this is not a race, since we do not re-use a PictureBuffer
  // until after the CC is done drawing it.
  avImage->SetMediaCodecBufferIndex(codec_buf_index);
  avImage->SetSize(state_provider_->GetSize());
}

void AndroidDeferredRenderingBackingStrategy::AssignOnePictureBuffer(
    const media::PictureBuffer& picture_buffer) {
  // Attach a GLImage to each texture that will use the surface texture.
  // We use a refptr here in case SetImageForPicture fails.
  scoped_refptr<gl::GLImage> gl_image(
      new AVDACodecImage(shared_state_, media_codec_,
                         state_provider_->GetGlDecoder(), surface_texture_));
  SetImageForPicture(picture_buffer, gl_image);
}

void AndroidDeferredRenderingBackingStrategy::ReleaseCodecBufferForPicture(
    const media::PictureBuffer& picture_buffer) {
  AVDACodecImage* avImage = GetImageForPicture(picture_buffer);

  // See if there is a media codec buffer still attached to this image.
  const int32 codec_buffer = avImage->GetMediaCodecBufferIndex();

  if (codec_buffer >= 0) {
    // PictureBuffer wasn't displayed, so release the buffer.
    media_codec_->ReleaseOutputBuffer(codec_buffer, false);
    avImage->SetMediaCodecBufferIndex(-1);
  }
}

void AndroidDeferredRenderingBackingStrategy::ReuseOnePictureBuffer(
    const media::PictureBuffer& picture_buffer) {
  // At this point, the CC must be done with the picture.  We can't really
  // check for that here directly.  it's guaranteed in gpu_video_decoder.cc,
  // when it waits on the sync point before releasing the mailbox.  That sync
  // point is inserted by destroying the resource in VideoLayerImpl::DidDraw.
  ReleaseCodecBufferForPicture(picture_buffer);
}

void AndroidDeferredRenderingBackingStrategy::DismissOnePictureBuffer(
    const media::PictureBuffer& picture_buffer) {
  // If there is an outstanding codec buffer attached to this image, then
  // release it.
  ReleaseCodecBufferForPicture(picture_buffer);

  // Paranoia.  The texture will be dropped anyway, causing the picture to be
  // deleted then.
  SetImageForPicture(picture_buffer, 0);

  // TODO(liberato): If we really want to re-use a picture buffer's texture as
  // the surface texture's consumer texture id, then we could manage
  // re-assigning it here.
}

void AndroidDeferredRenderingBackingStrategy::CodecChanged(
    media::VideoCodecBridge* codec,
    const AndroidVideoDecodeAccelerator::OutputBufferMap& buffers) {
  // Clear any outstanding codec buffer indices, since the new codec (if any)
  // doesn't know about them.
  media_codec_ = codec;
  for (const std::pair<int, media::PictureBuffer>& entry : buffers) {
    AVDACodecImage* avImage = GetImageForPicture(entry.second);
    avImage->SetMediaCodec(codec);
    avImage->SetMediaCodecBufferIndex(-1);
  }
}

}  // namespace content
