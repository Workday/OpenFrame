// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_APP_ICON_WIN_H_
#define CHROME_BROWSER_APP_ICON_WIN_H_

#include <windows.h>

#include "base/memory/scoped_ptr.h"

namespace gfx {
class ImageFamily;
class Size;
}

class SkBitmap;

HICON GetAppIcon();
HICON GetSmallAppIcon();

gfx::Size GetAppIconSize();
gfx::Size GetSmallAppIconSize();

// Retrieve the application icon for the current process. This returns all of
// the different sizes of the icon as an ImageFamily.
scoped_ptr<gfx::ImageFamily> GetAppIconImageFamily();

#endif  // CHROME_BROWSER_APP_ICON_WIN_H_
