// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/compositor/software_output_device_mus.h"

#include "components/bitmap_uploader/bitmap_uploader.h"
#include "third_party/skia/include/core/SkImageInfo.h"
#include "ui/base/view_prop.h"
#include "ui/compositor/compositor.h"
#include "ui/gfx/skia_util.h"

#if !defined(OFFICIAL_BUILD)
#include "base/threading/thread_restrictions.h"
#endif

namespace content {

SoftwareOutputDeviceMus::SoftwareOutputDeviceMus(ui::Compositor* compositor)
    : compositor_(compositor) {}

void SoftwareOutputDeviceMus::EndPaint() {
  SoftwareOutputDevice::EndPaint();
#if !defined(OFFICIAL_BUILD)
  base::ThreadRestrictions::ScopedAllowWait wait;
#endif

  if (!surface_)
    return;

  gfx::Rect rect = damage_rect_;
  rect.Intersect(gfx::Rect(viewport_pixel_size_));
  if (rect.IsEmpty())
    return;

  gfx::AcceleratedWidget widget = compositor_->widget();
  bitmap_uploader::BitmapUploader* uploader =
      reinterpret_cast<bitmap_uploader::BitmapUploader*>(ui::ViewProp::GetValue(
          widget, bitmap_uploader::kBitmapUploaderForAcceleratedWidget));
  DCHECK(uploader);

  SkImageInfo info;
  size_t rowBytes;
  const void* addr = surface_->peekPixels(&info, &rowBytes);

  if (!addr) {
    LOG(WARNING) << "SoftwareOutputDeviceMus: skia surface did not provide us "
                    "with pixels";
    return;
  }

  const unsigned char* pixels = static_cast<const unsigned char*>(addr);

  // TODO(rjkroege): This makes an additional copy. Improve the
  // bitmap_uploader API to remove.
  scoped_ptr<std::vector<unsigned char>> data(new std::vector<unsigned char>(
      pixels, pixels + rowBytes * viewport_pixel_size_.height()));
  uploader->SetBitmap(viewport_pixel_size_.width(),
                      viewport_pixel_size_.height(), data.Pass(),
                      bitmap_uploader::BitmapUploader::BGRA);
}

}  // namespace content
