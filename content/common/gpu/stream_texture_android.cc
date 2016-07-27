// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/gpu/stream_texture_android.h"

#include "base/bind.h"
#include "content/common/android/surface_texture_peer.h"
#include "content/common/gpu/gpu_channel.h"
#include "content/common/gpu/gpu_messages.h"
#include "gpu/command_buffer/service/context_group.h"
#include "gpu/command_buffer/service/context_state.h"
#include "gpu/command_buffer/service/gles2_cmd_decoder.h"
#include "gpu/command_buffer/service/texture_manager.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gl/gl_context.h"
#include "ui/gl/scoped_make_current.h"

namespace content {

using gpu::gles2::ContextGroup;
using gpu::gles2::GLES2Decoder;
using gpu::gles2::TextureManager;
using gpu::gles2::TextureRef;

// static
bool StreamTexture::Create(
    GpuCommandBufferStub* owner_stub,
    uint32 client_texture_id,
    int stream_id) {
  GLES2Decoder* decoder = owner_stub->decoder();
  TextureManager* texture_manager =
      decoder->GetContextGroup()->texture_manager();
  TextureRef* texture = texture_manager->GetTexture(client_texture_id);

  if (texture && (!texture->texture()->target() ||
                  texture->texture()->target() == GL_TEXTURE_EXTERNAL_OES)) {

    // TODO: Ideally a valid image id was returned to the client so that
    // it could then call glBindTexImage2D() for doing the following.
    scoped_refptr<gl::GLImage> gl_image(
        new StreamTexture(owner_stub, stream_id, texture->service_id()));
    gfx::Size size = gl_image->GetSize();
    texture_manager->SetTarget(texture, GL_TEXTURE_EXTERNAL_OES);
    texture_manager->SetLevelInfo(texture, GL_TEXTURE_EXTERNAL_OES, 0, GL_RGBA,
                                  size.width(), size.height(), 1, 0, GL_RGBA,
                                  GL_UNSIGNED_BYTE, gfx::Rect(size));
    texture_manager->SetLevelImage(texture, GL_TEXTURE_EXTERNAL_OES, 0,
                                   gl_image.get(),
                                   gpu::gles2::Texture::UNBOUND);
    return true;
  }

  return false;
}

StreamTexture::StreamTexture(GpuCommandBufferStub* owner_stub,
                             int32 route_id,
                             uint32 texture_id)
    : surface_texture_(gfx::SurfaceTexture::Create(texture_id)),
      size_(0, 0),
      has_valid_frame_(false),
      has_pending_frame_(false),
      owner_stub_(owner_stub),
      route_id_(route_id),
      has_listener_(false),
      texture_id_(texture_id),
      weak_factory_(this) {
  owner_stub->AddDestructionObserver(this);
  memset(current_matrix_, 0, sizeof(current_matrix_));
  owner_stub->channel()->AddRoute(route_id, this);
  surface_texture_->SetFrameAvailableCallback(base::Bind(
      &StreamTexture::OnFrameAvailable, weak_factory_.GetWeakPtr()));
}

StreamTexture::~StreamTexture() {
  if (owner_stub_) {
    owner_stub_->RemoveDestructionObserver(this);
    owner_stub_->channel()->RemoveRoute(route_id_);
  }
}

void StreamTexture::OnWillDestroyStub() {
  owner_stub_->RemoveDestructionObserver(this);
  owner_stub_->channel()->RemoveRoute(route_id_);
  owner_stub_ = NULL;

  // If the owner goes away, there is no need to keep the SurfaceTexture around.
  // The GL texture will keep working regardless with the currently bound frame.
  surface_texture_ = NULL;
}

void StreamTexture::Destroy(bool have_context) {
  NOTREACHED();
}

bool StreamTexture::CopyTexImage(unsigned target) {
  if (target != GL_TEXTURE_EXTERNAL_OES)
    return false;

  if (!owner_stub_ || !surface_texture_.get())
    return true;

  GLint texture_id;
  glGetIntegerv(GL_TEXTURE_BINDING_EXTERNAL_OES, &texture_id);
  DCHECK(texture_id);

  // The following code only works if we're being asked to copy into
  // |texture_id_|. Copying into a different texture is not supported.
  if (static_cast<unsigned>(texture_id) != texture_id_)
    return false;

  if (has_pending_frame_) {
    scoped_ptr<ui::ScopedMakeCurrent> scoped_make_current;
    bool needs_make_current =
        !owner_stub_->decoder()->GetGLContext()->IsCurrent(NULL);
    // On Android we should not have to perform a real context switch here when
    // using virtual contexts.
    DCHECK(!needs_make_current || !owner_stub_->decoder()
                                       ->GetContextGroup()
                                       ->feature_info()
                                       ->workarounds()
                                       .use_virtualized_gl_contexts);
    if (needs_make_current) {
      scoped_make_current.reset(new ui::ScopedMakeCurrent(
          owner_stub_->decoder()->GetGLContext(), owner_stub_->surface()));
    }
    surface_texture_->UpdateTexImage();
    has_valid_frame_ = true;
    has_pending_frame_ = false;
    if (scoped_make_current.get()) {
      // UpdateTexImage() implies glBindTexture().
      // The cmd decoder takes care of restoring the binding for this GLImage as
      // far as the current context is concerned, but if we temporarily change
      // it, we have to keep the state intact in *that* context also.
      const gpu::gles2::ContextState* state =
          owner_stub_->decoder()->GetContextState();
      const gpu::gles2::TextureUnit& active_unit =
          state->texture_units[state->active_texture_unit];
      glBindTexture(GL_TEXTURE_EXTERNAL_OES,
                    active_unit.bound_texture_external_oes.get()
                        ? active_unit.bound_texture_external_oes->service_id()
                        : 0);
    }
  }

  TextureManager* texture_manager =
      owner_stub_->decoder()->GetContextGroup()->texture_manager();
  gpu::gles2::Texture* texture =
      texture_manager->GetTextureForServiceId(texture_id_);
  if (texture) {
    // By setting image state to UNBOUND instead of COPIED we ensure that
    // CopyTexImage() is called each time the surface texture is used for
    // drawing.
    texture->SetLevelImage(GL_TEXTURE_EXTERNAL_OES, 0, this,
                           gpu::gles2::Texture::UNBOUND);
  }

  if (has_listener_ && has_valid_frame_) {
    float mtx[16];
    surface_texture_->GetTransformMatrix(mtx);

    // Only query the matrix once we have bound a valid frame.
    if (memcmp(current_matrix_, mtx, sizeof(mtx)) != 0) {
      memcpy(current_matrix_, mtx, sizeof(mtx));

      GpuStreamTextureMsg_MatrixChanged_Params params;
      memcpy(&params.m00, mtx, sizeof(mtx));
      owner_stub_->channel()->Send(
          new GpuStreamTextureMsg_MatrixChanged(route_id_, params));
    }
  }

  return true;
}

void StreamTexture::OnFrameAvailable() {
  has_pending_frame_ = true;
  if (has_listener_ && owner_stub_) {
    owner_stub_->channel()->Send(
        new GpuStreamTextureMsg_FrameAvailable(route_id_));
  }
}

gfx::Size StreamTexture::GetSize() {
  return size_;
}

unsigned StreamTexture::GetInternalFormat() {
  return GL_RGBA;
}

bool StreamTexture::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(StreamTexture, message)
    IPC_MESSAGE_HANDLER(GpuStreamTextureMsg_StartListening, OnStartListening)
    IPC_MESSAGE_HANDLER(GpuStreamTextureMsg_EstablishPeer, OnEstablishPeer)
    IPC_MESSAGE_HANDLER(GpuStreamTextureMsg_SetSize, OnSetSize)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()

