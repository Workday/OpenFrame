// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/input_method/input_method_util.h"

#include <algorithm>
#include <functional>
#include <map>
#include <utility>

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "base/prefs/pref_service.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chromeos/ime/component_extension_ime_manager.h"
#include "chromeos/ime/extension_ime_util.h"
#include "chromeos/ime/input_method_delegate.h"
// TODO(nona): move this header from this file.
#include "grit/generated_resources.h"

namespace {

// A mapping from an input method id to a string for the language indicator. The
// mapping is necessary since some input methods belong to the same language.
// For example, both "xkb:us::eng" and "xkb:us:dvorak:eng" are for US English.
const struct {
  const char* input_method_id;
  const char* indicator_text;
} kMappingFromIdToIndicatorText[] = {
  // To distinguish from "xkb:us::eng"
  { "xkb:us:altgr-intl:eng", "EXTD" },
  { "xkb:us:dvorak:eng", "DV" },
  { "xkb:us:intl:eng", "INTL" },
  { "xkb:us:colemak:eng", "CO" },
  { "english-m", "??" },
  { "xkb:de:neo:ger", "NEO" },
  // To distinguish from "xkb:es::spa"
  { "xkb:es:cat:cat", "CAS" },
  // To distinguish from "xkb:gb::eng"
  { "xkb:gb:dvorak:eng", "DV" },
  // To distinguish from "xkb:jp::jpn"
  // TODO(nona): Make following variables configurable. http://crbug.com/232260.
  { "_comp_ime_fpfbhcjppmaeaijcidgiibchfbnhbeljnacl_mozc_us", "\xe3\x81\x82" },
  { "_comp_ime_fpfbhcjppmaeaijcidgiibchfbnhbeljnacl_mozc_jp", "\xe3\x81\x82" },
  { "_comp_ime_bbaiamgfapehflhememkfglaehiobjnknacl_mozc_us", "\xe3\x81\x82" },
  { "_comp_ime_bbaiamgfapehflhememkfglaehiobjnknacl_mozc_jp", "\xe3\x81\x82" },
  // For simplified Chinese input methods
  { "pinyin", "\xe6\x8b\xbc" },  // U+62FC
  { "_comp_ime_cpgalbafkoofkjmaeonnfijgpfennjjnzh-t-i0-pinyin",
    "\xe6\x8b\xbc" },
  { "_comp_ime_nmblnjkfdkabgdofidlkienfnnbjhnabzh-t-i0-pinyin",
    "\xe6\x8b\xbc" },
  { "_comp_ime_gjhclobljhjhgoebiipblnmdodbmpdgdzh-t-i0-wubi-1986",
    "\xe4\xba\x94" }, // U+4E94
  { "pinyin-dv", "\xe6\x8b\xbc" },
  // For traditional Chinese input methods
  { "mozc-chewing", "\xe9\x85\xb7" },  // U+9177
  { "_comp_ime_ekbifjdfhkmdeeajnolmgdlmkllopefizh-hant-t-i0-und",
    "\xe9\x85\xb7" },  // U+9177
  { "_comp_ime_goedamlknlnjaengojinmfgpmdjmkooozh-hant-t-i0-und",
    "\xe9\x85\xb7" },  // U+9177
  { "m17n:zh:cangjie", "\xe5\x80\x89" },  // U+5009
  { "_comp_ime_aeebooiibjahgpgmhkeocbeekccfknbjzh-hant-t-i0-cangjie-1987",
    "\xe5\x80\x89" },  // U+5009
  { "_comp_ime_gjhclobljhjhgoebiipblnmdodbmpdgdzh-hant-t-i0-cangjie-1987",
    "\xe5\x80\x89" },  // U+5009
  { "m17n:zh:quick", "\xe9\x80\x9f" },  // U+901F
  // For Hangul input method.
  { "mozc-hangul", "\xed\x95\x9c" },  // U+D55C
  { "_comp_ime_bdgdidmhaijohebebipajioienkglgfohangul_2set", "\xed\x95\x9c" },
  { "_comp_ime_bdgdidmhaijohebebipajioienkglgfohangul_3set390",
    "\xed\x95\x9c" },
  { "_comp_ime_bdgdidmhaijohebebipajioienkglgfohangul_3setfinal",
    "\xed\x95\x9c" },
  { "_comp_ime_bdgdidmhaijohebebipajioienkglgfohangul_3setnoshift",
    "\xed\x95\x9c" },
  { "_comp_ime_bdgdidmhaijohebebipajioienkglgfohangul_romaja", "\xed\x95\x9c" },
};

const size_t kMappingFromIdToIndicatorTextLen =
    ARRAYSIZE_UNSAFE(kMappingFromIdToIndicatorText);

// A mapping from an input method id to a resource id for a
// medium length language indicator.
// For those languages that want to display a slightly longer text in the
// "Your input method has changed to..." bubble than in the status tray.
// If an entry is not found in this table the short name is used.
const struct {
  const char* input_method_id;
  const int resource_id;
} kMappingImeIdToMediumLenNameResourceId[] = {
  { "m17n:zh:cangjie", IDS_LANGUAGES_MEDIUM_LEN_NAME_CHINESE_TRADITIONAL },
  { "m17n:zh:quick", IDS_LANGUAGES_MEDIUM_LEN_NAME_CHINESE_TRADITIONAL },
  { "mozc-chewing", IDS_LANGUAGES_MEDIUM_LEN_NAME_CHINESE_TRADITIONAL },
  { "mozc-hangul", IDS_LANGUAGES_MEDIUM_LEN_NAME_KOREAN },
  { "pinyin", IDS_LANGUAGES_MEDIUM_LEN_NAME_CHINESE_SIMPLIFIED },
  { "pinyin-dv", IDS_LANGUAGES_MEDIUM_LEN_NAME_CHINESE_SIMPLIFIED },
  { "_comp_ime_cpgalbafkoofkjmaeonnfijgpfennjjnzh-t-i0-pinyin",
    IDS_LANGUAGES_MEDIUM_LEN_NAME_CHINESE_SIMPLIFIED},
  { "_comp_ime_nmblnjkfdkabgdofidlkienfnnbjhnabzh-t-i0-pinyin",
    IDS_LANGUAGES_MEDIUM_LEN_NAME_CHINESE_SIMPLIFIED },
  { "_comp_ime_gjhclobljhjhgoebiipblnmdodbmpdgdzh-t-i0-wubi-1986",
    IDS_LANGUAGES_MEDIUM_LEN_NAME_CHINESE_SIMPLIFIED },
  { "_comp_ime_ekbifjdfhkmdeeajnolmgdlmkllopefizh-hant-t-i0-und",
    IDS_LANGUAGES_MEDIUM_LEN_NAME_CHINESE_TRADITIONAL },
  { "_comp_ime_goedamlknlnjaengojinmfgpmdjmkooozh-hant-t-i0-und",
    IDS_LANGUAGES_MEDIUM_LEN_NAME_CHINESE_TRADITIONAL },
  { "_comp_ime_aeebooiibjahgpgmhkeocbeekccfknbjzh-hant-t-i0-cangjie-1987",
    IDS_LANGUAGES_MEDIUM_LEN_NAME_CHINESE_TRADITIONAL },
  { "_comp_ime_gjhclobljhjhgoebiipblnmdodbmpdgdzh-hant-t-i0-cangjie-1987",
    IDS_LANGUAGES_MEDIUM_LEN_NAME_CHINESE_TRADITIONAL },
};
const size_t kMappingImeIdToMediumLenNameResourceIdLen =
    ARRAYSIZE_UNSAFE(kMappingImeIdToMediumLenNameResourceId);

// Due to asynchronous initialization of component extension manager,
// GetFirstLogingInputMethodIds may miss component extension IMEs. To enable
// component extension IME as the first loging input method, we have to prepare
// component extension IME IDs.
const struct {
  const char* locale;
  const char* layout;
  const char* input_method_id;
} kDefaultInputMethodRecommendation[] = {
  { "ja", "us", "_comp_ime_fpfbhcjppmaeaijcidgiibchfbnhbeljnacl_mozc_us" },
  { "ja", "jp", "_comp_ime_fpfbhcjppmaeaijcidgiibchfbnhbeljnacl_mozc_jp" },
  { "zh-CN", "us", "_comp_ime_nmblnjkfdkabgdofidlkienfnnbjhnabzh-t-i0-pinyin" },
  { "zh-TW", "us",
    "_comp_ime_goedamlknlnjaengojinmfgpmdjmkooozh-hant-t-i0-und" },
  { "th", "us", "_comp_ime_jhffeifommiaekmbkkjlpmilogcfdohpvkd_th" },
  { "vi", "us", "_comp_ime_jhffeifommiaekmbkkjlpmilogcfdohpvkd_vi_tcvn" },
  { "vi", "us", "_comp_ime_jhffeifommiaekmbkkjlpmilogcfdohpvkd_vi_tcvn" },
};

}  // namespace

