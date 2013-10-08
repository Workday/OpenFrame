// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/command_buffer/service/image_manager.h"

#include "ui/gl/gl_image.h"

namespace gpu {
namespace gles2 {

ImageManager::ImageManager() {
}

ImageManager::~ImageManager() {
}

void ImageManager::AddImage(gfx::GLImage* image, int32 service_id) {
  gl_images_[service_id] = image;
}

void ImageManager::RemoveImage(int32 service_id) {
  gl_images_.erase(service_id);
}

gfx::GLImage* ImageManager::LookupImage(int32 service_id) {
  GLImageMap::const_iterator iter = gl_images_.find(service_id);
  if (iter != gl_images_.end())
    return iter->second.get();

  return NULL;
}

}  // namespace gles2
}  // namespace gpu
