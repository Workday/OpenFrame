// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/options/import_data_handler.h"

#include <string>

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/memory/scoped_ptr.h"
#include "base/strings/string16.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/importer/external_process_importer_host.h"
#include "chrome/browser/importer/importer_list.h"
#include "chrome/browser/importer/importer_uma.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/chrome_select_file_policy.h"
#include "content/public/browser/web_ui.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

namespace options {

ImportDataHandler::ImportDataHandler() : importer_host_(NULL),
                                         import_did_succeed_(false) {
}

ImportDataHandler::~ImportDataHandler() {
  if (importer_list_)
    importer_list_->set_observer(NULL);

  if (importer_host_)
    importer_host_->set_observer(NULL);

  if (select_file_dialog_)
    select_file_dialog_->ListenerDestroyed();
}

void ImportDataHandler::GetLocalizedValues(DictionaryValue* localized_strings) {
  DCHECK(localized_strings);

  static OptionsStringResource resources[] = {
    { "importFromLabel", IDS_IMPORT_FROM_LABEL },
    { "importLoading", IDS_IMPORT_LOADING_PROFILES },
    { "importDescription", IDS_IMPORT_ITEMS_LABEL },
    { "importHistory", IDS_IMPORT_HISTORY_CHKBOX },
    { "importFavorites", IDS_IMPORT_FAVORITES_CHKBOX },
    { "importSearch", IDS_IMPORT_SEARCH_ENGINES_CHKBOX },
    { "importPasswords", IDS_IMPORT_PASSWORDS_CHKBOX },
    { "importChooseFile", IDS_IMPORT_CHOOSE_FILE },
    { "importCommit", IDS_IMPORT_COMMIT },
    { "noProfileFound", IDS_IMPORT_NO_PROFILE_FOUND },
    { "importSucceeded", IDS_IMPORT_SUCCEEDED },
    { "findYourImportedBookmarks", IDS_IMPORT_FIND_YOUR_BOOKMARKS },
    { "macPasswordKeychain", IDS_IMPORT_PASSWORD_KEYCHAIN_WARNING },
  };

  RegisterStrings(localized_strings, resources, arraysize(resources));
  RegisterTitle(localized_strings, "importDataOverlay",
                IDS_IMPORT_SETTINGS_TITLE);
}

void ImportDataHandler::InitializeHandler() {
  importer_list_ = new ImporterList();
  importer_list_->DetectSourceProfiles(
      g_browser_process->GetApplicationLocale(), this);
}

void ImportDataHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "importData",
      base::Bind(&ImportDataHandler::ImportData, base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "chooseBookmarksFile",
      base::Bind(&ImportDataHandler::HandleChooseBookmarksFile,
                 base::Unretained(this)));
}

void ImportDataHandler::ImportData(const ListValue* args) {
  std::string string_value;

  int browser_index;
  if (!args->GetString(0, &string_value) ||
      !base::StringToInt(string_value, &browser_index)) {
    NOTREACHED();
    return;
  }

  uint16 selected_items = importer::NONE;
  if (args->GetString(1, &string_value) && string_value == "true") {
    selected_items |= importer::HISTORY;
  }
  if (args->GetString(2, &string_value) && string_value == "true") {
    selected_items |= importer::FAVORITES;
  }
  if (args->GetString(3, &string_value) && string_value == "true") {
    selected_items |= importer::PASSWORDS;
  }
  if (args->GetString(4, &string_value) && string_value == "true") {
    selected_items |= importer::SEARCH_ENGINES;
  }

  const importer::SourceProfile& source_profile =
      importer_list_->GetSourceProfileAt(browser_index);
  uint16 supported_items = source_profile.services_supported;

  uint16 import_services = (selected_items & supported_items);
  if (import_services) {
    base::FundamentalValue state(true);
    web_ui()->CallJavascriptFunction("ImportDataOverlay.setImportingState",
                                     state);
    import_did_succeed_ = false;

    importer_host_ = new ExternalProcessImporterHost();
    importer_host_->set_observer(this);
    Profile* profile = Profile::FromWebUI(web_ui());
    importer_host_->StartImportSettings(source_profile, profile,
                                        import_services,
                                        new ProfileWriter(profile));

    importer::LogImporterUseToMetrics("ImportDataHandler",
                                      source_profile.importer_type);
  } else {
    LOG(WARNING) << "There were no settings to import from '"
        << source_profile.importer_name << "'.";
  }
}