namespace chromeos {

extern const char* kExtensionImePrefix;

namespace input_method {

namespace {

const struct EnglishToResouceId {
  const char* english_string_from_ibus;
  int resource_id;
} kEnglishToResourceIdArray[] = {
  // For ibus-mozc-hangul
  { "Hanja mode", IDS_STATUSBAR_IME_KOREAN_HANJA_INPUT_MODE },
  { "Hangul mode", IDS_STATUSBAR_IME_KOREAN_HANGUL_INPUT_MODE },

  // For ibus-mozc-pinyin.
  { "Full/Half width",
    IDS_STATUSBAR_IME_CHINESE_PINYIN_TOGGLE_FULL_HALF },
  { "Full/Half width punctuation",
    IDS_STATUSBAR_IME_CHINESE_PINYIN_TOGGLE_FULL_HALF_PUNCTUATION },
  // TODO(hsumita): Fixes a typo
  { "Simplfied/Traditional Chinese",
    IDS_STATUSBAR_IME_CHINESE_PINYIN_TOGGLE_S_T_CHINESE },
  { "Chinese",
    IDS_STATUSBAR_IME_CHINESE_PINYIN_TOGGLE_CHINESE_ENGLISH },

  // For ibus-mozc-chewing.
  { "English",
    IDS_STATUSBAR_IME_CHINESE_MOZC_CHEWING_ENGLISH_MODE },
  { "_Chinese",
    IDS_STATUSBAR_IME_CHINESE_MOZC_CHEWING_CHINESE_MODE },
  { "Full-width English",
    IDS_STATUSBAR_IME_CHINESE_MOZC_CHEWING_FULL_WIDTH_ENGLISH_MODE },

  // For the "Languages and Input" dialog.
  { "m17n:ar:kbd", IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_STANDARD_INPUT_METHOD },
  { "m17n:hi:itrans",  // also uses the "STANDARD_INPUT_METHOD" id.
    IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_STANDARD_INPUT_METHOD },
  { "m17n:zh:cangjie",
    IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_CHINESE_CANGJIE_INPUT_METHOD },
  { "m17n:zh:quick",
    IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_CHINESE_QUICK_INPUT_METHOD },
  { "m17n:fa:isiri",
    IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_PERSIAN_ISIRI_2901_INPUT_METHOD },
  { "m17n:th:kesmanee",
    IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_THAI_KESMANEE_INPUT_METHOD },
  { "m17n:th:tis820",
    IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_THAI_TIS820_INPUT_METHOD },
  { "m17n:th:pattachote",
    IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_THAI_PATTACHOTE_INPUT_METHOD },
  { "m17n:vi:tcvn",
    IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_VIETNAMESE_TCVN_INPUT_METHOD },
  { "m17n:vi:telex",
    IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_VIETNAMESE_TELEX_INPUT_METHOD },
  { "m17n:vi:viqr",
    IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_VIETNAMESE_VIQR_INPUT_METHOD },
  { "m17n:vi:vni",
    IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_VIETNAMESE_VNI_INPUT_METHOD },
  { "m17n:bn:itrans",
    IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_STANDARD_INPUT_METHOD },
  { "m17n:gu:itrans",
    IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_STANDARD_INPUT_METHOD },
  { "m17n:ml:itrans",
    IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_STANDARD_INPUT_METHOD },
  { "m17n:mr:itrans",
    IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_STANDARD_INPUT_METHOD },
  { "m17n:ta:phonetic",
    IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_TAMIL_PHONETIC },
  { "m17n:ta:inscript",
    IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_TAMIL_INSCRIPT },
  { "m17n:ta:tamil99",
    IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_TAMIL_TAMIL99 },
  { "m17n:ta:itrans",
    IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_TAMIL_ITRANS },
  { "m17n:ta:typewriter",
    IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_TAMIL_TYPEWRITER },
  { "m17n:am:sera",
    IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_STANDARD_INPUT_METHOD },
  { "m17n:te:itrans",
    IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_STANDARD_INPUT_METHOD },
  { "m17n:kn:itrans",
    IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_STANDARD_INPUT_METHOD },

  { "mozc-chewing",
    IDS_OPTIONS_SETTINGS_LANGUAGES_CHEWING_INPUT_METHOD },
  { "pinyin", IDS_OPTIONS_SETTINGS_LANGUAGES_PINYIN_INPUT_METHOD },
  { "pinyin-dv",
    IDS_OPTIONS_SETTINGS_LANGUAGES_PINYIN_DV_INPUT_METHOD },
  { "zinnia-japanese",
    IDS_OPTIONS_SETTINGS_LANGUAGES_JAPANESE_HANDWRITING_INPUT_METHOD },
  { "mozc-hangul", IDS_OPTIONS_SETTINGS_LANGUAGES_KOREAN_INPUT_METHOD },

  // For ibus-xkb-layouts engine: third_party/ibus-xkb-layouts/files
  { "xkb:jp::jpn", IDS_STATUSBAR_LAYOUT_JAPAN },
  { "xkb:si::slv", IDS_STATUSBAR_LAYOUT_SLOVENIA },
  { "xkb:de::ger", IDS_STATUSBAR_LAYOUT_GERMANY },
  { "xkb:de:neo:ger", IDS_STATUSBAR_LAYOUT_GERMANY_NEO2 },
  { "xkb:it::ita", IDS_STATUSBAR_LAYOUT_ITALY },
  { "xkb:ee::est", IDS_STATUSBAR_LAYOUT_ESTONIA },
  { "xkb:hu::hun", IDS_STATUSBAR_LAYOUT_HUNGARY },
  { "xkb:pl::pol", IDS_STATUSBAR_LAYOUT_POLAND },
  { "xkb:dk::dan", IDS_STATUSBAR_LAYOUT_DENMARK },
  { "xkb:hr::scr", IDS_STATUSBAR_LAYOUT_CROATIA },
  { "xkb:br::por", IDS_STATUSBAR_LAYOUT_BRAZIL },
  { "xkb:rs::srp", IDS_STATUSBAR_LAYOUT_SERBIA },
  { "xkb:cz::cze", IDS_STATUSBAR_LAYOUT_CZECHIA },
  { "xkb:cz:qwerty:cze", IDS_STATUSBAR_LAYOUT_CZECHIA_QWERTY },
  { "xkb:us:dvorak:eng", IDS_STATUSBAR_LAYOUT_USA_DVORAK },
  { "xkb:us:colemak:eng", IDS_STATUSBAR_LAYOUT_USA_COLEMAK },
  { "xkb:ro::rum", IDS_STATUSBAR_LAYOUT_ROMANIA },
  { "xkb:us::eng", IDS_STATUSBAR_LAYOUT_USA },
  { "xkb:us:altgr-intl:eng", IDS_STATUSBAR_LAYOUT_USA_EXTENDED },
  { "xkb:us:intl:eng", IDS_STATUSBAR_LAYOUT_USA_INTERNATIONAL },
  { "xkb:lt::lit", IDS_STATUSBAR_LAYOUT_LITHUANIA },
  { "xkb:gb:extd:eng", IDS_STATUSBAR_LAYOUT_UNITED_KINGDOM },
  { "xkb:gb:dvorak:eng", IDS_STATUSBAR_LAYOUT_UNITED_KINGDOM_DVORAK },
  { "xkb:sk::slo", IDS_STATUSBAR_LAYOUT_SLOVAKIA },
  { "xkb:ru::rus", IDS_STATUSBAR_LAYOUT_RUSSIA },
  { "xkb:ru:phonetic:rus", IDS_STATUSBAR_LAYOUT_RUSSIA_PHONETIC },
  { "xkb:gr::gre", IDS_STATUSBAR_LAYOUT_GREECE },
  { "xkb:be::fra", IDS_STATUSBAR_LAYOUT_BELGIUM },
  { "xkb:be::ger", IDS_STATUSBAR_LAYOUT_BELGIUM },
  { "xkb:be::nld", IDS_STATUSBAR_LAYOUT_BELGIUM },
  { "xkb:bg::bul", IDS_STATUSBAR_LAYOUT_BULGARIA },
  { "xkb:bg:phonetic:bul", IDS_STATUSBAR_LAYOUT_BULGARIA_PHONETIC },
  { "xkb:ch::ger", IDS_STATUSBAR_LAYOUT_SWITZERLAND },
  { "xkb:ch:fr:fra", IDS_STATUSBAR_LAYOUT_SWITZERLAND_FRENCH },
  { "xkb:tr::tur", IDS_STATUSBAR_LAYOUT_TURKEY },
  { "xkb:pt::por", IDS_STATUSBAR_LAYOUT_PORTUGAL },
  { "xkb:es::spa", IDS_STATUSBAR_LAYOUT_SPAIN },
  { "xkb:fi::fin", IDS_STATUSBAR_LAYOUT_FINLAND },
  { "xkb:ua::ukr", IDS_STATUSBAR_LAYOUT_UKRAINE },
  { "xkb:es:cat:cat", IDS_STATUSBAR_LAYOUT_SPAIN_CATALAN },
  { "xkb:fr::fra", IDS_STATUSBAR_LAYOUT_FRANCE },
  { "xkb:no::nob", IDS_STATUSBAR_LAYOUT_NORWAY },
  { "xkb:se::swe", IDS_STATUSBAR_LAYOUT_SWEDEN },
  { "xkb:nl::nld", IDS_STATUSBAR_LAYOUT_NETHERLANDS },
  { "xkb:latam::spa", IDS_STATUSBAR_LAYOUT_LATIN_AMERICAN },
  { "xkb:lv:apostrophe:lav", IDS_STATUSBAR_LAYOUT_LATVIA },
  { "xkb:ca::fra", IDS_STATUSBAR_LAYOUT_CANADA },
  { "xkb:ca:eng:eng", IDS_STATUSBAR_LAYOUT_CANADA_ENGLISH },
  { "xkb:il::heb", IDS_STATUSBAR_LAYOUT_ISRAEL },
  { "xkb:kr:kr104:kor", IDS_STATUSBAR_LAYOUT_KOREA_104 },
  { "xkb:is::ice", IDS_STATUSBAR_LAYOUT_ICELANDIC },
  { "xkb:ca:multix:fra", IDS_STATUSBAR_LAYOUT_CANADIAN_MULTILINGUAL },
  { "xkb:by::bel", IDS_STATUSBAR_LAYOUT_BELARUSIAN },
  { "xkb:am:phonetic:arm", IDS_STATUSBAR_LAYOUT_ARMENIAN_PHONETIC },
  { "xkb:ge::geo", IDS_STATUSBAR_LAYOUT_GEORGIAN },
  { "xkb:mn::mon", IDS_STATUSBAR_LAYOUT_MONGOLIAN },

  { "english-m", IDS_STATUSBAR_LAYOUT_USA_MYSTERY },
};
const size_t kEnglishToResourceIdArraySize =
    arraysize(kEnglishToResourceIdArray);

// The list of language that do not have associated input methods in IBus.
// For these languages, we associate input methods here.
const struct ExtraLanguage {
  const char* language_code;
  const char* input_method_id;
} kExtraLanguages[] = {
  // Language Code  Input Method ID
  { "en-AU", "xkb:us::eng" },  // For Austrailia, use US keyboard layout.
  { "id", "xkb:us::eng" },  // For Indonesian, use US keyboard layout.
  // The code "fil" comes from l10_util.cc.
  { "fil", "xkb:us::eng" },  // For Filipino, use US keyboard layout.
  // For Netherlands, use US international keyboard layout.
  { "nl", "xkb:us:intl:eng" },
  // The code "es-419" comes from l10_util.cc.
  // For Spanish in Latin America, use Latin American keyboard layout.
  { "es-419", "xkb:latam::spa" },
  // For Malay, use US keyboard layout. crosbug.com/p/8288
  { "ms", "xkb:us::eng" },
  // For Brazil, it is common to use US-international keyboard layout.
  { "pt-BR", "xkb:us:intl:eng" },

  // TODO(yusukes): Add {"sw", "xkb:us::eng"} once Swahili is removed from the
  // blacklist in src/ui/base/l10n/l10n_util_posix.cc.
};
const size_t kExtraLanguagesLength = arraysize(kExtraLanguages);

}  // namespace

InputMethodUtil::InputMethodUtil(
    InputMethodDelegate* delegate,
    scoped_ptr<InputMethodDescriptors> supported_input_methods)
    : supported_input_methods_(supported_input_methods.Pass()),
      delegate_(delegate) {
  ReloadInternalMaps();

  // Initialize a map from English string to Chrome string resource ID as well.
  for (size_t i = 0; i < kEnglishToResourceIdArraySize; ++i) {
    const EnglishToResouceId& map_entry = kEnglishToResourceIdArray[i];
    const bool result = english_to_resource_id_.insert(std::make_pair(
        map_entry.english_string_from_ibus, map_entry.resource_id)).second;
    DCHECK(result) << "Duplicated string is found: "
                   << map_entry.english_string_from_ibus;
  }
}

InputMethodUtil::~InputMethodUtil() {
}

bool InputMethodUtil::TranslateStringInternal(
    const std::string& english_string, string16 *out_string) const {
  DCHECK(out_string);
  HashType::const_iterator iter = english_to_resource_id_.find(english_string);
  if (iter == english_to_resource_id_.end()) {
    // TODO(yusukes): Write Autotest which checks if all display names and all
    // property names for supported input methods are listed in the resource
    // ID array (crosbug.com/4572).
    LOG(ERROR) << "Resource ID is not found for: " << english_string;
    return false;
  }

  *out_string = delegate_->GetLocalizedString(iter->second);
  return true;
}

string16 InputMethodUtil::TranslateString(
    const std::string& english_string) const {
  string16 localized_string;
  if (TranslateStringInternal(english_string, &localized_string)) {
    return localized_string;
  }
  return UTF8ToUTF16(english_string);
}

bool InputMethodUtil::IsValidInputMethodId(
    const std::string& input_method_id) const {
  // We can't check the component extension is whilelisted or not here because
  // it might not be initialized.
  return GetInputMethodDescriptorFromId(input_method_id) != NULL ||
      ComponentExtensionIMEManager::IsComponentExtensionIMEId(input_method_id);
}

// static
bool InputMethodUtil::IsKeyboardLayout(const std::string& input_method_id) {
  const bool kCaseInsensitive = false;
  return StartsWithASCII(input_method_id, "xkb:", kCaseInsensitive);
}

std::string InputMethodUtil::GetLanguageCodeFromInputMethodId(
    const std::string& input_method_id) const {
  // The code should be compatible with one of codes used for UI languages,
  // defined in app/l10_util.cc.
  const char kDefaultLanguageCode[] = "en-US";
  std::map<std::string, std::string>::const_iterator iter
      = id_to_language_code_.find(input_method_id);
  return (iter == id_to_language_code_.end()) ?
      // Returning |kDefaultLanguageCode| here is not for Chrome OS but for
      // Ubuntu where the ibus-xkb-layouts engine could be missing.
      kDefaultLanguageCode : iter->second;
}

std::string InputMethodUtil::GetKeyboardLayoutName(
    const std::string& input_method_id) const {
  InputMethodIdToDescriptorMap::const_iterator iter
      = id_to_descriptor_.find(input_method_id);
  return (iter == id_to_descriptor_.end()) ?
      "" : iter->second.GetPreferredKeyboardLayout();
}

std::string InputMethodUtil::GetInputMethodDisplayNameFromId(
    const std::string& input_method_id) const {
  string16 display_name;
  if (!extension_ime_util::IsExtensionIME(input_method_id) &&
      TranslateStringInternal(input_method_id, &display_name)) {
    return UTF16ToUTF8(display_name);
  }
  // Return an empty string if the display name is not found.
  return "";
}

string16 InputMethodUtil::GetInputMethodShortName(
    const InputMethodDescriptor& input_method) const {
  // For the status area, we use two-letter, upper-case language code like
  // "US" and "JP".
  string16 text;

  // Check special cases first.
  for (size_t i = 0; i < kMappingFromIdToIndicatorTextLen; ++i) {
    if (kMappingFromIdToIndicatorText[i].input_method_id == input_method.id()) {
      text = UTF8ToUTF16(kMappingFromIdToIndicatorText[i].indicator_text);
      break;
    }
  }

  // Display the keyboard layout name when using a keyboard layout.
  if (text.empty() &&
      IsKeyboardLayout(input_method.id())) {
    const size_t kMaxKeyboardLayoutNameLen = 2;
    const string16 keyboard_layout =
        UTF8ToUTF16(GetKeyboardLayoutName(input_method.id()));
    text = StringToUpperASCII(keyboard_layout).substr(
        0, kMaxKeyboardLayoutNameLen);
  }

  // TODO(yusukes): Some languages have two or more input methods. For example,
  // Thai has 3, Vietnamese has 4. If these input methods could be activated at
  // the same time, we should do either of the following:
  //   (1) Add mappings to |kMappingFromIdToIndicatorText|
  //   (2) Add suffix (1, 2, ...) to |text| when ambiguous.

  if (text.empty()) {
    const size_t kMaxLanguageNameLen = 2;
    DCHECK(!input_method.language_codes().empty());
    const std::string language_code = input_method.language_codes().at(0);
    text = StringToUpperASCII(UTF8ToUTF16(language_code)).substr(
        0, kMaxLanguageNameLen);
  }
  DCHECK(!text.empty());
  return text;
}

string16 InputMethodUtil::GetInputMethodMediumName(
    const InputMethodDescriptor& input_method) const {
  // For the "Your input method has changed to..." bubble. In most cases
  // it uses the same name as the short name, unless found in a table
  // for medium length names.
  for (size_t i = 0; i < kMappingImeIdToMediumLenNameResourceIdLen; ++i) {
    if (kMappingImeIdToMediumLenNameResourceId[i].input_method_id ==
        input_method.id()) {
      return delegate_->GetLocalizedString(
          kMappingImeIdToMediumLenNameResourceId[i].resource_id);
    }
  }
  return GetInputMethodShortName(input_method);
}

string16 InputMethodUtil::GetInputMethodLongName(
    const InputMethodDescriptor& input_method) const {
  if (!input_method.name().empty()) {
    // If the descriptor has a name, use it.
    return UTF8ToUTF16(input_method.name());
  }

  // We don't show language here.  Name of keyboard layout or input method
  // usually imply (or explicitly include) its language.

  // Special case for German, French and Dutch: these languages have multiple
  // keyboard layouts and share the same layout of keyboard (Belgian). We need
  // to show explicitly the language for the layout. For Arabic, Amharic, and
  // Indic languages: they share "Standard Input Method".
  const string16 standard_input_method_text = delegate_->GetLocalizedString(
      IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_STANDARD_INPUT_METHOD);
  DCHECK(!input_method.language_codes().empty());
  const std::string language_code = input_method.language_codes().at(0);

  string16 text = TranslateString(input_method.id());
  if (text == standard_input_method_text ||
             language_code == "de" ||
             language_code == "fr" ||
             language_code == "nl") {
    const string16 language_name = delegate_->GetDisplayLanguageName(
        language_code);

    text = language_name + UTF8ToUTF16(" - ") + text;
  }

  DCHECK(!text.empty());
  return text;
}

const InputMethodDescriptor* InputMethodUtil::GetInputMethodDescriptorFromId(
    const std::string& input_method_id) const {
  InputMethodIdToDescriptorMap::const_iterator iter
      = id_to_descriptor_.find(input_method_id);
  return (iter == id_to_descriptor_.end()) ? NULL : &(iter->second);
}

std::vector<std::string> InputMethodUtil::GetExtraLanguageCodesFromId(
    const std::string& input_method_id) const {
  std::vector<std::string> result;
  for (size_t i = 0; i < kExtraLanguagesLength; ++i) {
    if (input_method_id == kExtraLanguages[i].input_method_id)
      result.push_back(kExtraLanguages[i].language_code);
  }
  return result;
}

std::vector<std::string> InputMethodUtil::GetExtraLanguageCodeList() const {
  std::vector<std::string> result;
  for (size_t i = 0; i < kExtraLanguagesLength; ++i)
    result.push_back(kExtraLanguages[i].language_code);
  return result;
}

bool InputMethodUtil::GetInputMethodIdsFromLanguageCode(
    const std::string& normalized_language_code,
    InputMethodType type,
    std::vector<std::string>* out_input_method_ids) const {
  return GetInputMethodIdsFromLanguageCodeInternal(
      language_code_to_ids_,
      normalized_language_code, type, out_input_method_ids);
}

bool InputMethodUtil::GetInputMethodIdsFromLanguageCodeInternal(
    const std::multimap<std::string, std::string>& language_code_to_ids,
    const std::string& normalized_language_code,
    InputMethodType type,
    std::vector<std::string>* out_input_method_ids) const {
  DCHECK(out_input_method_ids);
  out_input_method_ids->clear();

  bool result = false;
  std::pair<LanguageCodeToIdsMap::const_iterator,
      LanguageCodeToIdsMap::const_iterator> range =
      language_code_to_ids.equal_range(normalized_language_code);
  for (LanguageCodeToIdsMap::const_iterator iter = range.first;
       iter != range.second; ++iter) {
    const std::string& input_method_id = iter->second;
    if ((type == kAllInputMethods) || IsKeyboardLayout(input_method_id)) {
      out_input_method_ids->push_back(input_method_id);
      result = true;
    }
  }
  if ((type == kAllInputMethods) && !result) {
    DVLOG(1) << "Unknown language code: " << normalized_language_code;
  }
  return result;
}

void InputMethodUtil::GetFirstLoginInputMethodIds(
    const std::string& language_code,
    const InputMethodDescriptor& current_input_method,
    std::vector<std::string>* out_input_method_ids) const {
  out_input_method_ids->clear();

  // First, add the current keyboard layout (one used on the login screen).
  out_input_method_ids->push_back(current_input_method.id());

  const std::string current_layout
      = current_input_method.GetPreferredKeyboardLayout();
  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(kDefaultInputMethodRecommendation);
       ++i) {
    if (kDefaultInputMethodRecommendation[i].locale == language_code &&
        kDefaultInputMethodRecommendation[i].layout == current_layout) {
      out_input_method_ids->push_back(
          kDefaultInputMethodRecommendation[i].input_method_id);
      return;
    }
  }

