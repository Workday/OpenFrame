// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_DRM_GPU_GBM_SURFACE_FACTORY_H_
#define UI_OZONE_PLATFORM_DRM_GPU_GBM_SURFACE_FACTORY_H_

#include <map>

#include "base/threading/thread_checker.h"
#include "ui/ozone/public/surface_factory_ozone.h"

namespace ui {

class DrmThreadProxy;
class GbmDevice;
class GbmSurfaceless;

class GbmSurfaceFactory : public SurfaceFactoryOzone {
 public:
  GbmSurfaceFactory(DrmThreadProxy* drm_thread);
  ~GbmSurfaceFactory() override;

  void RegisterSurface(gfx::AcceleratedWidget widget, GbmSurfaceless* surface);
  void UnregisterSurface(gfx::AcceleratedWidget widget);
  GbmSurfaceless* GetSurface(gfx::AcceleratedWidget widget) const;

  // SurfaceFactoryOzone:
  intptr_t GetNativeDisplay() override;
  const int32_t* GetEGLSurfaceProperties(const int32_t* desired_list) override;
  bool LoadEGLGLES2Bindings(
      AddGLLibraryCallback add_gl_library,
      SetGLGetProcAddressProcCallback set_gl_get_proc_address) override;
  scoped_ptr<SurfaceOzoneCanvas> CreateCanvasForWidget(
      gfx::AcceleratedWidget widget) override;
  scoped_ptr<ui::SurfaceOzoneEGL> CreateEGLSurfaceForWidget(
      gfx::AcceleratedWidget w) override;
  scoped_ptr<SurfaceOzoneEGL> CreateSurfacelessEGLSurfaceForWidget(
      gfx::AcceleratedWidget widget) override;
  scoped_refptr<ui::NativePixmap> CreateNativePixmap(
      gfx::AcceleratedWidget widget,
      gfx::Size size,
      gfx::BufferFormat format,
      gfx::BufferUsage usage) override;
  scoped_refptr<NativePixmap> CreateNativePixmapFromHandle(
      const gfx::NativePixmapHandle& handle) override;

 private:
  base::ThreadChecker thread_checker_;

  DrmThreadProxy* drm_thread_;

  std::map<gfx::AcceleratedWidget, GbmSurfaceless*> widget_to_surface_map_;

  DISALLOW_COPY_AND_ASSIGN(GbmSurfaceFactory);
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_DRM_GPU_GBM_SURFACE_FACTORY_H_
