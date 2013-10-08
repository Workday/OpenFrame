// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/defaults.h"

namespace browser_defaults {

#if defined(USE_X11)
#if defined(TOOLKIT_VIEWS)
const bool kCanToggleSystemTitleBar = false;
#else
const bool kCanToggleSystemTitleBar = true;
#endif
#endif

const int kOmniboxFontPixelSize = 16;

#if defined(TOOLKIT_VIEWS)
// Windows and Chrome OS have bigger shadows in the tab art.
const int kMiniTabWidth = 64;
#else
const int kMiniTabWidth = 56;
#endif

const bool kRestorePopups = false;

#if defined(OS_CHROMEOS) || defined(OS_MACOSX)
const bool kBrowserAliveWithNoWindows = true;
const bool kShowExitMenuItem = false;
#else
const bool kBrowserAliveWithNoWindows = false;
const bool kShowExitMenuItem = true;
#endif

#if defined(OS_CHROMEOS)
const bool kShowFeedbackMenuItem = true;
const bool kShowHelpMenuItemIcon = true;
const bool kShowUpgradeMenuItem = false;
const bool kShowImportOnBookmarkBar = false;
const bool kAlwaysOpenIncognitoWindow = true;
const bool kAlwaysCreateTabbedBrowserOnSessionRestore = false;
#else
const bool kShowFeedbackMenuItem = false;
const bool kShowHelpMenuItemIcon = false;
const bool kShowUpgradeMenuItem = true;
const bool kShowImportOnBookmarkBar = true;
const bool kAlwaysOpenIncognitoWindow = false;
const bool kAlwaysCreateTabbedBrowserOnSessionRestore = true;
#endif

#if defined(OS_CHROMEOS)
const bool kOSSupportsOtherBrowsers = false;
#else
const bool kOSSupportsOtherBrowsers = true;
#endif

const bool kDownloadPageHasShowInFolder = true;
const bool kSizeTabButtonToTopOfTabStrip = false;

#if defined(OS_CHROMEOS) || defined(OS_ANDROID)
const bool kSyncAutoStarts = true;
const bool kShowOtherBrowsersInAboutMemory = false;
#else
const bool kSyncAutoStarts = false;
const bool kShowOtherBrowsersInAboutMemory = true;
#endif

#if defined(TOOLKIT_GTK)
const bool kShowCancelButtonInTaskManager = true;
#else
const bool kShowCancelButtonInTaskManager = false;
#endif

const int kBookmarkBarHeight = 28;

const ui::ResourceBundle::FontStyle kAssociatedNetworkFontStyle =
    ui::ResourceBundle::BoldFont;

const int kInfoBarBorderPaddingVertical = 5;

#if !defined(OS_ANDROID)
const bool kPasswordEchoEnabled = false;
#endif

bool bookmarks_enabled = true;

bool enable_help_app = true;

}  // namespace browser_defaults
