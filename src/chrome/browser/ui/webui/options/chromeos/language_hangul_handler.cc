// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/options/chromeos/language_hangul_handler.h"

#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/chromeos/language_preferences.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

namespace chromeos {
namespace options {

LanguageHangulHandler::LanguageHangulHandler() {
}

LanguageHangulHandler::~LanguageHangulHandler() {
}

void LanguageHangulHandler::GetLocalizedValues(
    DictionaryValue* localized_strings) {
  DCHECK(localized_strings);

  RegisterTitle(localized_strings, "languageHangulPage",
                IDS_OPTIONS_SETTINGS_LANGUAGES_HANGUL_SETTINGS_TITLE);

  localized_strings->SetString("hangulKeyboardLayout",
      l10n_util::GetStringUTF16(IDS_OPTIONS_SETTINGS_KEYBOARD_LAYOUT_TEXT));

  localized_strings->Set("HangulkeyboardLayoutList", GetKeyboardLayoutList());
}

ListValue* LanguageHangulHandler::GetKeyboardLayoutList() {
  ListValue* keyboard_layout_list = new ListValue();
  for (size_t i = 0; i < language_prefs::kNumHangulKeyboardNameIDPairs; ++i) {
    ListValue* option = new ListValue();
    option->Append(new base::StringValue(
        language_prefs::kHangulKeyboardNameIDPairs[i].keyboard_id));
    option->Append(new base::StringValue(l10n_util::GetStringUTF16(
        language_prefs::kHangulKeyboardNameIDPairs[i].message_id)));
    keyboard_layout_list->Append(option);
  }
  return keyboard_layout_list;
}

}  // namespace options
}  // namespace chromeos
