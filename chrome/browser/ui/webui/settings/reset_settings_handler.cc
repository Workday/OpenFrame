// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/settings/reset_settings_handler.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/metrics/histogram.h"
#include "base/prefs/pref_service.h"
#include "base/strings/string16.h"
#include "base/values.h"
#include "chrome/browser/google/google_brand.h"
#include "chrome/browser/profile_resetter/brandcode_config_fetcher.h"
#include "chrome/browser/profile_resetter/brandcoded_default_settings.h"
#include "chrome/browser/profile_resetter/profile_resetter.h"
#include "chrome/browser/profile_resetter/resettable_settings_snapshot.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/url_constants.h"
#include "chrome/grit/chromium_strings.h"
#include "chrome/grit/generated_resources.h"
#include "content/public/browser/user_metrics.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"

#if defined(OS_CHROMEOS)
#include "base/prefs/pref_service.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browser_process_platform_part.h"
#include "chrome/browser/chromeos/policy/browser_policy_connector_chromeos.h"
#include "chrome/browser/chromeos/reset/metrics.h"
#include "chrome/common/pref_names.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/power_manager_client.h"
#include "components/user_manager/user_manager.h"
#endif  // defined(OS_CHROMEOS)

#if defined(OS_WIN)
#include "chrome/browser/profile_resetter/triggered_profile_resetter.h"
#include "chrome/browser/profile_resetter/triggered_profile_resetter_factory.h"
#endif  // defined(OS_WIN)

