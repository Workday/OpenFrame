// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/flags_ui.h"

#include <string>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/memory/ref_counted_memory.h"
#include "base/prefs/pref_registry_simple.h"
#include "base/prefs/pref_service.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/about_flags.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/pref_service_flags_storage.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_version_info.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "grit/browser_resources.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"

#if defined(OS_CHROMEOS)
#include "base/chromeos/chromeos_version.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/browser/chromeos/settings/owner_flags_storage.h"
#include "components/user_prefs/pref_registry_syncable.h"
#endif

using content::WebContents;
using content::WebUIMessageHandler;

namespace {

content::WebUIDataSource* CreateFlagsUIHTMLSource() {
  content::WebUIDataSource* source =
      content::WebUIDataSource::Create(chrome::kChromeUIFlagsHost);

  source->SetUseJsonJSFormatV2();
  source->AddLocalizedString("flagsLongTitle", IDS_FLAGS_LONG_TITLE);
  source->AddLocalizedString("flagsTableTitle", IDS_FLAGS_TABLE_TITLE);
  source->AddLocalizedString("flagsNoExperimentsAvailable",
                             IDS_FLAGS_NO_EXPERIMENTS_AVAILABLE);
  source->AddLocalizedString("flagsWarningHeader", IDS_FLAGS_WARNING_HEADER);
  source->AddLocalizedString("flagsBlurb", IDS_FLAGS_WARNING_TEXT);
  source->AddLocalizedString("channelPromoBeta",
                             IDS_FLAGS_PROMOTE_BETA_CHANNEL);
  source->AddLocalizedString("channelPromoDev", IDS_FLAGS_PROMOTE_DEV_CHANNEL);
  source->AddLocalizedString("flagsUnsupportedTableTitle",
                             IDS_FLAGS_UNSUPPORTED_TABLE_TITLE);
  source->AddLocalizedString("flagsNoUnsupportedExperiments",
                             IDS_FLAGS_NO_UNSUPPORTED_EXPERIMENTS);
  source->AddLocalizedString("flagsNotSupported", IDS_FLAGS_NOT_AVAILABLE);
  source->AddLocalizedString("flagsRestartNotice", IDS_FLAGS_RELAUNCH_NOTICE);
  source->AddLocalizedString("flagsRestartButton", IDS_FLAGS_RELAUNCH_BUTTON);
  source->AddLocalizedString("resetAllButton", IDS_FLAGS_RESET_ALL_BUTTON);
  source->AddLocalizedString("disable", IDS_FLAGS_DISABLE);
  source->AddLocalizedString("enable", IDS_FLAGS_ENABLE);

#if defined(OS_CHROMEOS)
  if (!chromeos::UserManager::Get()->IsCurrentUserOwner() &&
      base::chromeos::IsRunningOnChromeOS()) {
    // Set the strings to show which user can actually change the flags.
    std::string owner;
    chromeos::CrosSettings::Get()->GetString(chromeos::kDeviceOwner, &owner);
    source->AddString("ownerWarning",
                      l10n_util::GetStringFUTF16(IDS_SYSTEM_FLAGS_OWNER_ONLY,
                                                 UTF8ToUTF16(owner)));
  } else {
    source->AddString("ownerWarning", string16());
  }
#endif

  source->SetJsonPath("strings.js");
  source->AddResourcePath("flags.js", IDR_FLAGS_JS);
  source->SetDefaultResource(IDR_FLAGS_HTML);
  return source;
}

////////////////////////////////////////////////////////////////////////////////
//
// FlagsDOMHandler
//
////////////////////////////////////////////////////////////////////////////////

// The handler for Javascript messages for the about:flags page.
class FlagsDOMHandler : public WebUIMessageHandler {
 public:
  FlagsDOMHandler(about_flags::FlagsStorage* flags_storage,
                  about_flags::FlagAccess access)
      : flags_storage_(flags_storage), access_(access) {}
  virtual ~FlagsDOMHandler() {}

  // WebUIMessageHandler implementation.
  virtual void RegisterMessages() OVERRIDE;

  // Callback for the "requestFlagsExperiments" message.
  void HandleRequestFlagsExperiments(const ListValue* args);

  // Callback for the "enableFlagsExperiment" message.
  void HandleEnableFlagsExperimentMessage(const ListValue* args);

  // Callback for the "restartBrowser" message. Restores all tabs on restart.
  void HandleRestartBrowser(const ListValue* args);

  // Callback for the "resetAllFlags" message.
  void HandleResetAllFlags(const ListValue* args);

 private:
  scoped_ptr<about_flags::FlagsStorage> flags_storage_;
  about_flags::FlagAccess access_;

