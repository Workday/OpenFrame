// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_BITMAP_UPLOADER_BITMAP_UPLOADER_H_
#define COMPONENTS_BITMAP_UPLOADER_BITMAP_UPLOADER_H_

#include "base/compiler_specific.h"
#include "base/containers/hash_tables.h"
#include "base/macros.h"
#include "components/bitmap_uploader/bitmap_uploader_export.h"
#include "components/mus/public/cpp/window_surface.h"
#include "components/mus/public/interfaces/compositor_frame.mojom.h"
#include "components/mus/public/interfaces/gpu.mojom.h"
#include "gpu/GLES2/gl2chromium.h"
#include "gpu/GLES2/gl2extchromium.h"
#include "mojo/public/c/gles2/gles2.h"

namespace mojo {
class Shell;
}

namespace bitmap_uploader {

BITMAP_UPLOADER_EXPORT extern const char kBitmapUploaderForAcceleratedWidget[];

// BitmapUploader is useful if you want to draw a bitmap or color in a
// mus::Window.
class BITMAP_UPLOADER_EXPORT BitmapUploader
    : NON_EXPORTED_BASE(public mus::mojom::SurfaceClient) {
 public:
  explicit BitmapUploader(mus::Window* window);
  ~BitmapUploader() override;

  void Init(mojo::Shell* shell);

  // Sets the color which is RGBA.
  void SetColor(uint32_t color);

  enum Format {
    RGBA,  // Pixel layout on Android.
    BGRA,  // Pixel layout everywhere else.
  };

  // Sets a bitmap.
  void SetBitmap(int width,
                 int height,
                 scoped_ptr<std::vector<unsigned char>> data,
                 Format format);

 private:
  void Upload();

  uint32_t BindTextureForSize(const mojo::Size size);

  uint32_t TextureFormat() const {
    return format_ == BGRA ? GL_BGRA_EXT : GL_RGBA;
  }

  void SetIdNamespace(uint32_t id_namespace);

  // SurfaceClient implementation.
  void ReturnResources(
      mojo::Array<mus::mojom::ReturnedResourcePtr> resources) override;

  mus::Window* window_;
  mus::mojom::GpuPtr gpu_service_;
  scoped_ptr<mus::WindowSurface> surface_;
  MojoGLES2Context gles2_context_;

  mojo::Size size_;
  uint32_t color_;
  int width_;
  int height_;
  Format format_;
  scoped_ptr<std::vector<unsigned char>> bitmap_;
  uint32_t next_resource_id_;
  uint32_t id_namespace_;
  uint32_t local_id_;
  base::hash_map<uint32_t, uint32_t> resource_to_texture_id_map_;
  mojo::Binding<mus::mojom::SurfaceClient> surface_client_binding_;

  DISALLOW_COPY_AND_ASSIGN(BitmapUploader);
};

}  // namespace bitmap_uploader

#endif  // COMPONENTS_BITMAP_UPLOADER_BITMAP_UPLAODER_H_
