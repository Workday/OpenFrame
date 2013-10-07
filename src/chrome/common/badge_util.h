// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_COMMON_BADGE_UTIL_H_
#define CHROME_COMMON_BADGE_UTIL_H_

#include <string>

#include "base/strings/string16.h"
#include "chrome/common/extensions/api/extension_action/action_info.h"
#include "third_party/skia/include/core/SkBitmap.h"

class SkPaint;

namespace gfx {
class Canvas;
class Rect;
}

// badge_util provides a set of helper routines for rendering dynamically
// generated text overlays ("badges") on toolbar icons.
namespace badge_util {

// Helper routine that returns a singleton SkPaint object configured for
// rendering badge overlay text (correct font, typeface, etc).
SkPaint* GetBadgeTextPaintSingleton();

// Given an |icon|, renders the |text| centered on the |icon|. If |text| is
// too large to fit within the bounds of the image, the |fallback| string is
// rendered instead (or nothing, if |fallback| is empty).
SkBitmap DrawBadgeIconOverlay(const SkBitmap& icon,
                              float font_size_in_pixels,
                              const string16& text,
                              const string16& fallback);

// Paints badge with specified parameters to |canvas|.
void PaintBadge(gfx::Canvas* canvas,
                const gfx::Rect& bounds,
                const std::string& text,
                const SkColor& text_color_in,
                const SkColor& background_color_in,
                int icon_width,
                extensions::ActionInfo::Type action_type);

}  // namespace badge_util;

#endif  // CHROME_COMMON_BADGE_UTIL_H_
