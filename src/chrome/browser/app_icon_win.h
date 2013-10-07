// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_APP_ICON_WIN_H_
#define CHROME_BROWSER_APP_ICON_WIN_H_

#include <windows.h>

#include "base/memory/scoped_ptr.h"

class SkBitmap;

HICON GetAppIcon();

// Retrieve the application icon for the given size. Note that if you specify a
// size other than what is contained in chrome.dll (16x16, 32x32, 48x48), this
// might return a poorly resized icon.
scoped_ptr<SkBitmap> GetAppIconForSize(int size);

#endif  // CHROME_BROWSER_APP_ICON_WIN_H_
