// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_EXTENSIONS_CHROMEOS_KIOSK_APPS_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_EXTENSIONS_CHROMEOS_KIOSK_APPS_HANDLER_H_

#include <string>

#include "base/compiler_specific.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/chromeos/app_mode/kiosk_app_manager.h"
#include "chrome/browser/chromeos/app_mode/kiosk_app_manager_observer.h"
#include "content/public/browser/web_ui_message_handler.h"

namespace base {
class DictionaryValue;
class ListValue;
}

namespace content {
class WebUIDataSource;
}

namespace chromeos {

class KioskAppManager;

class KioskAppsHandler : public content::WebUIMessageHandler,
                         public KioskAppManagerObserver {
 public:
  KioskAppsHandler();
  virtual ~KioskAppsHandler();

  void GetLocalizedValues(content::WebUIDataSource* source);

  // content::WebUIMessageHandler overrides:
  virtual void RegisterMessages() OVERRIDE;

  // KioskAppPrefsObserver overrides:
  virtual void OnKioskAppDataChanged(const std::string& app_id) OVERRIDE;
  virtual void OnKioskAppDataLoadFailure(const std::string& app_id) OVERRIDE;
  virtual void OnKioskAppsSettingsChanged() OVERRIDE;

 private:
  // Sends all kiosk apps and settings to webui.
  void SendKioskAppSettings();

  // JS callbacks.
  void HandleInitializeKioskAppSettings(const base::ListValue* args);
  void HandleGetKioskAppSettings(const base::ListValue* args);
  void HandleAddKioskApp(const base::ListValue* args);
  void HandleRemoveKioskApp(const base::ListValue* args);
  void HandleEnableKioskAutoLaunch(const base::ListValue* args);
  void HandleDisableKioskAutoLaunch(const base::ListValue* args);
  void HandleSetDisableBailoutShortcut(const base::ListValue* args);

  // Callback for KioskAppManager::GetConsumerKioskModeStatus().
  void OnGetConsumerKioskModeStatus(
      chromeos::KioskAppManager::ConsumerKioskModeStatus status);

  KioskAppManager* kiosk_app_manager_;  // not owned.
  bool initialized_;
  bool is_kiosk_enabled_;
  base::WeakPtrFactory<KioskAppsHandler> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(KioskAppsHandler);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_UI_WEBUI_EXTENSIONS_CHROMEOS_KIOSK_APPS_HANDLER_H_
