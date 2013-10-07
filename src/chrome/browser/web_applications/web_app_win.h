// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_WEB_APPLICATIONS_WEB_APP_WIN_H_
#define CHROME_BROWSER_WEB_APPLICATIONS_WEB_APP_WIN_H_

#include "base/files/file_path.h"
#include "chrome/browser/shell_integration.h"

namespace gfx {
class ImageFamily;
}

namespace web_app {

// Create a shortcut in the given web app data dir, returning the name of the
// created shortcut.
base::FilePath CreateShortcutInWebAppDir(
    const base::FilePath& web_app_path,
    const ShellIntegration::ShortcutInfo& shortcut_info);

namespace internals {

bool CheckAndSaveIcon(const base::FilePath& icon_file,
                      const gfx::ImageFamily& image);

}  // namespace internals

}  // namespace web_app

#endif  // CHROME_BROWSER_WEB_APPLICATIONS_WEB_APP_WIN_H_