  DISALLOW_COPY_AND_ASSIGN(FlagsDOMHandler);
};

void FlagsDOMHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback("requestFlagsExperiments",
      base::Bind(&FlagsDOMHandler::HandleRequestFlagsExperiments,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("enableFlagsExperiment",
      base::Bind(&FlagsDOMHandler::HandleEnableFlagsExperimentMessage,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("restartBrowser",
      base::Bind(&FlagsDOMHandler::HandleRestartBrowser,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("resetAllFlags",
      base::Bind(&FlagsDOMHandler::HandleResetAllFlags,
                 base::Unretained(this)));
}

void FlagsDOMHandler::HandleRequestFlagsExperiments(const ListValue* args) {
  scoped_ptr<ListValue> supported_experiments(new ListValue);
  scoped_ptr<ListValue> unsupported_experiments(new ListValue);
  about_flags::GetFlagsExperimentsData(flags_storage_.get(),
                                       access_,
                                       supported_experiments.get(),
                                       unsupported_experiments.get());
  DictionaryValue results;
  results.Set("supportedExperiments", supported_experiments.release());
  results.Set("unsupportedExperiments", unsupported_experiments.release());
  results.SetBoolean("needsRestart",
                     about_flags::IsRestartNeededToCommitChanges());
#if defined(OS_WIN) || defined(OS_MACOSX) || defined(OS_CHROMEOS)
  chrome::VersionInfo::Channel channel = chrome::VersionInfo::GetChannel();
  results.SetBoolean("showBetaChannelPromotion",
                     channel == chrome::VersionInfo::CHANNEL_STABLE);
  results.SetBoolean("showDevChannelPromotion",
                     channel == chrome::VersionInfo::CHANNEL_BETA);
#else
  results.SetBoolean("showBetaChannelPromotion", false);
  results.SetBoolean("showDevChannelPromotion", false);
#endif
  web_ui()->CallJavascriptFunction("returnFlagsExperiments", results);
}

void FlagsDOMHandler::HandleEnableFlagsExperimentMessage(
    const ListValue* args) {
  DCHECK_EQ(2u, args->GetSize());
  if (args->GetSize() != 2)
    return;

  std::string experiment_internal_name;
  std::string enable_str;
  if (!args->GetString(0, &experiment_internal_name) ||
      !args->GetString(1, &enable_str))
    return;

  about_flags::SetExperimentEnabled(
      flags_storage_.get(),
      experiment_internal_name,
      enable_str == "true");
}

void FlagsDOMHandler::HandleRestartBrowser(const ListValue* args) {
  chrome::AttemptRestart();
}

void FlagsDOMHandler::HandleResetAllFlags(const ListValue* args) {
  about_flags::ResetAllFlags(flags_storage_.get());
}

}  // namespace

///////////////////////////////////////////////////////////////////////////////
//
// FlagsUI
//
///////////////////////////////////////////////////////////////////////////////

FlagsUI::FlagsUI(content::WebUI* web_ui)
    : WebUIController(web_ui),
      weak_factory_(this) {
  Profile* profile = Profile::FromWebUI(web_ui);

#if defined(OS_CHROMEOS)
  chromeos::DeviceSettingsService::Get()->GetOwnershipStatusAsync(
      base::Bind(&FlagsUI::FinishInitialization,
                 weak_factory_.GetWeakPtr(), profile));
#else
  web_ui->AddMessageHandler(
      new FlagsDOMHandler(new about_flags::PrefServiceFlagsStorage(
                              g_browser_process->local_state()),
                          about_flags::kOwnerAccessToFlags));

  // Set up the about:flags source.
  content::WebUIDataSource::Add(profile, CreateFlagsUIHTMLSource());
#endif
}

FlagsUI::~FlagsUI() {
}

// static
base::RefCountedMemory* FlagsUI::GetFaviconResourceBytes(
      ui::ScaleFactor scale_factor) {
  return ResourceBundle::GetSharedInstance().
      LoadDataResourceBytesForScale(IDR_FLAGS_FAVICON, scale_factor);
}

// static
void FlagsUI::RegisterPrefs(PrefRegistrySimple* registry) {
  registry->RegisterListPref(prefs::kEnabledLabsExperiments);
}

#if defined(OS_CHROMEOS)
// static
void FlagsUI::RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterListPref(prefs::kEnabledLabsExperiments,
                             user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
}

void FlagsUI::FinishInitialization(
    Profile* profile,
    chromeos::DeviceSettingsService::OwnershipStatus status,
    bool current_user_is_owner) {
  // On Chrome OS the owner can set system wide flags and other users can only
  // set flags for their own session.
  if (current_user_is_owner) {
    web_ui()->AddMessageHandler(
        new FlagsDOMHandler(new chromeos::about_flags::OwnerFlagsStorage(
                                profile->GetPrefs(),
                                chromeos::CrosSettings::Get()),
                            about_flags::kOwnerAccessToFlags));
  } else {
    web_ui()->AddMessageHandler(
        new FlagsDOMHandler(new about_flags::PrefServiceFlagsStorage(
                                profile->GetPrefs()),
                            about_flags::kGeneralAccessFlagsOnly));
  }

  // Set up the about:flags source.
  content::WebUIDataSource::Add(profile, CreateFlagsUIHTMLSource());
}
#endif
