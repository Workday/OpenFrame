// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/crashes_ui.h"

#include <vector>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/i18n/time_formatting.h"
#include "base/memory/ref_counted_memory.h"
#include "base/prefs/pref_service.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/crash_upload_list.h"
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
#include "chrome/browser/chromeos/settings/cros_settings.h"
#endif

using content::WebContents;
using content::WebUIMessageHandler;

namespace {

content::WebUIDataSource* CreateCrashesUIHTMLSource() {
  content::WebUIDataSource* source =
      content::WebUIDataSource::Create(chrome::kChromeUICrashesHost);
  source->SetUseJsonJSFormatV2();

  source->AddLocalizedString("crashesTitle", IDS_CRASHES_TITLE);
  source->AddLocalizedString("crashCountFormat",
                             IDS_CRASHES_CRASH_COUNT_BANNER_FORMAT);
  source->AddLocalizedString("crashHeaderFormat",
                             IDS_CRASHES_CRASH_HEADER_FORMAT);
  source->AddLocalizedString("crashTimeFormat", IDS_CRASHES_CRASH_TIME_FORMAT);
  source->AddLocalizedString("bugLinkText", IDS_CRASHES_BUG_LINK_LABEL);
  source->AddLocalizedString("noCrashesMessage",
                             IDS_CRASHES_NO_CRASHES_MESSAGE);
  source->AddLocalizedString("disabledHeader", IDS_CRASHES_DISABLED_HEADER);
  source->AddLocalizedString("disabledMessage", IDS_CRASHES_DISABLED_MESSAGE);
  source->SetJsonPath("strings.js");
  source->AddResourcePath("crashes.js", IDR_CRASHES_JS);
  source->SetDefaultResource(IDR_CRASHES_HTML);
  return source;
}

////////////////////////////////////////////////////////////////////////////////
//
// CrashesDOMHandler
//
////////////////////////////////////////////////////////////////////////////////

// The handler for Javascript messages for the chrome://crashes/ page.
class CrashesDOMHandler : public WebUIMessageHandler,
                          public CrashUploadList::Delegate {
 public:
  explicit CrashesDOMHandler();
  virtual ~CrashesDOMHandler();

  // WebUIMessageHandler implementation.
  virtual void RegisterMessages() OVERRIDE;

  // CrashUploadList::Delegate implemenation.
  virtual void OnUploadListAvailable() OVERRIDE;

 private:
  // Asynchronously fetches the list of crashes. Called from JS.
  void HandleRequestCrashes(const ListValue* args);

  // Sends the recent crashes list JS.
  void UpdateUI();

  scoped_refptr<CrashUploadList> upload_list_;
  bool list_available_;
  bool js_request_pending_;

  DISALLOW_COPY_AND_ASSIGN(CrashesDOMHandler);
};

CrashesDOMHandler::CrashesDOMHandler()
    : list_available_(false), js_request_pending_(false) {
  upload_list_ = CrashUploadList::Create(this);
}

CrashesDOMHandler::~CrashesDOMHandler() {
  upload_list_->ClearDelegate();
}

void CrashesDOMHandler::RegisterMessages() {
  upload_list_->LoadUploadListAsynchronously();

  web_ui()->RegisterMessageCallback("requestCrashList",
      base::Bind(&CrashesDOMHandler::HandleRequestCrashes,
                 base::Unretained(this)));
}

void CrashesDOMHandler::HandleRequestCrashes(const ListValue* args) {
  if (!CrashesUI::CrashReportingUIEnabled() || list_available_)
    UpdateUI();
  else
    js_request_pending_ = true;
}

void CrashesDOMHandler::OnUploadListAvailable() {
  list_available_ = true;
  if (js_request_pending_)
    UpdateUI();
}

void CrashesDOMHandler::UpdateUI() {
  bool crash_reporting_enabled = CrashesUI::CrashReportingUIEnabled();
  ListValue crash_list;

  if (crash_reporting_enabled) {
    std::vector<CrashUploadList::UploadInfo> crashes;
    upload_list_->GetUploads(50, &crashes);

    for (std::vector<CrashUploadList::UploadInfo>::iterator i = crashes.begin();
         i != crashes.end(); ++i) {
      DictionaryValue* crash = new DictionaryValue();
      crash->SetString("id", i->id);
      crash->SetString("time", base::TimeFormatFriendlyDateAndTime(i->time));
      crash_list.Append(crash);
    }
  }

  base::FundamentalValue enabled(crash_reporting_enabled);

  const chrome::VersionInfo version_info;
  base::StringValue version(version_info.Version());

  web_ui()->CallJavascriptFunction("updateCrashList", enabled, crash_list,
                                   version);
}

}  // namespace

///////////////////////////////////////////////////////////////////////////////
//
// CrashesUI
//
///////////////////////////////////////////////////////////////////////////////

CrashesUI::CrashesUI(content::WebUI* web_ui) : WebUIController(web_ui) {
  web_ui->AddMessageHandler(new CrashesDOMHandler());

  // Set up the chrome://crashes/ source.
  Profile* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource::Add(profile, CreateCrashesUIHTMLSource());
}

// static
base::RefCountedMemory* CrashesUI::GetFaviconResourceBytes(
      ui::ScaleFactor scale_factor) {
  return ResourceBundle::GetSharedInstance().
      LoadDataResourceBytesForScale(IDR_SAD_FAVICON, scale_factor);
}

// static
bool CrashesUI::CrashReportingUIEnabled() {
#if defined(GOOGLE_CHROME_BUILD)
#if defined(OS_CHROMEOS)
  bool reporting_enabled = false;
  chromeos::CrosSettings::Get()->GetBoolean(chromeos::kStatsReportingPref,
                                            &reporting_enabled);
  return reporting_enabled;
#elif defined(OS_ANDROID)
  // Android has it's own setings for metrics / crash uploading.
  PrefService* prefs = g_browser_process->local_state();
  return prefs->GetBoolean(prefs::kCrashReportingEnabled);
#else
  PrefService* prefs = g_browser_process->local_state();
  return prefs->GetBoolean(prefs::kMetricsReportingEnabled);
#endif
#else
  return false;
#endif
}