  DCHECK(handled);
  return handled;
}

void StreamTexture::OnStartListening() {
  DCHECK(!has_listener_);
  has_listener_ = true;
}

void StreamTexture::OnEstablishPeer(int32 primary_id, int32 secondary_id) {
  if (!owner_stub_)
    return;

  base::ProcessHandle process = owner_stub_->channel()->GetClientPID();

  SurfaceTexturePeer::GetInstance()->EstablishSurfaceTexturePeer(
      process, surface_texture_, primary_id, secondary_id);
}

bool StreamTexture::BindTexImage(unsigned target) {
  NOTREACHED();
  return false;
}

void StreamTexture::ReleaseTexImage(unsigned target) {
  NOTREACHED();
}

bool StreamTexture::CopyTexSubImage(unsigned target,
                                    const gfx::Point& offset,
                                    const gfx::Rect& rect) {
  return false;
}

bool StreamTexture::ScheduleOverlayPlane(gfx::AcceleratedWidget widget,
                                         int z_order,
                                         gfx::OverlayTransform transform,
                                         const gfx::Rect& bounds_rect,
                                         const gfx::RectF& crop_rect) {
  NOTREACHED();
  return false;
}

void StreamTexture::OnMemoryDump(base::trace_event::ProcessMemoryDump* pmd,
                                 uint64_t process_tracing_id,
                                 const std::string& dump_name) {
  // TODO(ericrk): Add OnMemoryDump for GLImages. crbug.com/514914
}

}  // namespace content
