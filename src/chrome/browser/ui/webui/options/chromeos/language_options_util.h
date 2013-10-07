// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_OPTIONS_CHROMEOS_LANGUAGE_OPTIONS_UTIL_H_
#define CHROME_BROWSER_UI_WEBUI_OPTIONS_CHROMEOS_LANGUAGE_OPTIONS_UTIL_H_

#include <string>

#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/chromeos/language_preferences.h"
#include "ui/base/l10n/l10n_util.h"

namespace base {
class ListValue;
}

namespace chromeos {
namespace options {

// Returns an i18n-content value corresponding to |preference|.
template <typename T>
std::string GetI18nContentValue(const T& preference, const char* prefix) {
  return std::string(prefix) + preference.ibus_config_name;
}

// Returns a property name of templateData corresponding to |preference|.
template <typename T>
std::string GetTemplateDataPropertyName(const T& preference,
                                        const char* prefix) {
  return std::string(prefix) + preference.ibus_config_name + "Value";
}

// Returns an property name of templateData corresponding the value of the min
// attribute.
template <typename T>
std::string GetTemplateDataMinName(const T& preference, const char* prefix) {
  return std::string(prefix) + preference.ibus_config_name + "Min";
}

// Returns an property name of templateData corresponding the value of the max
// attribute.
template <typename T>
std::string GetTemplateDataMaxName(const T& preference, const char* prefix) {
  return std::string(prefix) + preference.ibus_config_name + "Max";
}

// Creates a Value object from the given value. Here we use function
// overloading to handle string and integer preferences in
// CreateMultipleChoiceList.
base::Value* CreateValue(const char* in_value);
base::Value* CreateValue(int in_value);

// Creates a multiple choice list from the given preference.
template <typename T>
base::ListValue* CreateMultipleChoiceList(
    const language_prefs::LanguageMultipleChoicePreference<T>& preference) {
  int list_length = 0;
  for (size_t i = 0;
       i < language_prefs::LanguageMultipleChoicePreference<T>::kMaxItems;
       ++i) {
    if (preference.values_and_ids[i].item_message_id == 0)
      break;
    ++list_length;
  }
  DCHECK_GT(list_length, 0);

  base::ListValue* list_value = new base::ListValue();
  for (int i = 0; i < list_length; ++i) {
    base::ListValue* option = new base::ListValue();
    option->Append(CreateValue(
        preference.values_and_ids[i].ibus_config_value));
    option->Append(new base::StringValue(l10n_util::GetStringUTF16(
        preference.values_and_ids[i].item_message_id)));
    list_value->Append(option);
  }
  return list_value;
}

}  // namespace options
}  // namespace chromeos

#endif  // CHROME_BROWSER_UI_WEBUI_OPTIONS_CHROMEOS_LANGUAGE_OPTIONS_UTIL_H_
