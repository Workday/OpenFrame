// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/themes/theme_service.h"

#include <gdk-pixbuf/gdk-pixbuf.h>

#include "base/i18n/rtl.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/gtk_util.h"
#include "ui/gfx/image/image.h"

GdkPixbuf* ThemeService::GetRTLEnabledPixbufNamed(int id) const {
  return GetPixbufImpl(id, true);
}

GdkPixbuf* ThemeService::GetPixbufImpl(int id, bool rtl_enabled) const {
  DCHECK(CalledOnValidThread());
  // Use the negative |resource_id| for the key for BIDI-aware images.
  int key = rtl_enabled ? -id : id;

  // Check to see if we already have the pixbuf in the cache.
  GdkPixbufMap::const_iterator pixbufs_iter = gdk_pixbufs_.find(key);
  if (pixbufs_iter != gdk_pixbufs_.end())
    return pixbufs_iter->second;

  SkBitmap bitmap = GetImageNamed(id).AsBitmap();
  GdkPixbuf* pixbuf = gfx::GdkPixbufFromSkBitmap(bitmap);

  // We loaded successfully.  Cache the pixbuf.
  if (pixbuf) {
    if (base::i18n::IsRTL() && rtl_enabled) {
      GdkPixbuf* original_pixbuf = pixbuf;
      pixbuf = gdk_pixbuf_flip(pixbuf, TRUE);
      g_object_unref(original_pixbuf);
    }

    gdk_pixbufs_[key] = pixbuf;
    return pixbuf;
  }

  // We failed to retrieve the bitmap, show a debugging red square.
  LOG(WARNING) << "Unable to load GdkPixbuf with id " << id;
  NOTREACHED();  // Want to assert in debug mode.

  static GdkPixbuf* empty_bitmap = NULL;
  if (!empty_bitmap) {
    // The placeholder bitmap is bright red so people notice the problem.
    SkBitmap skia_bitmap;
    skia_bitmap.setConfig(SkBitmap::kARGB_8888_Config, 32, 32);
    skia_bitmap.allocPixels();
    skia_bitmap.eraseARGB(255, 255, 0, 0);
    empty_bitmap = gfx::GdkPixbufFromSkBitmap(skia_bitmap);
  }
  return empty_bitmap;
}

void ThemeService::FreePlatformCaches() {
  DCHECK(CalledOnValidThread());

  // Free GdkPixbufs.
  for (GdkPixbufMap::iterator i = gdk_pixbufs_.begin();
       i != gdk_pixbufs_.end(); i++) {
    g_object_unref(i->second);
  }
  gdk_pixbufs_.clear();
}
