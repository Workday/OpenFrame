// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_CHROMEOS_LOGIN_KIOSK_APP_MENU_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_CHROMEOS_LOGIN_KIOSK_APP_MENU_HANDLER_H_

#include <string>

#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/chromeos/app_mode/kiosk_app_manager.h"
#include "chrome/browser/chromeos/app_mode/kiosk_app_manager_observer.h"
#include "content/public/browser/web_ui_message_handler.h"

namespace chromeos {

// KioskAppMenuHandler supplies kiosk apps data to apps menu on sign-in
// screen when app mode is enabled and handles "launchKioskApp" request
// from the apps menu.
class KioskAppMenuHandler : public content::WebUIMessageHandler,
                            public KioskAppManagerObserver {
 public:
  KioskAppMenuHandler();
  virtual ~KioskAppMenuHandler();

  void GetLocalizedStrings(base::DictionaryValue* localized_strings);

  // content::WebUIMessageHandler overrides:
  virtual void RegisterMessages() OVERRIDE;

 private:
  // Sends all kiosk apps to webui.
  void SendKioskApps();

  // JS callbacks.
  void HandleInitializeKioskApps(const base::ListValue* args);
  void HandleKioskAppsLoaded(const base::ListValue* args);
  void HandleLaunchKioskApps(const base::ListValue* args);
  void HandleCheckKioskAppLaunchError(const base::ListValue* args);

  // KioskAppManagerObserver overrides:
  virtual void OnKioskAppsSettingsChanged() OVERRIDE;
  virtual void OnKioskAppDataChanged(const std::string& app_id) OVERRIDE;

  // Callback for KioskAppManager::GetConsumerKioskModeStatus().
  void OnGetConsumerKioskModeStatus(
      KioskAppManager::ConsumerKioskModeStatus status);

  bool initialized_;
  base::WeakPtrFactory<KioskAppMenuHandler> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(KioskAppMenuHandler);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_UI_WEBUI_CHROMEOS_LOGIN_KIOSK_APP_MENU_HANDLER_H_
