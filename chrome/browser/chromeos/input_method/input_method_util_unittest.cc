// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/input_method/input_method_util.h"

#include <string>

#include "base/bind.h"
#include "base/memory/scoped_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "chromeos/ime/fake_input_method_delegate.h"
#include "chromeos/ime/input_method_manager.h"
#include "chromeos/ime/input_method_whitelist.h"
#include "grit/generated_resources.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/l10n/l10n_util.h"

namespace chromeos {

extern const char* kExtensionImePrefix;

namespace input_method {

namespace {

const char pinyin_ime_id[] =
    "_comp_ime_nmblnjkfdkabgdofidlkienfnnbjhnabzh-t-i0-pinyin";
const char zhuyin_ime_id[] =
    "_comp_ime_goedamlknlnjaengojinmfgpmdjmkooozh-hant-t-i0-und";

class TestableInputMethodUtil : public InputMethodUtil {
 public:
  explicit TestableInputMethodUtil(InputMethodDelegate* delegate,
                                   scoped_ptr<InputMethodDescriptors> methods)
      : InputMethodUtil(delegate, methods.Pass()) {
  }
  // Change access rights.
  using InputMethodUtil::GetInputMethodIdsFromLanguageCodeInternal;
  using InputMethodUtil::GetKeyboardLayoutName;
  using InputMethodUtil::ReloadInternalMaps;
  using InputMethodUtil::supported_input_methods_;
};

}  // namespace

class InputMethodUtilTest : public testing::Test {
 public:
  InputMethodUtilTest()
      : util_(&delegate_, whitelist_.GetSupportedInputMethods()) {
    delegate_.set_get_localized_string_callback(
        base::Bind(&l10n_util::GetStringUTF16));
    delegate_.set_get_display_language_name_callback(
        base::Bind(&InputMethodUtilTest::GetDisplayLanguageName));
  }

  virtual void SetUp() OVERRIDE {
    InputMethodDescriptors input_methods;

    std::vector<std::string> layouts;
    std::vector<std::string> languages;
    layouts.push_back("us");
    languages.push_back("zh-CN");

    InputMethodDescriptor pinyin_ime(pinyin_ime_id,
                                     "Pinyin input for testing",
                                     layouts,
                                     languages,
                                     GURL(""));
    input_methods.push_back(pinyin_ime);

    languages.clear();
    languages.push_back("zh-TW");
    InputMethodDescriptor zhuyin_ime(zhuyin_ime_id,
                                     "Zhuyin input for testing",
                                     layouts,
                                     languages,
                                     GURL(""));
    input_methods.push_back(zhuyin_ime);

    util_.SetComponentExtensions(input_methods);
  }

  InputMethodDescriptor GetDesc(const std::string& id,
                                const std::string& raw_layout,
                                const std::string& language_code) {
    std::vector<std::string> layouts;
    layouts.push_back(raw_layout);
    std::vector<std::string> languages;
    languages.push_back(language_code);
    return InputMethodDescriptor(id,
                                 "",
                                 layouts,
                                 languages,
                                 GURL());  // options page url
  }

  static string16 GetDisplayLanguageName(const std::string& language_code) {
    return l10n_util::GetDisplayNameForLocale(language_code, "en", true);
  }

