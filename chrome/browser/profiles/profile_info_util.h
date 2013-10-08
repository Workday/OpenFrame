// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PROFILES_PROFILE_INFO_UTIL_H_
#define CHROME_BROWSER_PROFILES_PROFILE_INFO_UTIL_H_

#include "ui/gfx/image/image.h"

namespace profiles {

extern const int kAvatarIconWidth;
extern const int kAvatarIconHeight;
extern const int kAvatarIconBorder;

// Returns a version of |image| of a specific size and with a grey border.
// Note that no checks are done on the width/height so make sure they're
// reasonable values; in the range of 16-256 is probably best.
gfx::Image GetSizedAvatarIconWithBorder(const gfx::Image& image,
                                        bool is_gaia_picture,
                                        int width, int height);

// Returns a version of |image| suitable for use in menus.
gfx::Image GetAvatarIconForMenu(const gfx::Image& image,
                                bool is_gaia_picture);

// Returns a version of |image| suitable for use in WebUI.
gfx::Image GetAvatarIconForWebUI(const gfx::Image& image,
                                 bool is_gaia_picture);

// Returns a version of |image| suitable for use in title bars. The returned
// image is scaled to fit |dst_width| and |dst_height|.
gfx::Image GetAvatarIconForTitleBar(const gfx::Image& image,
                                    bool is_gaia_picture,
                                    int dst_width,
                                    int dst_height);

}

#endif  // CHROME_BROWSER_PROFILES_PROFILE_INFO_UTIL_H_
