// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/chrome_browser_main_extra_parts_views.h"

#include "chrome/browser/chrome_browser_main.h"
#include "chrome/browser/ui/views/chrome_views_delegate.h"
#include "chrome/common/chrome_switches.h"
#include "ui/base/ui_base_switches.h"

ChromeBrowserMainExtraPartsViews::ChromeBrowserMainExtraPartsViews() {
}

void ChromeBrowserMainExtraPartsViews::ToolkitInitialized() {
  // The delegate needs to be set before any UI is created so that windows
  // display the correct icon.
  if (!views::ViewsDelegate::views_delegate)
    views::ViewsDelegate::views_delegate = new ChromeViewsDelegate;
}
