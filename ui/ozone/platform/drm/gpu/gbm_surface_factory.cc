// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/drm/gpu/gbm_surface_factory.h"

#include <gbm.h>

#include "base/files/file_path.h"
#include "third_party/khronos/EGL/egl.h"
#include "ui/ozone/common/egl_util.h"
#include "ui/ozone/platform/drm/gpu/drm_thread_proxy.h"
#include "ui/ozone/platform/drm/gpu/drm_window_proxy.h"
#include "ui/ozone/platform/drm/gpu/gbm_buffer.h"
#include "ui/ozone/platform/drm/gpu/gbm_surfaceless.h"
#include "ui/ozone/platform/drm/gpu/proxy_helpers.h"
#include "ui/ozone/platform/drm/gpu/screen_manager.h"
#include "ui/ozone/public/native_pixmap.h"
#include "ui/ozone/public/surface_ozone_canvas.h"
#include "ui/ozone/public/surface_ozone_egl.h"

namespace ui {

GbmSurfaceFactory::GbmSurfaceFactory(DrmThreadProxy* drm_thread)
    : drm_thread_(drm_thread) {}

GbmSurfaceFactory::~GbmSurfaceFactory() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

void GbmSurfaceFactory::RegisterSurface(gfx::AcceleratedWidget widget,
                                        GbmSurfaceless* surface) {
  DCHECK(thread_checker_.CalledOnValidThread());
  widget_to_surface_map_.insert(std::make_pair(widget, surface));
}

void GbmSurfaceFactory::UnregisterSurface(gfx::AcceleratedWidget widget) {
  DCHECK(thread_checker_.CalledOnValidThread());
  widget_to_surface_map_.erase(widget);
}

GbmSurfaceless* GbmSurfaceFactory::GetSurface(
    gfx::AcceleratedWidget widget) const {
  DCHECK(thread_checker_.CalledOnValidThread());
  auto it = widget_to_surface_map_.find(widget);
  DCHECK(it != widget_to_surface_map_.end());
  return it->second;
}

intptr_t GbmSurfaceFactory::GetNativeDisplay() {
  DCHECK(thread_checker_.CalledOnValidThread());
  return EGL_DEFAULT_DISPLAY;
}

const int32* GbmSurfaceFactory::GetEGLSurfaceProperties(
    const int32* desired_list) {
  DCHECK(thread_checker_.CalledOnValidThread());
  static const int32 kConfigAttribs[] = {EGL_BUFFER_SIZE,
                                         32,
                                         EGL_ALPHA_SIZE,
                                         8,
                                         EGL_BLUE_SIZE,
                                         8,
                                         EGL_GREEN_SIZE,
                                         8,
                                         EGL_RED_SIZE,
                                         8,
                                         EGL_RENDERABLE_TYPE,
                                         EGL_OPENGL_ES2_BIT,
                                         EGL_SURFACE_TYPE,
                                         EGL_WINDOW_BIT,
                                         EGL_NONE};

  return kConfigAttribs;
}

bool GbmSurfaceFactory::LoadEGLGLES2Bindings(
    AddGLLibraryCallback add_gl_library,
    SetGLGetProcAddressProcCallback set_gl_get_proc_address) {
  DCHECK(thread_checker_.CalledOnValidThread());
  return LoadDefaultEGLGLES2Bindings(add_gl_library, set_gl_get_proc_address);
}

scoped_ptr<SurfaceOzoneCanvas> GbmSurfaceFactory::CreateCanvasForWidget(
    gfx::AcceleratedWidget widget) {
  DCHECK(thread_checker_.CalledOnValidThread());
  LOG(FATAL) << "Software rendering mode is not supported with GBM platform";
  return nullptr;
}

scoped_ptr<SurfaceOzoneEGL> GbmSurfaceFactory::CreateEGLSurfaceForWidget(
    gfx::AcceleratedWidget widget) {
  NOTREACHED();
  return nullptr;
}

scoped_ptr<SurfaceOzoneEGL>
GbmSurfaceFactory::CreateSurfacelessEGLSurfaceForWidget(
    gfx::AcceleratedWidget widget) {
  DCHECK(thread_checker_.CalledOnValidThread());
  return make_scoped_ptr(
      new GbmSurfaceless(drm_thread_->CreateDrmWindowProxy(widget), this));
}

scoped_refptr<ui::NativePixmap> GbmSurfaceFactory::CreateNativePixmap(
    gfx::AcceleratedWidget widget,
    gfx::Size size,
    gfx::BufferFormat format,
    gfx::BufferUsage usage) {
#if !defined(OS_CHROMEOS)
  // Support for memory mapping accelerated buffers requires some
  // CrOS-specific patches (using vgem).
  DCHECK(gfx::BufferUsage::SCANOUT == usage);
#endif

  scoped_refptr<GbmBuffer> buffer =
      drm_thread_->CreateBuffer(widget, size, format, usage);
  if (!buffer.get())
    return nullptr;

  scoped_refptr<GbmPixmap> pixmap(new GbmPixmap(this));
  if (!pixmap->InitializeFromBuffer(buffer))
    return nullptr;

  return pixmap;
}

scoped_refptr<ui::NativePixmap> GbmSurfaceFactory::CreateNativePixmapFromHandle(
    const gfx::NativePixmapHandle& handle) {
  scoped_refptr<GbmPixmap> pixmap(new GbmPixmap(this));
  pixmap->Initialize(base::ScopedFD(handle.fd.fd), handle.stride);
  return pixmap;
}

}  // namespace ui
