// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/chromeos/login/kiosk_app_menu_handler.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/chromeos/app_mode/kiosk_app_launch_error.h"
#include "chrome/browser/chromeos/app_mode/kiosk_app_launcher.h"
#include "chrome/browser/chromeos/login/existing_user_controller.h"
#include "chrome/browser/policy/browser_policy_connector.h"
#include "chromeos/chromeos_switches.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/web_ui.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/webui/web_ui_util.h"

namespace chromeos {

KioskAppMenuHandler::KioskAppMenuHandler()
    : initialized_(false),
      weak_ptr_factory_(this) {
  KioskAppManager::Get()->AddObserver(this);
}

KioskAppMenuHandler::~KioskAppMenuHandler() {
  KioskAppManager::Get()->RemoveObserver(this);
}

void KioskAppMenuHandler::GetLocalizedStrings(
    base::DictionaryValue* localized_strings) {
  localized_strings->SetString(
      "showApps",
      l10n_util::GetStringUTF16(IDS_KIOSK_APPS_BUTTON));
}

void KioskAppMenuHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback("initializeKioskApps",
      base::Bind(&KioskAppMenuHandler::HandleInitializeKioskApps,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("kioskAppsLoaded",
      base::Bind(&KioskAppMenuHandler::HandleKioskAppsLoaded,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("launchKioskApp",
      base::Bind(&KioskAppMenuHandler::HandleLaunchKioskApps,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("checkKioskAppLaunchError",
      base::Bind(&KioskAppMenuHandler::HandleCheckKioskAppLaunchError,
                 base::Unretained(this)));
}

void KioskAppMenuHandler::SendKioskApps() {
  if (!initialized_)
    return;

  KioskAppManager::Apps apps;
  KioskAppManager::Get()->GetApps(&apps);

  base::ListValue apps_list;
  for (size_t i = 0; i < apps.size(); ++i) {
    const KioskAppManager::App& app_data = apps[i];

    scoped_ptr<base::DictionaryValue> app_info(new base::DictionaryValue);
    app_info->SetString("id", app_data.app_id);
    app_info->SetString("label", app_data.name);

    // TODO(xiyuan): Replace data url with a URLDataSource.
    std::string icon_url("chrome://theme/IDR_APP_DEFAULT_ICON");
    if (!app_data.icon.isNull())
      icon_url = webui::GetBitmapDataUrl(*app_data.icon.bitmap());
    app_info->SetString("iconUrl", icon_url);

    apps_list.Append(app_info.release());
  }

  web_ui()->CallJavascriptFunction("login.AppsMenuButton.setApps",
                                   apps_list);
}

void KioskAppMenuHandler::HandleInitializeKioskApps(
    const base::ListValue* args) {
  if (g_browser_process->browser_policy_connector()->IsEnterpriseManaged()) {
    initialized_ = true;
    SendKioskApps();
    return;
  }

  KioskAppManager::Get()->GetConsumerKioskModeStatus(
      base::Bind(&KioskAppMenuHandler::OnGetConsumerKioskModeStatus,
                 weak_ptr_factory_.GetWeakPtr()));
}

void KioskAppMenuHandler::OnGetConsumerKioskModeStatus(
    KioskAppManager::ConsumerKioskModeStatus status) {
  initialized_ =
      status == KioskAppManager::CONSUMER_KIOSK_MODE_ENABLED;
  SendKioskApps();
}

void KioskAppMenuHandler::HandleKioskAppsLoaded(
    const base::ListValue* args) {
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_KIOSK_APPS_LOADED,
      content::NotificationService::AllSources(),
      content::NotificationService::NoDetails());
}

void KioskAppMenuHandler::HandleLaunchKioskApps(const base::ListValue* args) {
  std::string app_id;
  CHECK(args->GetString(0, &app_id));

  KioskAppManager::App app_data;
  CHECK(KioskAppManager::Get()->GetApp(app_id, &app_data));

  ExistingUserController::current_controller()->PrepareKioskAppLaunch();

  // KioskAppLauncher deletes itself when done.
  (new KioskAppLauncher(KioskAppManager::Get(), app_id))->Start();
}

void KioskAppMenuHandler::HandleCheckKioskAppLaunchError(
    const base::ListValue* args) {
  KioskAppLaunchError::Error error = KioskAppLaunchError::Get();
  if (error == KioskAppLaunchError::NONE)
    return;
  KioskAppLaunchError::Clear();

  const std::string error_message = KioskAppLaunchError::GetErrorMessage(error);
  web_ui()->CallJavascriptFunction("login.AppsMenuButton.showError",
                                   base::StringValue(error_message));
}

void KioskAppMenuHandler::OnKioskAppsSettingsChanged() {
  SendKioskApps();
}

void KioskAppMenuHandler::OnKioskAppDataChanged(const std::string& app_id) {
  SendKioskApps();
}

}  // namespace chromeos
