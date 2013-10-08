// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/screensaver_window_finder_x11.h"

#if defined(TOOLKIT_GTK)
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#endif

#include "base/basictypes.h"
#include "ui/base/x/x11_util.h"

ScreensaverWindowFinder::ScreensaverWindowFinder()
    : exists_(false) {
}

bool ScreensaverWindowFinder::ScreensaverWindowExists() {
#if defined(TOOLKIT_GTK)
  gdk_error_trap_push();
#endif
  ScreensaverWindowFinder finder;
  ui::EnumerateTopLevelWindows(&finder);
  bool got_error = false;
#if defined(TOOLKIT_GTK)
  got_error = gdk_error_trap_pop();
#endif
  return finder.exists_ && !got_error;
}

bool ScreensaverWindowFinder::ShouldStopIterating(XID window) {
  if (!ui::IsWindowVisible(window) || !IsScreensaverWindow(window))
    return false;
  exists_ = true;
  return true;
}

bool ScreensaverWindowFinder::IsScreensaverWindow(XID window) const {
  // It should occupy the full screen.
  if (!ui::IsX11WindowFullScreen(window))
    return false;

  // For xscreensaver, the window should have _SCREENSAVER_VERSION property.
  if (ui::PropertyExists(window, "_SCREENSAVER_VERSION"))
    return true;

  // For all others, like gnome-screensaver, the window's WM_CLASS property
  // should contain "screensaver".
  std::string value;
  if (!ui::GetStringProperty(window, "WM_CLASS", &value))
    return false;

  return value.find("screensaver") != std::string::npos;
}
