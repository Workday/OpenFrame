// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_APP_LIST_APP_LIST_SERVICE_WIN_H_
#define CHROME_BROWSER_UI_APP_LIST_APP_LIST_SERVICE_WIN_H_

class AppListService;

namespace chrome {

AppListService* GetAppListServiceWin();

// Returns the resource id of the app list PNG icon used for the taskbar,
// infobars and window decorations.
int GetAppListIconResourceId();

}  // namespace chrome

#endif  // CHROME_BROWSER_UI_APP_LIST_APP_LIST_SERVICE_WIN_H_
