// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/tabs/tabs_constants.h"

namespace extensions {
namespace tabs_constants {

const char kActiveKey[] = "active";
const char kAllFramesKey[] = "allFrames";
const char kAlwaysOnTopKey[] = "alwaysOnTop";
const char kBypassCache[] = "bypassCache";
const char kCodeKey[] = "code";
const char kCurrentWindowKey[] = "currentWindow";
const char kDrawAttentionKey[] = "drawAttention";
const char kFaviconUrlKey[] = "favIconUrl";
const char kFileKey[] = "file";
const char kFocusedKey[] = "focused";
const char kFormatKey[] = "format";
const char kFromIndexKey[] = "fromIndex";
const char kHeightKey[] = "height";
const char kIdKey[] = "id";
const char kIncognitoKey[] = "incognito";
const char kIndexKey[] = "index";
const char kLastFocusedWindowKey[] = "lastFocusedWindow";
const char kLeftKey[] = "left";
const char kNewPositionKey[] = "newPosition";
const char kNewWindowIdKey[] = "newWindowId";
const char kOldPositionKey[] = "oldPosition";
const char kOldWindowIdKey[] = "oldWindowId";
const char kOpenerTabIdKey[] = "openerTabId";
const char kPinnedKey[] = "pinned";
const char kQualityKey[] = "quality";
const char kHighlightedKey[] = "highlighted";
const char kRunAtKey[] = "runAt";
const char kSelectedKey[] = "selected";
const char kShowStateKey[] = "state";
const char kStatusKey[] = "status";
const char kTabIdKey[] = "tabId";
const char kTabIdsKey[] = "tabIds";
const char kTabsKey[] = "tabs";
const char kTabUrlKey[] = "tabUrl";
const char kTitleKey[] = "title";
const char kToIndexKey[] = "toIndex";
const char kTopKey[] = "top";
const char kUrlKey[] = "url";
const char kWindowClosing[] = "isWindowClosing";
const char kWidthKey[] = "width";
const char kWindowIdKey[] = "windowId";
const char kWindowTypeKey[] = "type";
const char kWindowTypeLongKey[] = "windowType";

const char kFormatValueJpeg[] = "jpeg";
const char kFormatValuePng[] = "png";
const char kMimeTypeJpeg[] = "image/jpeg";
const char kMimeTypePng[] = "image/png";
const char kShowStateValueNormal[] = "normal";
const char kShowStateValueMinimized[] = "minimized";
const char kShowStateValueMaximized[] = "maximized";
const char kShowStateValueFullscreen[] = "fullscreen";
const char kStatusValueComplete[] = "complete";
const char kStatusValueLoading[] = "loading";

// TODO(mpcomplete): should we expose more specific detail, like devtools, app
// panel, etc?
const char kWindowTypeValueNormal[] = "normal";
const char kWindowTypeValuePopup[] = "popup";
const char kWindowTypeValuePanel[] = "panel";
const char kWindowTypeValueDetachedPanel[] = "detached_panel";
const char kWindowTypeValueApp[] = "app";

const char kCanOnlyMoveTabsWithinNormalWindowsError[] = "Tabs can only be "
    "moved to and from normal windows.";
const char kCanOnlyMoveTabsWithinSameProfileError[] = "Tabs can only be moved "
    "between windows in the same profile.";
const char kNoCrashBrowserError[] =
    "I'm sorry. I'm afraid I can't do that.";
const char kNoCurrentWindowError[] = "No current window";
const char kNoLastFocusedWindowError[] = "No last-focused window";
const char kWindowNotFoundError[] = "No window with id: *.";
const char kTabIndexNotFoundError[] = "No tab at index: *.";
const char kTabNotFoundError[] = "No tab with id: *.";
const char kTabStripNotEditableError[] =
    "Tabs cannot be edited right now (user may be dragging a tab).";
const char kNoSelectedTabError[] = "No selected tab";
const char kNoHighlightedTabError[] = "No highlighted tab";
const char kIncognitoModeIsDisabled[] = "Incognito mode is disabled.";
const char kIncognitoModeIsForced[] = "Incognito mode is forced. "
    "Cannot open normal windows.";
const char kURLsNotAllowedInIncognitoError[] = "Cannot open URL \"*\" "
    "in an incognito window.";
const char kInvalidUrlError[] = "Invalid url: \"*\".";
const char kInternalVisibleTabCaptureError[] =
    "Internal error while trying to capture visible region of the current tab";
const char kNotImplementedError[] = "This call is not yet implemented";
const char kSupportedInWindowsOnlyError[] = "Supported in Windows only";
const char kInvalidWindowTypeError[] = "Invalid value for type";
const char kInvalidWindowStateError[] = "Invalid value for state";
const char kScreenshotsDisabled[] = "Taking screenshots has been disabled";

const char kNoCodeOrFileToExecuteError[] = "No source code or file specified.";
const char kMoreThanOneValuesError[] = "Code and file should not be specified "
    "at the same time in the second argument.";
const char kLoadFileError[] = "Failed to load file: \"*\". ";
const char kCannotDetermineLanguageOfUnloadedTab[] =
    "Cannot determine language: tab not loaded";

}  // namespace tabs_constants
}  // namespace extensions
