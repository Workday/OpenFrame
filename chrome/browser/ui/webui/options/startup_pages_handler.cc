// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/options/startup_pages_handler.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/prefs/pref_service.h"
#include "chrome/browser/autocomplete/autocomplete_classifier.h"
#include "chrome/browser/autocomplete/autocomplete_controller.h"
#include "chrome/browser/autocomplete/autocomplete_input.h"
#include "chrome/browser/autocomplete/autocomplete_result.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/custom_home_pages_table_model.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/net/url_fixer_upper.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/web_ui.h"
#include "grit/generated_resources.h"

namespace options {

StartupPagesHandler::StartupPagesHandler() {}

StartupPagesHandler::~StartupPagesHandler() {

}

void StartupPagesHandler::GetLocalizedValues(
    DictionaryValue* localized_strings) {
  DCHECK(localized_strings);

  static OptionsStringResource resources[] = {
    { "startupAddLabel", IDS_OPTIONS_STARTUP_ADD_LABEL },
    { "startupUseCurrent", IDS_OPTIONS_STARTUP_USE_CURRENT },
    { "startupPagesPlaceholder", IDS_OPTIONS_STARTUP_PAGES_PLACEHOLDER },
  };

  RegisterStrings(localized_strings, resources, arraysize(resources));
  RegisterTitle(localized_strings, "startupPagesOverlay",
                IDS_OPTIONS_STARTUP_PAGES_DIALOG_TITLE);
}

void StartupPagesHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback("removeStartupPages",
      base::Bind(&StartupPagesHandler::RemoveStartupPages,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("addStartupPage",
      base::Bind(&StartupPagesHandler::AddStartupPage,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("editStartupPage",
      base::Bind(&StartupPagesHandler::EditStartupPage,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("setStartupPagesToCurrentPages",
      base::Bind(&StartupPagesHandler::SetStartupPagesToCurrentPages,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("dragDropStartupPage",
      base::Bind(&StartupPagesHandler::DragDropStartupPage,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "requestAutocompleteSuggestionsForStartupPages",
      base::Bind(&StartupPagesHandler::RequestAutocompleteSuggestions,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("commitStartupPrefChanges",
      base::Bind(&StartupPagesHandler::CommitChanges,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("cancelStartupPrefChanges",
      base::Bind(&StartupPagesHandler::CancelChanges,
                 base::Unretained(this)));
}

void StartupPagesHandler::UpdateStartupPages() {
  Profile* profile = Profile::FromWebUI(web_ui());
  const SessionStartupPref startup_pref =
      SessionStartupPref::GetStartupPref(profile->GetPrefs());
  startup_custom_pages_table_model_->SetURLs(startup_pref.urls);
}

void StartupPagesHandler::InitializeHandler() {
  Profile* profile = Profile::FromWebUI(web_ui());

  startup_custom_pages_table_model_.reset(
      new CustomHomePagesTableModel(profile));
  startup_custom_pages_table_model_->SetObserver(this);

  pref_change_registrar_.Init(profile->GetPrefs());
  pref_change_registrar_.Add(
      prefs::kURLsToRestoreOnStartup,
      base::Bind(&StartupPagesHandler::UpdateStartupPages,
                 base::Unretained(this)));

  autocomplete_controller_.reset(new AutocompleteController(profile, this,
      AutocompleteClassifier::kDefaultOmniboxProviders));
}

void StartupPagesHandler::InitializePage() {
  UpdateStartupPages();
}

void StartupPagesHandler::OnModelChanged() {
  ListValue startup_pages;
  int page_count = startup_custom_pages_table_model_->RowCount();
  std::vector<GURL> urls = startup_custom_pages_table_model_->GetURLs();
  for (int i = 0; i < page_count; ++i) {
    DictionaryValue* entry = new DictionaryValue();
    entry->SetString("title", startup_custom_pages_table_model_->GetText(i, 0));
    entry->SetString("url", urls[i].spec());
    entry->SetString("tooltip",
                     startup_custom_pages_table_model_->GetTooltip(i));
    entry->SetInteger("modelIndex", i);
    startup_pages.Append(entry);
  }

  web_ui()->CallJavascriptFunction("StartupOverlay.updateStartupPages",
                                   startup_pages);
}

void StartupPagesHandler::OnItemsChanged(int start, int length) {
  OnModelChanged();
}

void StartupPagesHandler::OnItemsAdded(int start, int length) {
  OnModelChanged();
}

void StartupPagesHandler::OnItemsRemoved(int start, int length) {
  OnModelChanged();
}

void StartupPagesHandler::SetStartupPagesToCurrentPages(
    const ListValue* args) {
  startup_custom_pages_table_model_->SetToCurrentlyOpenPages();
}

void StartupPagesHandler::RemoveStartupPages(const ListValue* args) {
  for (int i = args->GetSize() - 1; i >= 0; --i) {
    int selected_index;
    CHECK(args->GetInteger(i, &selected_index));

    if (selected_index < 0 ||
        selected_index >= startup_custom_pages_table_model_->RowCount()) {
      NOTREACHED();
      return;
    }
    startup_custom_pages_table_model_->Remove(selected_index);
  }
}

void StartupPagesHandler::AddStartupPage(const ListValue* args) {
  std::string url_string;
  CHECK_EQ(args->GetSize(), 1U);
  CHECK(args->GetString(0, &url_string));

  GURL url = URLFixerUpper::FixupURL(url_string, std::string());
  if (!url.is_valid())
    return;
  int index = startup_custom_pages_table_model_->RowCount();
  startup_custom_pages_table_model_->Add(index, url);
}

void StartupPagesHandler::EditStartupPage(const ListValue* args) {
  std::string url_string;
  int index;
  CHECK_EQ(args->GetSize(), 2U);
  CHECK(args->GetInteger(0, &index));
  CHECK(args->GetString(1, &url_string));

  if (index < 0 || index > startup_custom_pages_table_model_->RowCount()) {
    NOTREACHED();
    return;
  }

  std::vector<GURL> urls = startup_custom_pages_table_model_->GetURLs();
  urls[index] = URLFixerUpper::FixupURL(url_string, std::string());
  startup_custom_pages_table_model_->SetURLs(urls);
}

void StartupPagesHandler::DragDropStartupPage(const ListValue* args) {
  CHECK_EQ(args->GetSize(), 2U);

  int to_index;

  CHECK(args->GetInteger(0, &to_index));

  const ListValue* selected;
  CHECK(args->GetList(1, &selected));

  std::vector<int> index_list;
  for (size_t i = 0; i < selected->GetSize(); ++i) {
    int index;
    CHECK(selected->GetInteger(i, &index));
    index_list.push_back(index);
  }

  startup_custom_pages_table_model_->MoveURLs(to_index, index_list);
}

void StartupPagesHandler::SaveStartupPagesPref() {
  PrefService* prefs = Profile::FromWebUI(web_ui())->GetPrefs();

  SessionStartupPref pref = SessionStartupPref::GetStartupPref(prefs);
  pref.urls = startup_custom_pages_table_model_->GetURLs();

  if (pref.urls.empty())
    pref.type = SessionStartupPref::DEFAULT;

  SessionStartupPref::SetStartupPref(prefs, pref);
}

void StartupPagesHandler::CommitChanges(const ListValue* args) {
  SaveStartupPagesPref();
}

void StartupPagesHandler::CancelChanges(const ListValue* args) {
  UpdateStartupPages();
}

void StartupPagesHandler::RequestAutocompleteSuggestions(
    const ListValue* args) {
  string16 input;
  CHECK_EQ(args->GetSize(), 1U);
  CHECK(args->GetString(0, &input));

  autocomplete_controller_->Start(AutocompleteInput(
      input, string16::npos, string16(), GURL(),
      AutocompleteInput::INVALID_SPEC, true,
      false, false, AutocompleteInput::ALL_MATCHES));
}

void StartupPagesHandler::OnResultChanged(bool default_match_changed) {
  const AutocompleteResult& result = autocomplete_controller_->result();
  ListValue suggestions;
  OptionsUI::ProcessAutocompleteSuggestions(result, &suggestions);
  web_ui()->CallJavascriptFunction(
      "StartupOverlay.updateAutocompleteSuggestions", suggestions);
}

}  // namespace options
