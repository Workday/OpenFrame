// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_APP_LIST_APP_LIST_CONTROLLER_DELEGATE_H_
#define CHROME_BROWSER_UI_APP_LIST_APP_LIST_CONTROLLER_DELEGATE_H_

#include <string>

#include "ui/gfx/native_widget_types.h"

class Profile;

namespace extensions {
class Extension;
}

namespace gfx {
class ImageSkia;
}

// Interface to allow the view delegate to call out to whatever is controlling
// the app list. This will have different implementations for different
// platforms.
class AppListControllerDelegate {
 public:
  virtual ~AppListControllerDelegate();

  // Dismisses the view.
  virtual void DismissView() = 0;

  // Handle the view being closed.
  virtual void ViewClosing();

  // Handle the view being activated or deactivated.
  virtual void ViewActivationChanged(bool active);

  // Get app list window.
  virtual gfx::NativeWindow GetAppListWindow() = 0;

  // Get the application icon to be used, if any, for the app list.
  virtual gfx::ImageSkia GetWindowIcon();

  // Control of pinning apps.
  virtual bool IsAppPinned(const std::string& extension_id);
  virtual void PinApp(const std::string& extension_id);
  virtual void UnpinApp(const std::string& extension_id);
  virtual bool CanPin() = 0;

  // Be aware of the extension prompt (either uninstalling flow or enable flow).
  virtual void OnShowExtensionPrompt() {}
  virtual void OnCloseExtensionPrompt() {}

  // Whether the controller supports a Create Shortcuts flow.
  virtual bool CanDoCreateShortcutsFlow(bool is_platform_app) = 0;
  virtual void DoCreateShortcutsFlow(Profile* profile,
                                     const std::string& extension_id);

  // Handle the "create window" context menu items of Chrome App.
  // |incognito| is true to create an incognito window.
  virtual void CreateNewWindow(Profile* profile, bool incognito);

  // Show the app's most recent window, or launch it if it is not running.
  virtual void ActivateApp(Profile* profile,
                           const extensions::Extension* extension,
                           int event_flags) = 0;

  // Launch the app.
  virtual void LaunchApp(Profile* profile,
                         const extensions::Extension* extension,
                         int event_flags) = 0;

  // Whether or not the icon indicating which user is logged in should be
  // visible.
  virtual bool ShouldShowUserIcon();
};

#endif  // CHROME_BROWSER_UI_APP_LIST_APP_LIST_CONTROLLER_DELEGATE_H_