  // Second, find the most popular input method associated with the
  // current UI language. The input method IDs returned from
  // GetInputMethodIdsFromLanguageCode() are sorted by popularity, hence
  // our basic strategy is to pick the first one, but it's a bit more
  // complicated as shown below.
  std::string most_popular_id;
  std::vector<std::string> input_method_ids;
  // This returns the input methods sorted by popularity.
  GetInputMethodIdsFromLanguageCode(
      language_code, kAllInputMethods, &input_method_ids);
  for (size_t i = 0; i < input_method_ids.size(); ++i) {
    const std::string& input_method_id = input_method_ids[i];
    // Pick the first one.
    if (most_popular_id.empty())
      most_popular_id = input_method_id;

    // Check if there is one that matches the current keyboard layout, but
    // not the current keyboard itself. This is useful if there are
    // multiple keyboard layout choices for one input method. For
    // instance, Mozc provides three choices: mozc (US keyboard), mozc-jp
    // (JP keyboard), mozc-dv (Dvorak).
    const InputMethodDescriptor* descriptor =
        GetInputMethodDescriptorFromId(input_method_id);
    if (descriptor &&
        descriptor->id() != current_input_method.id() &&
        descriptor->GetPreferredKeyboardLayout() ==
        current_input_method.GetPreferredKeyboardLayout()) {
      most_popular_id = input_method_id;
      break;
    }
  }
  // Add the most popular input method ID, if it's different from the
  // current input method.
  if (most_popular_id != current_input_method.id() &&
      // TODO(yusukes): Remove this hack when we remove the "english-m" IME.
      most_popular_id != "english-m") {
    out_input_method_ids->push_back(most_popular_id);
  }
}

void InputMethodUtil::GetLanguageCodesFromInputMethodIds(
    const std::vector<std::string>& input_method_ids,
    std::vector<std::string>* out_language_codes) const {
  out_language_codes->clear();

  for (size_t i = 0; i < input_method_ids.size(); ++i) {
    const std::string& input_method_id = input_method_ids[i];
    const InputMethodDescriptor* input_method =
        GetInputMethodDescriptorFromId(input_method_id);
    if (!input_method) {
      DVLOG(1) << "Unknown input method ID: " << input_method_ids[i];
      continue;
    }
    DCHECK(!input_method->language_codes().empty());
    const std::string language_code = input_method->language_codes().at(0);
    // Add it if it's not already present.
    if (std::count(out_language_codes->begin(), out_language_codes->end(),
                   language_code) == 0) {
      out_language_codes->push_back(language_code);
    }
  }
}

std::string InputMethodUtil::GetHardwareInputMethodId() const {
  const std::string input_method_id = delegate_->GetHardwareKeyboardLayout();

  if (input_method_id.empty()) {
    // This is totally fine if it's empty. The hardware keyboard layout is
    // not stored if startup_manifest.json (OEM customization data) is not
    // present (ex. Cr48 doen't have that file).
    return GetFallbackInputMethodDescriptor().id();
  }
  return input_method_id;
}

void InputMethodUtil::SetComponentExtensions(
    const InputMethodDescriptors& imes) {
  component_extension_ime_id_to_descriptor_.clear();
  for (size_t i = 0; i < imes.size(); ++i) {
    const InputMethodDescriptor& input_method = imes.at(i);
    DCHECK(!input_method.language_codes().empty());
    const std::string language_code = input_method.language_codes().at(0);
    id_to_language_code_.insert(
        std::make_pair(input_method.id(), language_code));
    id_to_descriptor_.insert(
        std::make_pair(input_method.id(), input_method));
  }
}

InputMethodDescriptor InputMethodUtil::GetFallbackInputMethodDescriptor() {
  std::vector<std::string> layouts;
  layouts.push_back("us");
  std::vector<std::string> languages;
  languages.push_back("en-US");
  return InputMethodDescriptor("xkb:us::eng",
                               "",
                               layouts,
                               languages,
                               GURL());  // options page, not available.
}

void InputMethodUtil::ReloadInternalMaps() {
  if (supported_input_methods_->size() <= 1) {
    DVLOG(1) << "GetSupportedInputMethods returned a fallback ID";
    // TODO(yusukes): Handle this error in nicer way.
  }

  // Clear the existing maps.
  language_code_to_ids_.clear();
  id_to_language_code_.clear();
  id_to_descriptor_.clear();
  xkb_id_to_descriptor_.clear();

  for (size_t i = 0; i < supported_input_methods_->size(); ++i) {
    const InputMethodDescriptor& input_method =
        supported_input_methods_->at(i);
    const std::vector<std::string>& language_codes =
        input_method.language_codes();
    for (size_t i = 0; i < language_codes.size(); ++i) {
      language_code_to_ids_.insert(
          std::make_pair(language_codes[i], input_method.id()));
      // Remember the pairs.
      id_to_language_code_.insert(
          std::make_pair(input_method.id(), language_codes[i]));
    }
    id_to_descriptor_.insert(
        std::make_pair(input_method.id(), input_method));
    if (IsKeyboardLayout(input_method.id())) {
      xkb_id_to_descriptor_.insert(
          std::make_pair(input_method.GetPreferredKeyboardLayout(),
                         input_method));
    }
  }

  // Go through the languages listed in kExtraLanguages.
  for (size_t i = 0; i < kExtraLanguagesLength; ++i) {
    const char* language_code = kExtraLanguages[i].language_code;
    const char* input_method_id = kExtraLanguages[i].input_method_id;
    InputMethodIdToDescriptorMap::const_iterator iter =
        id_to_descriptor_.find(input_method_id);
    // If the associated input method descriptor is found, add the language
    // code and the input method.
    if (iter != id_to_descriptor_.end()) {
      const InputMethodDescriptor& input_method = iter->second;
      language_code_to_ids_.insert(
          std::make_pair(language_code, input_method.id()));
    }
  }
}

}  // namespace input_method
}  // namespace chromeos