namespace settings {

ResetSettingsHandler::ResetSettingsHandler(
    content::WebUIDataSource* html_source, content::WebUI* web_ui) {
  google_brand::GetBrand(&brandcode_);
  Profile* profile = Profile::FromWebUI(web_ui);
  resetter_.reset(new ProfileResetter(profile));

#if defined(OS_CHROMEOS)
  policy::BrowserPolicyConnectorChromeOS* connector =
      g_browser_process->platform_part()->browser_policy_connector_chromeos();
  allow_powerwash_ = !connector->IsEnterpriseManaged() &&
      !user_manager::UserManager::Get()->IsLoggedInAsGuest() &&
      !user_manager::UserManager::Get()->IsLoggedInAsSupervisedUser();
  html_source->AddBoolean("allowPowerwash", allow_powerwash_);
#endif  // defined(OS_CHROMEOS)
}

ResetSettingsHandler::~ResetSettingsHandler() {}

void ResetSettingsHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback("performResetProfileSettings",
      base::Bind(&ResetSettingsHandler::HandleResetProfileSettings,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("onShowResetProfileDialog",
      base::Bind(&ResetSettingsHandler::OnShowResetProfileDialog,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("onHideResetProfileDialog",
      base::Bind(&ResetSettingsHandler::OnHideResetProfileDialog,
                 base::Unretained(this)));
#if defined(OS_CHROMEOS)
  web_ui()->RegisterMessageCallback(
       "onPowerwashDialogShow",
       base::Bind(&ResetSettingsHandler::OnShowPowerwashDialog,
                  base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "requestFactoryResetRestart",
      base::Bind(&ResetSettingsHandler::HandleFactoryResetRestart,
                 base::Unretained(this)));
#endif  // defined(OS_CHROMEOS)
}

void ResetSettingsHandler::HandleResetProfileSettings(
    const base::ListValue* value) {
  bool send_settings = false;
  bool success = value->GetBoolean(0, &send_settings);
  DCHECK(success);

  DCHECK(brandcode_.empty() || config_fetcher_);
  if (config_fetcher_ && config_fetcher_->IsActive()) {
    // Reset once the prefs are fetched.
    config_fetcher_->SetCallback(
        base::Bind(&ResetSettingsHandler::ResetProfile,
                   Unretained(this),
                   send_settings));
  } else {
    ResetProfile(send_settings);
  }
}

void ResetSettingsHandler::OnResetProfileSettingsDone(
    bool send_feedback) {
  web_ui()->CallJavascriptFunction("SettingsResetPage.doneResetting");
  if (send_feedback && setting_snapshot_) {
    Profile* profile = Profile::FromWebUI(web_ui());
    ResettableSettingsSnapshot current_snapshot(profile);
    int difference = setting_snapshot_->FindDifferentFields(current_snapshot);
    if (difference) {
      setting_snapshot_->Subtract(current_snapshot);
      std::string report = SerializeSettingsReport(*setting_snapshot_,
                                                   difference);
      SendSettingsFeedback(report, profile);
    }
  }
  setting_snapshot_.reset();
}

void ResetSettingsHandler::OnShowResetProfileDialog(
    const base::ListValue* value) {
  if (!resetter_->IsActive()) {
    setting_snapshot_.reset(
        new ResettableSettingsSnapshot(Profile::FromWebUI(web_ui())));
    setting_snapshot_->RequestShortcuts(base::Bind(
        &ResetSettingsHandler::UpdateFeedbackUI, AsWeakPtr()));
    UpdateFeedbackUI();
  }

  if (brandcode_.empty())
    return;
  config_fetcher_.reset(new BrandcodeConfigFetcher(
      base::Bind(&ResetSettingsHandler::OnSettingsFetched,
                 Unretained(this)),
      GURL("https://tools.google.com/service/update2"),
      brandcode_));
}

void ResetSettingsHandler::OnHideResetProfileDialog(
    const base::ListValue* value) {
  if (!resetter_->IsActive())
    setting_snapshot_.reset();
}

void ResetSettingsHandler::OnSettingsFetched() {
  DCHECK(config_fetcher_);
  DCHECK(!config_fetcher_->IsActive());
  // The master prefs is fetched. We are waiting for user pressing 'Reset'.
}

void ResetSettingsHandler::ResetProfile(bool send_settings) {
  DCHECK(resetter_);
  DCHECK(!resetter_->IsActive());

  scoped_ptr<BrandcodedDefaultSettings> default_settings;
  if (config_fetcher_) {
    DCHECK(!config_fetcher_->IsActive());
    default_settings = config_fetcher_->GetSettings();
    config_fetcher_.reset();
  } else {
    DCHECK(brandcode_.empty());
  }

  // If failed to fetch BrandcodedDefaultSettings or this is an organic
  // installation, use default settings.
  if (!default_settings)
    default_settings.reset(new BrandcodedDefaultSettings);
  resetter_->Reset(
      ProfileResetter::ALL,
      default_settings.Pass(),
      base::Bind(&ResetSettingsHandler::OnResetProfileSettingsDone,
                 AsWeakPtr(),
                 send_settings));
  content::RecordAction(base::UserMetricsAction("ResetProfile"));
  UMA_HISTOGRAM_BOOLEAN("ProfileReset.SendFeedback", send_settings);
}

void ResetSettingsHandler::UpdateFeedbackUI() {
  if (!setting_snapshot_)
    return;
  scoped_ptr<base::ListValue> list = GetReadableFeedbackForSnapshot(
      Profile::FromWebUI(web_ui()),
      *setting_snapshot_);
  base::DictionaryValue feedback_info;
  feedback_info.Set("feedbackInfo", list.release());
  web_ui()->CallJavascriptFunction(
      "SettingsResetPage.setFeedbackInfo", feedback_info);
}

#if defined(OS_CHROMEOS)
void ResetSettingsHandler::OnShowPowerwashDialog(
     const base::ListValue* args) {
  UMA_HISTOGRAM_ENUMERATION(
      "Reset.ChromeOS.PowerwashDialogShown",
      chromeos::reset::DIALOG_FROM_OPTIONS,
      chromeos::reset::DIALOG_VIEW_TYPE_SIZE);
}

void ResetSettingsHandler::HandleFactoryResetRestart(
    const base::ListValue* args) {
  if (!allow_powerwash_)
    return;

  PrefService* prefs = g_browser_process->local_state();
  prefs->SetBoolean(prefs::kFactoryResetRequested, true);
  prefs->CommitPendingWrite();

  // Perform sign out. Current chrome process will then terminate, new one will
  // be launched (as if it was a restart).
  chromeos::DBusThreadManager::Get()->GetPowerManagerClient()->RequestRestart();
}
#endif  // defined(OS_CHROMEOS)

}  // namespace settings
