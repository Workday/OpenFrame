// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_OMNIBOX_OMNIBOX_LOG_H_
#define CHROME_BROWSER_OMNIBOX_OMNIBOX_LOG_H_

#include <stddef.h>

#include "base/strings/string16.h"
#include "base/time/time.h"
#include "chrome/browser/autocomplete/autocomplete_input.h"
#include "chrome/browser/autocomplete/autocomplete_provider.h"
#include "chrome/browser/sessions/session_id.h"

class AutocompleteResult;

// The data to log (via the metrics service) when the user selects an item from
// the omnibox popup.
struct OmniboxLog {
  OmniboxLog(
      const string16& text,
      bool just_deleted_text,
      AutocompleteInput::Type input_type,
      size_t selected_index,
      SessionID::id_type tab_id,
      AutocompleteInput::PageClassification current_page_classification,
      base::TimeDelta elapsed_time_since_user_first_modified_omnibox,
      size_t completed_length,
      base::TimeDelta elapsed_time_since_last_change_to_default_match,
      const AutocompleteResult& result);
  ~OmniboxLog();

  // The user's input text in the omnibox.
  string16 text;

  // Whether the user deleted text immediately before selecting an omnibox
  // suggestion.  This is usually the result of pressing backspace or delete.
  bool just_deleted_text;

  // The detected type of the user's input.
  AutocompleteInput::Type input_type;

  // Selected index (if selected) or -1 (OmniboxPopupModel::kNoMatch).
  size_t selected_index;

  // ID of the tab the selected autocomplete suggestion was opened in.
  // Set to -1 if we haven't yet determined the destination tab.
  SessionID::id_type tab_id;

  // The type of page (e.g., new tab page, regular web page) that the
  // user was viewing before going somewhere with the omnibox.
  AutocompleteInput::PageClassification current_page_classification;

  // The amount of time since the user first began modifying the text
  // in the omnibox.  If at some point after modifying the text, the
  // user reverts the modifications (thus seeing the current web
  // page's URL again), then writes in the omnibox again, this time
  // delta should be computed starting from the second series of
  // modifications.  If we somehow skipped the logic to record
  // the time the user began typing (this should only happen in
  // unit tests), this elapsed time is set to -1 milliseconds.
  base::TimeDelta elapsed_time_since_user_first_modified_omnibox;

  // The number of extra characters the user would have to manually type
  // if she/he were not given the opportunity to select this match.  Set to
  // string16::npos if not available.
  size_t completed_length;

  // The amount of time since the last time the default (i.e., inline)
  // match changed.  This will certainly be less than
  // elapsed_time_since_user_first_modified_omnibox.
  base::TimeDelta elapsed_time_since_last_change_to_default_match;

  // Result set.
  const AutocompleteResult& result;

  // Diagnostic information from providers.  See
  // AutocompleteController::AddProvidersInfo() and
  // AutocompleteProvider::AddProviderInfo() above.
  ProvidersInfo providers_info;
};

#endif  // CHROME_BROWSER_OMNIBOX_OMNIBOX_LOG_H_
