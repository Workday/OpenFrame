// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/options/font_settings_handler.h"

#include <string>

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/i18n/rtl.h"
#include "base/prefs/pref_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/character_encoding.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/options/font_settings_utils.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/font_list_async.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/web_ui.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

#if defined(OS_WIN)
#include "ui/gfx/font.h"
#include "ui/gfx/platform_font_win.h"
#endif

namespace {

// Returns the localized name of a font so that settings can find it within the
// list of system fonts. On Windows, the list of system fonts has names only
// for the system locale, but the pref value may be in the English name.
std::string MaybeGetLocalizedFontName(const std::string& font_name) {
#if defined(OS_WIN)
  gfx::Font font(font_name, 12);  // dummy font size
  return static_cast<gfx::PlatformFontWin*>(font.platform_font())->
      GetLocalizedFontName();
#else
  return font_name;
#endif
}

}  // namespace


namespace options {

FontSettingsHandler::FontSettingsHandler() {
}

FontSettingsHandler::~FontSettingsHandler() {
}

void FontSettingsHandler::GetLocalizedValues(
    DictionaryValue* localized_strings) {
  DCHECK(localized_strings);

  static OptionsStringResource resources[] = {
    { "fontSettingsStandard",
      IDS_FONT_LANGUAGE_SETTING_FONT_SELECTOR_STANDARD_LABEL },
    { "fontSettingsSerif",
      IDS_FONT_LANGUAGE_SETTING_FONT_SELECTOR_SERIF_LABEL },
    { "fontSettingsSansSerif",
      IDS_FONT_LANGUAGE_SETTING_FONT_SELECTOR_SANS_SERIF_LABEL },
    { "fontSettingsFixedWidth",
      IDS_FONT_LANGUAGE_SETTING_FONT_SELECTOR_FIXED_WIDTH_LABEL },
    { "fontSettingsMinimumSize",
      IDS_FONT_LANGUAGE_SETTING_MINIMUM_FONT_SIZE_TITLE },
    { "fontSettingsEncoding",
      IDS_FONT_LANGUAGE_SETTING_FONT_SUB_DIALOG_ENCODING_TITLE },
    { "fontSettingsSizeTiny",
      IDS_FONT_LANGUAGE_SETTING_FONT_SIZE_TINY },
    { "fontSettingsSizeHuge",
      IDS_FONT_LANGUAGE_SETTING_FONT_SIZE_HUGE },
    { "fontSettingsLoremIpsum",
      IDS_FONT_LANGUAGE_SETTING_LOREM_IPSUM },
  };

  RegisterStrings(localized_strings, resources, arraysize(resources));
  RegisterTitle(localized_strings, "fontSettingsPage",
                IDS_FONT_LANGUAGE_SETTING_FONT_TAB_TITLE);
  localized_strings->SetString("fontSettingsPlaceholder",
      l10n_util::GetStringUTF16(
          IDS_FONT_LANGUAGE_SETTING_PLACEHOLDER));
}

void FontSettingsHandler::InitializePage() {
  DCHECK(web_ui());
  SetUpStandardFontSample();
  SetUpSerifFontSample();
  SetUpSansSerifFontSample();
  SetUpFixedFontSample();
  SetUpMinimumFontSample();
}

void FontSettingsHandler::RegisterMessages() {
  // Perform validation for saved fonts.
  PrefService* pref_service = Profile::FromWebUI(web_ui())->GetPrefs();
  FontSettingsUtilities::ValidateSavedFonts(pref_service);

  // Register for preferences that we need to observe manually.
  font_encoding_.Init(prefs::kDefaultCharset, pref_service);

  standard_font_.Init(prefs::kWebKitStandardFontFamily,
                      pref_service,
                      base::Bind(&FontSettingsHandler::SetUpStandardFontSample,
                                 base::Unretained(this)));
  serif_font_.Init(prefs::kWebKitSerifFontFamily,
                   pref_service,
                   base::Bind(&FontSettingsHandler::SetUpSerifFontSample,
                              base::Unretained(this)));
  sans_serif_font_.Init(
      prefs::kWebKitSansSerifFontFamily,
      pref_service,
      base::Bind(&FontSettingsHandler::SetUpSansSerifFontSample,
                 base::Unretained(this)));

  base::Closure callback = base::Bind(
      &FontSettingsHandler::SetUpFixedFontSample, base::Unretained(this));

  fixed_font_.Init(prefs::kWebKitFixedFontFamily, pref_service, callback);
  default_fixed_font_size_.Init(prefs::kWebKitDefaultFixedFontSize,
                                pref_service, callback);
  default_font_size_.Init(
      prefs::kWebKitDefaultFontSize,
      pref_service,
      base::Bind(&FontSettingsHandler::OnWebKitDefaultFontSizeChanged,
                 base::Unretained(this)));
  minimum_font_size_.Init(
      prefs::kWebKitMinimumFontSize,
      pref_service,
      base::Bind(&FontSettingsHandler::SetUpMinimumFontSample,
                 base::Unretained(this)));

  web_ui()->RegisterMessageCallback("fetchFontsData",
      base::Bind(&FontSettingsHandler::HandleFetchFontsData,
                 base::Unretained(this)));
}

void FontSettingsHandler::HandleFetchFontsData(const ListValue* args) {
  content::GetFontListAsync(
      base::Bind(&FontSettingsHandler::FontsListHasLoaded,
                 base::Unretained(this)));
}

void FontSettingsHandler::FontsListHasLoaded(
    scoped_ptr<base::ListValue> list) {
  // Selects the directionality for the fonts in the given list.
  for (size_t i = 0; i < list->GetSize(); i++) {
    ListValue* font;
    bool has_font = list->GetList(i, &font);
    DCHECK(has_font);
    string16 value;
    bool has_value = font->GetString(1, &value);
    DCHECK(has_value);
    bool has_rtl_chars = base::i18n::StringContainsStrongRTLChars(value);
    font->Append(new base::StringValue(has_rtl_chars ? "rtl" : "ltr"));
  }

  ListValue encoding_list;
  const std::vector<CharacterEncoding::EncodingInfo>* encodings;
  PrefService* pref_service = Profile::FromWebUI(web_ui())->GetPrefs();
  encodings = CharacterEncoding::GetCurrentDisplayEncodings(
      g_browser_process->GetApplicationLocale(),
      pref_service->GetString(prefs::kStaticEncodings),
      pref_service->GetString(prefs::kRecentlySelectedEncoding));
  DCHECK(encodings);
  DCHECK(!encodings->empty());

  std::vector<CharacterEncoding::EncodingInfo>::const_iterator it;
  for (it = encodings->begin(); it != encodings->end(); ++it) {
    ListValue* option = new ListValue();
    if (it->encoding_id) {
      int cmd_id = it->encoding_id;
      std::string encoding =
      CharacterEncoding::GetCanonicalEncodingNameByCommandId(cmd_id);
      string16 name = it->encoding_display_name;
      bool has_rtl_chars = base::i18n::StringContainsStrongRTLChars(name);
      option->Append(new base::StringValue(encoding));
      option->Append(new base::StringValue(name));
      option->Append(new base::StringValue(has_rtl_chars ? "rtl" : "ltr"));
    } else {
      // Add empty name/value to indicate a separator item.
      option->Append(new base::StringValue(std::string()));
      option->Append(new base::StringValue(std::string()));
    }
    encoding_list.Append(option);
  }

  ListValue selected_values;
  selected_values.Append(new base::StringValue(MaybeGetLocalizedFontName(
      standard_font_.GetValue())));
  selected_values.Append(new base::StringValue(MaybeGetLocalizedFontName(
      serif_font_.GetValue())));
  selected_values.Append(new base::StringValue(MaybeGetLocalizedFontName(
      sans_serif_font_.GetValue())));
  selected_values.Append(new base::StringValue(MaybeGetLocalizedFontName(
      fixed_font_.GetValue())));
  selected_values.Append(new base::StringValue(font_encoding_.GetValue()));

  web_ui()->CallJavascriptFunction("FontSettings.setFontsData",
                                   *list.get(), encoding_list,
                                   selected_values);
}

void FontSettingsHandler::SetUpStandardFontSample() {
  base::StringValue font_value(standard_font_.GetValue());
  base::FundamentalValue size_value(default_font_size_.GetValue());
  web_ui()->CallJavascriptFunction(
      "FontSettings.setUpStandardFontSample", font_value, size_value);
}

void FontSettingsHandler::SetUpSerifFontSample() {
  base::StringValue font_value(serif_font_.GetValue());
  base::FundamentalValue size_value(default_font_size_.GetValue());
  web_ui()->CallJavascriptFunction(
      "FontSettings.setUpSerifFontSample", font_value, size_value);
}

void FontSettingsHandler::SetUpSansSerifFontSample() {
  base::StringValue font_value(sans_serif_font_.GetValue());
  base::FundamentalValue size_value(default_font_size_.GetValue());
  web_ui()->CallJavascriptFunction(
      "FontSettings.setUpSansSerifFontSample", font_value, size_value);
}

void FontSettingsHandler::SetUpFixedFontSample() {
  base::StringValue font_value(fixed_font_.GetValue());
  base::FundamentalValue size_value(default_fixed_font_size_.GetValue());
  web_ui()->CallJavascriptFunction(
      "FontSettings.setUpFixedFontSample", font_value, size_value);
}

void FontSettingsHandler::SetUpMinimumFontSample() {
  base::FundamentalValue size_value(minimum_font_size_.GetValue());
  web_ui()->CallJavascriptFunction("FontSettings.setUpMinimumFontSample",
                                   size_value);
}

void FontSettingsHandler::OnWebKitDefaultFontSizeChanged() {
  SetUpStandardFontSample();
  SetUpSerifFontSample();
  SetUpSansSerifFontSample();
}

}  // namespace options
