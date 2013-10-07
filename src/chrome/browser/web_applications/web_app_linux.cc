// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/web_applications/web_app.h"

#include "base/environment.h"
#include "base/logging.h"
#include "chrome/browser/shell_integration_linux.h"
#include "content/public/browser/browser_thread.h"

namespace web_app {

namespace internals {

bool CreatePlatformShortcuts(
    const base::FilePath& web_app_path,
    const ShellIntegration::ShortcutInfo& shortcut_info,
    const ShellIntegration::ShortcutLocations& creation_locations,
    ShortcutCreationReason /*creation_reason*/) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::FILE));
  return ShellIntegrationLinux::CreateDesktopShortcut(
      shortcut_info, creation_locations);
}

void DeletePlatformShortcuts(
    const base::FilePath& web_app_path,
    const ShellIntegration::ShortcutInfo& shortcut_info) {
  ShellIntegrationLinux::DeleteDesktopShortcuts(shortcut_info.profile_path,
      shortcut_info.extension_id);
}

void UpdatePlatformShortcuts(
    const base::FilePath& web_app_path,
    const string16& /*old_app_title*/,
    const ShellIntegration::ShortcutInfo& shortcut_info) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::FILE));

  scoped_ptr<base::Environment> env(base::Environment::Create());

  // Find out whether shortcuts are already installed.
  ShellIntegration::ShortcutLocations creation_locations =
      ShellIntegrationLinux::GetExistingShortcutLocations(
          env.get(), shortcut_info.profile_path, shortcut_info.extension_id);
  // Always create a hidden shortcut in applications if a visible one is not
  // being created. This allows the operating system to identify the app, but
  // not show it in the menu.
  creation_locations.hidden = true;

  // Always create the shortcut in the Chrome Apps subdir (even if it is
  // currently in a different location).
  creation_locations.applications_menu_subdir = GetAppShortcutsSubdirName();

  CreatePlatformShortcuts(web_app_path, shortcut_info, creation_locations,
                          SHORTCUT_CREATION_BY_USER);
}

void DeleteAllShortcutsForProfile(const base::FilePath& profile_path) {
  ShellIntegrationLinux::DeleteAllDesktopShortcuts(profile_path);
}

}  // namespace internals

}  // namespace web_app