  FakeInputMethodDelegate delegate_;
  InputMethodWhitelist whitelist_;
  TestableInputMethodUtil util_;
};

TEST_F(InputMethodUtilTest, GetInputMethodShortNameTest) {
  // Test normal cases. Two-letter language code should be returned.
  {
    InputMethodDescriptor desc = GetDesc("m17n:fa:isiri",  // input method id
                                         "us",  // keyboard layout name
                                         "fa");  // language name
    EXPECT_EQ(ASCIIToUTF16("FA"), util_.GetInputMethodShortName(desc));
  }
  {
    InputMethodDescriptor desc = GetDesc("mozc-hangul", "us", "ko");
    EXPECT_EQ(UTF8ToUTF16("\xed\x95\x9c"),
              util_.GetInputMethodShortName(desc));
  }
  {
    InputMethodDescriptor desc = GetDesc("invalid-id", "us", "xx");
    // Upper-case string of the unknown language code, "xx", should be returned.
    EXPECT_EQ(ASCIIToUTF16("XX"), util_.GetInputMethodShortName(desc));
  }

  // Test special cases.
  {
    InputMethodDescriptor desc = GetDesc("xkb:us:dvorak:eng", "us", "en-US");
    EXPECT_EQ(ASCIIToUTF16("DV"), util_.GetInputMethodShortName(desc));
  }
  {
    InputMethodDescriptor desc = GetDesc("xkb:us:colemak:eng", "us", "en-US");
    EXPECT_EQ(ASCIIToUTF16("CO"), util_.GetInputMethodShortName(desc));
  }
  {
    InputMethodDescriptor desc =
        GetDesc("xkb:us:altgr-intl:eng", "us", "en-US");
    EXPECT_EQ(ASCIIToUTF16("EXTD"), util_.GetInputMethodShortName(desc));
  }
  {
    InputMethodDescriptor desc = GetDesc("xkb:us:intl:eng", "us", "en-US");
    EXPECT_EQ(ASCIIToUTF16("INTL"), util_.GetInputMethodShortName(desc));
  }
  {
    InputMethodDescriptor desc = GetDesc("xkb:de:neo:ger", "de(neo)", "de");
    EXPECT_EQ(ASCIIToUTF16("NEO"), util_.GetInputMethodShortName(desc));
  }
  {
    InputMethodDescriptor desc = GetDesc("xkb:es:cat:cat", "es(cat)", "ca");
    EXPECT_EQ(ASCIIToUTF16("CAS"), util_.GetInputMethodShortName(desc));
  }
  {
    InputMethodDescriptor desc = GetDesc(pinyin_ime_id, "us", "zh-CN");
    EXPECT_EQ(UTF8ToUTF16("\xe6\x8b\xbc"),
              util_.GetInputMethodShortName(desc));
  }
  {
    InputMethodDescriptor desc = GetDesc(zhuyin_ime_id, "us", "zh-TW");
    EXPECT_EQ(UTF8ToUTF16("\xe9\x85\xb7"),
              util_.GetInputMethodShortName(desc));
  }
  {
    InputMethodDescriptor desc = GetDesc("m17n:zh:cangjie", "us", "zh-TW");
    EXPECT_EQ(UTF8ToUTF16("\xe5\x80\x89"),
              util_.GetInputMethodShortName(desc));
  }
  {
    InputMethodDescriptor desc = GetDesc("m17n:zh:quick", "us", "zh-TW");
    EXPECT_EQ(UTF8ToUTF16("\xe9\x80\x9f"),
              util_.GetInputMethodShortName(desc));
  }
}

TEST_F(InputMethodUtilTest, GetInputMethodMediumNameTest) {
  {
    // input methods with medium name equal to short name
    const char * input_method_id[] = {
      "xkb:us:altgr-intl:eng",
      "xkb:us:dvorak:eng",
      "xkb:us:intl:eng",
      "xkb:us:colemak:eng",
      "english-m",
      "xkb:de:neo:ger",
      "xkb:es:cat:cat",
      "xkb:gb:dvorak:eng",
    };
    const int len = ARRAYSIZE_UNSAFE(input_method_id);
    for (int i=0; i<len; ++i) {
      InputMethodDescriptor desc = GetDesc(input_method_id[i], "", "");
      string16 medium_name = util_.GetInputMethodMediumName(desc);
      string16 short_name = util_.GetInputMethodShortName(desc);
      EXPECT_EQ(medium_name,short_name);
    }
  }
  {
    // input methods with medium name not equal to short name
    const char * input_method_id[] = {
      "m17n:zh:cangjie",
      "m17n:zh:quick",
      pinyin_ime_id,
      zhuyin_ime_id,
      "mozc-hangul",
      pinyin_ime_id,
      pinyin_ime_id,
    };
    const int len = ARRAYSIZE_UNSAFE(input_method_id);
    for (int i=0; i<len; ++i) {
      InputMethodDescriptor desc = GetDesc(input_method_id[i], "", "");
      string16 medium_name = util_.GetInputMethodMediumName(desc);
      string16 short_name = util_.GetInputMethodShortName(desc);
      EXPECT_NE(medium_name,short_name);
    }
  }
}

TEST_F(InputMethodUtilTest, GetInputMethodLongNameTest) {
  // For most languages input method or keyboard layout name is returned.
  // See below for exceptions.
  {
    InputMethodDescriptor desc = GetDesc("m17n:fa:isiri", "us", "fa");
    EXPECT_EQ(ASCIIToUTF16("Persian input method (ISIRI 2901 layout)"),
              util_.GetInputMethodLongName(desc));
  }
  {
    InputMethodDescriptor desc = GetDesc("mozc-hangul", "us", "ko");
    EXPECT_EQ(ASCIIToUTF16("Korean input method"),
              util_.GetInputMethodLongName(desc));
  }
  {
    InputMethodDescriptor desc = GetDesc("m17n:vi:tcvn", "us", "vi");
    EXPECT_EQ(ASCIIToUTF16("Vietnamese input method (TCVN6064)"),
              util_.GetInputMethodLongName(desc));
  }
  {
    InputMethodDescriptor desc = GetDesc("xkb:jp::jpn", "jp", "ja");
    EXPECT_EQ(ASCIIToUTF16("Japanese keyboard"),
              util_.GetInputMethodLongName(desc));
  }
  {
    InputMethodDescriptor desc =
        GetDesc("xkb:us:dvorak:eng", "us(dvorak)", "en-US");
    EXPECT_EQ(ASCIIToUTF16("US Dvorak keyboard"),
              util_.GetInputMethodLongName(desc));
  }
  {
    InputMethodDescriptor desc =
        GetDesc("xkb:gb:dvorak:eng", "gb(dvorak)", "en-US");
    EXPECT_EQ(ASCIIToUTF16("UK Dvorak keyboard"),
              util_.GetInputMethodLongName(desc));
  }

  // For Arabic, Dutch, French, German and Hindi,
  // "language - keyboard layout" pair is returned.
  {
    InputMethodDescriptor desc = GetDesc("m17n:ar:kbd", "us", "ar");
    EXPECT_EQ(ASCIIToUTF16("Arabic - Standard input method"),
              util_.GetInputMethodLongName(desc));
  }
  {
    InputMethodDescriptor desc = GetDesc("xkb:be::nld", "be", "nl");
    EXPECT_EQ(ASCIIToUTF16("Dutch - Belgian keyboard"),
              util_.GetInputMethodLongName(desc));
  }
  {
    InputMethodDescriptor desc = GetDesc("xkb:fr::fra", "fr", "fr");
    EXPECT_EQ(ASCIIToUTF16("French - French keyboard"),
              util_.GetInputMethodLongName(desc));
  }
  {
    InputMethodDescriptor desc = GetDesc("xkb:be::fra", "be", "fr");
    EXPECT_EQ(ASCIIToUTF16("French - Belgian keyboard"),
              util_.GetInputMethodLongName(desc));
  }
  {
    InputMethodDescriptor desc = GetDesc("xkb:de::ger", "de", "de");
    EXPECT_EQ(ASCIIToUTF16("German - German keyboard"),
              util_.GetInputMethodLongName(desc));
  }
  {
    InputMethodDescriptor desc = GetDesc("xkb:be::ger", "be", "de");
    EXPECT_EQ(ASCIIToUTF16("German - Belgian keyboard"),
              util_.GetInputMethodLongName(desc));
  }
  {
    InputMethodDescriptor desc = GetDesc("m17n:hi:itrans", "us", "hi");
    EXPECT_EQ(ASCIIToUTF16("Hindi - Standard input method"),
              util_.GetInputMethodLongName(desc));
  }

  {
    InputMethodDescriptor desc = GetDesc("invalid-id", "us", "xx");
    // You can safely ignore the "Resouce ID is not found for: invalid-id"
    // error.
    EXPECT_EQ(ASCIIToUTF16("invalid-id"),
              util_.GetInputMethodLongName(desc));
  }
}

TEST_F(InputMethodUtilTest, TestIsValidInputMethodId) {
  EXPECT_TRUE(util_.IsValidInputMethodId("xkb:us:colemak:eng"));
  EXPECT_TRUE(util_.IsValidInputMethodId(pinyin_ime_id));
  EXPECT_FALSE(util_.IsValidInputMethodId("unsupported-input-method"));
}

TEST_F(InputMethodUtilTest, TestIsKeyboardLayout) {
  EXPECT_TRUE(InputMethodUtil::IsKeyboardLayout("xkb:us::eng"));
  EXPECT_FALSE(InputMethodUtil::IsKeyboardLayout(pinyin_ime_id));
}

TEST_F(InputMethodUtilTest, TestGetKeyboardLayoutName) {
  // Unsupported case.
  EXPECT_EQ("", util_.GetKeyboardLayoutName("UNSUPPORTED_ID"));

  // Supported cases (samples).
  EXPECT_EQ("us", util_.GetKeyboardLayoutName(pinyin_ime_id));
  EXPECT_EQ("es", util_.GetKeyboardLayoutName("xkb:es::spa"));
  EXPECT_EQ("es(cat)", util_.GetKeyboardLayoutName("xkb:es:cat:cat"));
  EXPECT_EQ("gb(extd)", util_.GetKeyboardLayoutName("xkb:gb:extd:eng"));
  EXPECT_EQ("us", util_.GetKeyboardLayoutName("xkb:us::eng"));
  EXPECT_EQ("us(dvorak)", util_.GetKeyboardLayoutName("xkb:us:dvorak:eng"));
  EXPECT_EQ("us(colemak)", util_.GetKeyboardLayoutName("xkb:us:colemak:eng"));
  EXPECT_EQ("de(neo)", util_.GetKeyboardLayoutName("xkb:de:neo:ger"));
}

TEST_F(InputMethodUtilTest, TestGetLanguageCodeFromInputMethodId) {
  // Make sure that the -CN is added properly.
  EXPECT_EQ("zh-CN", util_.GetLanguageCodeFromInputMethodId(pinyin_ime_id));
}

TEST_F(InputMethodUtilTest, TestGetInputMethodDisplayNameFromId) {
  EXPECT_EQ("US keyboard",
            util_.GetInputMethodDisplayNameFromId("xkb:us::eng"));
  EXPECT_EQ("", util_.GetInputMethodDisplayNameFromId("nonexistent"));
}

TEST_F(InputMethodUtilTest, TestGetInputMethodDescriptorFromId) {
  EXPECT_EQ(NULL, util_.GetInputMethodDescriptorFromId("non_existent"));

  const InputMethodDescriptor* descriptor =
      util_.GetInputMethodDescriptorFromId(pinyin_ime_id);
  ASSERT_TRUE(NULL != descriptor);  // ASSERT_NE doesn't compile.
  EXPECT_EQ(pinyin_ime_id, descriptor->id());
  EXPECT_EQ("us", descriptor->GetPreferredKeyboardLayout());
  // This used to be "zh" but now we have "zh-CN" in input_methods.h,
  // hence this should be zh-CN now.
  ASSERT_TRUE(!descriptor->language_codes().empty());
  EXPECT_EQ("zh-CN", descriptor->language_codes().at(0));
}

TEST_F(InputMethodUtilTest, TestGetInputMethodIdsForLanguageCode) {
  std::multimap<std::string, std::string> language_code_to_ids_map;
  language_code_to_ids_map.insert(std::make_pair("ja", pinyin_ime_id));
  language_code_to_ids_map.insert(std::make_pair("ja", pinyin_ime_id));
  language_code_to_ids_map.insert(std::make_pair("ja", "xkb:jp:jpn"));
  language_code_to_ids_map.insert(std::make_pair("fr", "xkb:fr:fra"));

  std::vector<std::string> result;
  EXPECT_TRUE(util_.GetInputMethodIdsFromLanguageCodeInternal(
      language_code_to_ids_map, "ja", kAllInputMethods, &result));
  EXPECT_EQ(3U, result.size());
  EXPECT_TRUE(util_.GetInputMethodIdsFromLanguageCodeInternal(
      language_code_to_ids_map, "ja", kKeyboardLayoutsOnly, &result));
  ASSERT_EQ(1U, result.size());
  EXPECT_EQ("xkb:jp:jpn", result[0]);

  EXPECT_TRUE(util_.GetInputMethodIdsFromLanguageCodeInternal(
      language_code_to_ids_map, "fr", kAllInputMethods, &result));
  ASSERT_EQ(1U, result.size());
  EXPECT_EQ("xkb:fr:fra", result[0]);
  EXPECT_TRUE(util_.GetInputMethodIdsFromLanguageCodeInternal(
      language_code_to_ids_map, "fr", kKeyboardLayoutsOnly, &result));
  ASSERT_EQ(1U, result.size());
  EXPECT_EQ("xkb:fr:fra", result[0]);

  EXPECT_FALSE(util_.GetInputMethodIdsFromLanguageCodeInternal(
      language_code_to_ids_map, "invalid_lang", kAllInputMethods, &result));
  EXPECT_FALSE(util_.GetInputMethodIdsFromLanguageCodeInternal(
      language_code_to_ids_map, "invalid_lang", kKeyboardLayoutsOnly, &result));
}

// US keyboard + English US UI = US keyboard only.
TEST_F(InputMethodUtilTest, TestGetFirstLoginInputMethodIds_Us_And_EnUs) {
  const InputMethodDescriptor* descriptor =
      util_.GetInputMethodDescriptorFromId("xkb:us::eng");  // US keyboard.
  ASSERT_TRUE(NULL != descriptor);  // ASSERT_NE doesn't compile.
  std::vector<std::string> input_method_ids;
  util_.GetFirstLoginInputMethodIds("en-US", *descriptor, &input_method_ids);
  ASSERT_EQ(1U, input_method_ids.size());
  EXPECT_EQ("xkb:us::eng", input_method_ids[0]);
}

// US keyboard + Chinese UI = US keyboard + Pinyin IME.
TEST_F(InputMethodUtilTest, TestGetFirstLoginInputMethodIds_Us_And_Zh) {
  const InputMethodDescriptor* descriptor =
      util_.GetInputMethodDescriptorFromId("xkb:us::eng");  // US keyboard.
  ASSERT_TRUE(NULL != descriptor);  // ASSERT_NE doesn't compile.
  std::vector<std::string> input_method_ids;
  util_.GetFirstLoginInputMethodIds("zh-CN", *descriptor, &input_method_ids);
  ASSERT_EQ(2U, input_method_ids.size());
  EXPECT_EQ("xkb:us::eng", input_method_ids[0]);
  EXPECT_EQ(pinyin_ime_id, input_method_ids[1]);  // Pinyin for US keybaord.
}

// US keyboard + Russian UI = US keyboard + Russsian keyboard
TEST_F(InputMethodUtilTest, TestGetFirstLoginInputMethodIds_Us_And_Ru) {
  const InputMethodDescriptor* descriptor =
      util_.GetInputMethodDescriptorFromId("xkb:us::eng");  // US keyboard.
  ASSERT_TRUE(NULL != descriptor);  // ASSERT_NE doesn't compile.
  std::vector<std::string> input_method_ids;
  util_.GetFirstLoginInputMethodIds("ru", *descriptor, &input_method_ids);
  ASSERT_EQ(2U, input_method_ids.size());
  EXPECT_EQ("xkb:us::eng", input_method_ids[0]);
  EXPECT_EQ("xkb:ru::rus", input_method_ids[1]);  // Russian keyboard.
}

// US keyboard + Traditional Chinese = US keyboard + chewing.
TEST_F(InputMethodUtilTest, TestGetFirstLoginInputMethodIds_Us_And_ZhTw) {
  const InputMethodDescriptor* descriptor =
      util_.GetInputMethodDescriptorFromId("xkb:us::eng");  // US keyboard.
  ASSERT_TRUE(NULL != descriptor);  // ASSERT_NE doesn't compile.
  std::vector<std::string> input_method_ids;
  util_.GetFirstLoginInputMethodIds("zh-TW", *descriptor, &input_method_ids);
  ASSERT_EQ(2U, input_method_ids.size());
  EXPECT_EQ("xkb:us::eng", input_method_ids[0]);
  EXPECT_EQ(zhuyin_ime_id, input_method_ids[1]);  // Chewing.
}

// US keyboard + Thai = US keyboard + kesmanee.
TEST_F(InputMethodUtilTest, TestGetFirstLoginInputMethodIds_Us_And_Th) {
  const InputMethodDescriptor* descriptor =
      util_.GetInputMethodDescriptorFromId("xkb:us::eng");  // US keyboard.
  ASSERT_TRUE(NULL != descriptor);  // ASSERT_NE doesn't compile.
  std::vector<std::string> input_method_ids;
  util_.GetFirstLoginInputMethodIds("th", *descriptor, &input_method_ids);
  ASSERT_EQ(2U, input_method_ids.size());
  EXPECT_EQ("xkb:us::eng", input_method_ids[0]);
  EXPECT_EQ("_comp_ime_jhffeifommiaekmbkkjlpmilogcfdohpvkd_th",
            input_method_ids[1]);  // Kesmanee.
}

// US keyboard + Vietnamese = US keyboard + TCVN6064.
TEST_F(InputMethodUtilTest, TestGetFirstLoginInputMethodIds_Us_And_Vi) {
  const InputMethodDescriptor* descriptor =
      util_.GetInputMethodDescriptorFromId("xkb:us::eng");  // US keyboard.
  ASSERT_TRUE(NULL != descriptor);  // ASSERT_NE doesn't compile.
  std::vector<std::string> input_method_ids;
  util_.GetFirstLoginInputMethodIds("vi", *descriptor, &input_method_ids);
  ASSERT_EQ(2U, input_method_ids.size());
  EXPECT_EQ("xkb:us::eng", input_method_ids[0]);
  EXPECT_EQ("_comp_ime_jhffeifommiaekmbkkjlpmilogcfdohpvkd_vi_tcvn",
            input_method_ids[1]);  // TCVN6064.
}

TEST_F(InputMethodUtilTest, TestGetLanguageCodesFromInputMethodIds) {
  std::vector<std::string> input_method_ids;
  input_method_ids.push_back("xkb:us::eng");  // English US.
  input_method_ids.push_back("xkb:us:dvorak:eng");  // English US Dvorak.
  input_method_ids.push_back(pinyin_ime_id);  // Pinyin
  input_method_ids.push_back("xkb:fr::fra");  // French France.
  std::vector<std::string> language_codes;
  util_.GetLanguageCodesFromInputMethodIds(input_method_ids, &language_codes);
  ASSERT_EQ(3U, language_codes.size());
  EXPECT_EQ("en-US", language_codes[0]);
  EXPECT_EQ("zh-CN", language_codes[1]);
  EXPECT_EQ("fr", language_codes[2]);
}

// Test all supported descriptors to detect a typo in ibus_input_methods.txt.
TEST_F(InputMethodUtilTest, TestIBusInputMethodText) {
  for (size_t i = 0; i < util_.supported_input_methods_->size(); ++i) {
    const std::string language_code =
        util_.supported_input_methods_->at(i).language_codes().at(0);
    const string16 display_name =
        l10n_util::GetDisplayNameForLocale(language_code, "en", false);
    // Only two formats, like "fr" (lower case) and "en-US" (lower-upper), are
    // allowed. See the text file for details.
    EXPECT_TRUE(language_code.length() == 2 ||
                (language_code.length() == 5 && language_code[2] == '-'))
        << "Invalid language code " << language_code;
    EXPECT_TRUE(l10n_util::IsValidLocaleSyntax(language_code))
        << "Invalid language code " << language_code;
    EXPECT_FALSE(display_name.empty())
        << "Invalid language code " << language_code;
    // On error, GetDisplayNameForLocale() returns the |language_code| as-is.
    EXPECT_NE(language_code, UTF16ToUTF8(display_name))
        << "Invalid language code " << language_code;
  }
}

}  // namespace input_method
}  // namespace chromeos