void ImportDataHandler::OnSourceProfilesLoaded() {
  InitializePage();
}

void ImportDataHandler::InitializePage() {
  if (!importer_list_->source_profiles_loaded())
    return;

  ListValue browser_profiles;
  for (size_t i = 0; i < importer_list_->count(); ++i) {
    const importer::SourceProfile& source_profile =
        importer_list_->GetSourceProfileAt(i);
    uint16 browser_services = source_profile.services_supported;

    DictionaryValue* browser_profile = new DictionaryValue();
    browser_profile->SetString("name", source_profile.importer_name);
    browser_profile->SetInteger("index", i);
    browser_profile->SetBoolean("history",
        (browser_services & importer::HISTORY) != 0);
    browser_profile->SetBoolean("favorites",
        (browser_services & importer::FAVORITES) != 0);
    browser_profile->SetBoolean("passwords",
        (browser_services & importer::PASSWORDS) != 0);
    browser_profile->SetBoolean("search",
        (browser_services & importer::SEARCH_ENGINES) != 0);

    browser_profile->SetBoolean("show_bottom_bar",
#if defined(OS_MACOSX)
        source_profile.importer_type == importer::TYPE_SAFARI);
#else
        false);
#endif

    browser_profiles.Append(browser_profile);
  }

  web_ui()->CallJavascriptFunction("ImportDataOverlay.updateSupportedBrowsers",
                                   browser_profiles);
}

void ImportDataHandler::ImportStarted() {
}

void ImportDataHandler::ImportItemStarted(importer::ImportItem item) {
  // TODO(csilv): show progress detail in the web view.
}

void ImportDataHandler::ImportItemEnded(importer::ImportItem item) {
  // TODO(csilv): show progress detail in the web view.
  import_did_succeed_ = true;
}

void ImportDataHandler::ImportEnded() {
  importer_host_->set_observer(NULL);
  importer_host_ = NULL;

  if (import_did_succeed_) {
    web_ui()->CallJavascriptFunction("ImportDataOverlay.confirmSuccess");
  } else {
    base::FundamentalValue state(false);
    web_ui()->CallJavascriptFunction("ImportDataOverlay.setImportingState",
                                     state);
    web_ui()->CallJavascriptFunction("ImportDataOverlay.dismiss");
  }
}

void ImportDataHandler::FileSelected(const base::FilePath& path,
                                     int index,
                                     void* params) {
  base::FundamentalValue importing(true);
  web_ui()->CallJavascriptFunction("ImportDataOverlay.setImportingState",
                                   importing);
  import_did_succeed_ = false;

  importer_host_ = new ExternalProcessImporterHost();
  importer_host_->set_observer(this);

  importer::SourceProfile source_profile;
  source_profile.importer_type = importer::TYPE_BOOKMARKS_FILE;
  source_profile.source_path = path;

  Profile* profile = Profile::FromWebUI(web_ui());

  importer_host_->StartImportSettings(
      source_profile, profile, importer::FAVORITES, new ProfileWriter(profile));
}

void ImportDataHandler::HandleChooseBookmarksFile(const base::ListValue* args) {
  DCHECK(args && args->empty());
  select_file_dialog_ = ui::SelectFileDialog::Create(
      this, new ChromeSelectFilePolicy(web_ui()->GetWebContents()));

  ui::SelectFileDialog::FileTypeInfo file_type_info;
  file_type_info.extensions.resize(1);
  file_type_info.extensions[0].push_back(FILE_PATH_LITERAL("html"));

  Browser* browser =
      chrome::FindBrowserWithWebContents(web_ui()->GetWebContents());

  select_file_dialog_->SelectFile(ui::SelectFileDialog::SELECT_OPEN_FILE,
                                  base::string16(),
                                  base::FilePath(),
                                  &file_type_info,
                                  0,
                                  base::FilePath::StringType(),
                                  browser->window()->GetNativeWindow(),
                                  NULL);
}

}  // namespace options
