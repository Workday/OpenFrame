// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ASH_CHROME_SHELL_DELEGATE_H_
#define CHROME_BROWSER_UI_ASH_CHROME_SHELL_DELEGATE_H_

#include <string>

#include "ash/launcher/launcher_types.h"
#include "ash/shell_delegate.h"
#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"

class Browser;

namespace ash {
class WindowPositioner;
}

namespace content {
class WebContents;
}

namespace keyboard {
class KeyboardControllerProxy;
}

class ChromeLauncherController;

class ChromeShellDelegate : public ash::ShellDelegate,
                            public content::NotificationObserver {
 public:
  ChromeShellDelegate();
  virtual ~ChromeShellDelegate();

  static ChromeShellDelegate* instance() { return instance_; }

  ash::WindowPositioner* window_positioner() {
    return window_positioner_.get();
  }

  // ash::ShellDelegate overrides;
  virtual bool IsFirstRunAfterBoot() const OVERRIDE;
  virtual bool IsMultiProfilesEnabled() const OVERRIDE;
  virtual bool IsRunningInForcedAppMode() const OVERRIDE;
  virtual void PreInit() OVERRIDE;
  virtual void Shutdown() OVERRIDE;
  virtual void Exit() OVERRIDE;
  virtual void NewTab() OVERRIDE;
  virtual void NewWindow(bool is_incognito) OVERRIDE;
  virtual void ToggleFullscreen() OVERRIDE;
  virtual void ToggleMaximized() OVERRIDE;
  virtual void OpenFileManager(bool as_dialog) OVERRIDE;
  virtual void OpenCrosh() OVERRIDE;
  virtual void RestoreTab() OVERRIDE;
  virtual void ShowKeyboardOverlay() OVERRIDE;
  virtual keyboard::KeyboardControllerProxy*
      CreateKeyboardControllerProxy() OVERRIDE;
  virtual void ShowTaskManager() OVERRIDE;
  virtual content::BrowserContext* GetCurrentBrowserContext() OVERRIDE;
  virtual void ToggleHighContrast() OVERRIDE;
  virtual bool IsSpokenFeedbackEnabled() const OVERRIDE;
  virtual void ToggleSpokenFeedback(
      ash::AccessibilityNotificationVisibility notify) OVERRIDE;
  virtual bool IsHighContrastEnabled() const OVERRIDE;
  virtual void SetMagnifierEnabled(bool enabled) OVERRIDE;
  virtual void SetMagnifierType(ash::MagnifierType type) OVERRIDE;
  virtual bool IsMagnifierEnabled() const OVERRIDE;
  virtual ash::MagnifierType GetMagnifierType() const OVERRIDE;
  virtual void SetLargeCursorEnabled(bool enabled) OVERRIDE;
  virtual bool IsLargeCursorEnabled() const OVERRIDE;
  virtual bool ShouldAlwaysShowAccessibilityMenu() const OVERRIDE;
  virtual void SilenceSpokenFeedback() const OVERRIDE;
  virtual app_list::AppListViewDelegate* CreateAppListViewDelegate() OVERRIDE;
  virtual ash::LauncherDelegate* CreateLauncherDelegate(
      ash::LauncherModel* model) OVERRIDE;
  virtual ash::SystemTrayDelegate* CreateSystemTrayDelegate() OVERRIDE;
  virtual ash::UserWallpaperDelegate* CreateUserWallpaperDelegate() OVERRIDE;
  virtual ash::CapsLockDelegate* CreateCapsLockDelegate() OVERRIDE;
  virtual ash::SessionStateDelegate* CreateSessionStateDelegate() OVERRIDE;
  virtual aura::client::UserActionClient* CreateUserActionClient() OVERRIDE;
  virtual void OpenFeedbackPage() OVERRIDE;
  virtual void RecordUserMetricsAction(ash::UserMetricsAction action) OVERRIDE;
  virtual void HandleMediaNextTrack() OVERRIDE;
  virtual void HandleMediaPlayPause() OVERRIDE;
  virtual void HandleMediaPrevTrack() OVERRIDE;
  virtual void SaveScreenMagnifierScale(double scale) OVERRIDE;
  virtual double GetSavedScreenMagnifierScale() OVERRIDE;
  virtual ui::MenuModel* CreateContextMenu(aura::RootWindow* root) OVERRIDE;
  virtual ash::RootWindowHostFactory* CreateRootWindowHostFactory() OVERRIDE;
  virtual string16 GetProductName() const OVERRIDE;

  // content::NotificationObserver override:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

 private:
  class TabRestoreHelper;

  void PlatformInit();

  // Returns the browser for active ash window if any. Otherwise it searches
  // for a browser or create one for default profile and returns it.
  Browser* GetTargetBrowser();

  static ChromeShellDelegate* instance_;

  content::NotificationRegistrar registrar_;

  scoped_ptr<ash::WindowPositioner> window_positioner_;

  base::WeakPtrFactory<ChromeShellDelegate> weak_factory_;

  ChromeLauncherController* launcher_delegate_;

  scoped_ptr<TabRestoreHelper> tab_restore_helper_;

  DISALLOW_COPY_AND_ASSIGN(ChromeShellDelegate);
};

#endif  // CHROME_BROWSER_UI_ASH_CHROME_SHELL_DELEGATE_H_
