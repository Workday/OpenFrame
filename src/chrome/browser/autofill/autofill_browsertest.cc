// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/basictypes.h"
#include "base/command_line.h"
#include "base/file_util.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/rand_util.h"
#include "base/strings/string16.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/autofill/personal_data_manager_factory.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/infobars/confirm_infobar_delegate.h"
#include "chrome/browser/infobars/infobar_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/render_messages.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/test_switches.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/autofill/content/browser/autofill_driver_impl.h"
#include "components/autofill/core/browser/autofill_common_test.h"
#include "components/autofill/core/browser/autofill_profile.h"
#include "components/autofill/core/browser/credit_card.h"
#include "components/autofill/core/browser/personal_data_manager.h"
#include "components/autofill/core/browser/personal_data_manager_observer.h"
#include "components/autofill/core/browser/validation.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/test_utils.h"
#include "net/url_request/test_url_fetcher_factory.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/keycodes/keyboard_codes.h"

namespace autofill {

class WindowedPersonalDataManagerObserver
    : public PersonalDataManagerObserver,
      public content::NotificationObserver {
 public:
  explicit WindowedPersonalDataManagerObserver(Browser* browser)
      : alerted_(false),
        has_run_message_loop_(false),
        browser_(browser),
        infobar_service_(NULL) {
    PersonalDataManagerFactory::GetForProfile(browser_->profile())->
        AddObserver(this);
    registrar_.Add(this, chrome::NOTIFICATION_TAB_CONTENTS_INFOBAR_ADDED,
                   content::NotificationService::AllSources());
  }

  virtual ~WindowedPersonalDataManagerObserver() {
    if (infobar_service_ && (infobar_service_->infobar_count() > 0))
      infobar_service_->RemoveInfoBar(infobar_service_->infobar_at(0));
  }

  void Wait() {
    if (!alerted_) {
      has_run_message_loop_ = true;
      content::RunMessageLoop();
    }
    PersonalDataManagerFactory::GetForProfile(browser_->profile())->
        RemoveObserver(this);
  }

  // PersonalDataManagerObserver:
  virtual void OnPersonalDataChanged() OVERRIDE {
    if (has_run_message_loop_) {
      base::MessageLoopForUI::current()->Quit();
      has_run_message_loop_ = false;
    }
    alerted_ = true;
  }

  virtual void OnInsufficientFormData() OVERRIDE {
    OnPersonalDataChanged();
  }

  // content::NotificationObserver:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE {
    EXPECT_EQ(chrome::NOTIFICATION_TAB_CONTENTS_INFOBAR_ADDED, type);
    infobar_service_ = InfoBarService::FromWebContents(
        browser_->tab_strip_model()->GetActiveWebContents());
    ConfirmInfoBarDelegate* infobar_delegate =
        infobar_service_->infobar_at(0)->AsConfirmInfoBarDelegate();
    ASSERT_TRUE(infobar_delegate);
    infobar_delegate->Accept();
  }

 private:
  bool alerted_;
  bool has_run_message_loop_;
  Browser* browser_;
  content::NotificationRegistrar registrar_;
  InfoBarService* infobar_service_;
};

class AutofillTest : public InProcessBrowserTest {
 protected:
  AutofillTest() {}

  virtual void SetUpOnMainThread() OVERRIDE {
    // Don't want Keychain coming up on Mac.
    test::DisableSystemServices(browser()->profile());
  }

  virtual void CleanUpOnMainThread() OVERRIDE {
    // Make sure to close any showing popups prior to tearing down the UI.
    content::WebContents* web_contents =
        browser()->tab_strip_model()->GetActiveWebContents();
    AutofillManager* autofill_manager =
        AutofillDriverImpl::FromWebContents(web_contents)->autofill_manager();
    autofill_manager->delegate()->HideAutofillPopup();
  }

  PersonalDataManager* personal_data_manager() {
    return PersonalDataManagerFactory::GetForProfile(browser()->profile());
  }

  void SetProfiles(std::vector<AutofillProfile>* profiles) {
    WindowedPersonalDataManagerObserver observer(browser());
    personal_data_manager()->SetProfiles(profiles);
    observer.Wait();
  }

  void SetProfile(const AutofillProfile& profile) {
    std::vector<AutofillProfile> profiles;
    profiles.push_back(profile);
    SetProfiles(&profiles);
  }

  void SetCards(std::vector<CreditCard>* cards) {
    WindowedPersonalDataManagerObserver observer(browser());
    personal_data_manager()->SetCreditCards(cards);
    observer.Wait();
  }

  void SetCard(const CreditCard& card) {
    std::vector<CreditCard> cards;
    cards.push_back(card);
    SetCards(&cards);
  }

  typedef std::map<std::string, std::string> FormMap;
  // Navigate to the form, input values into the fields, and submit the form.
  // The function returns after the PersonalDataManager is updated.
  void FillFormAndSubmit(const std::string& filename, const FormMap& data) {
    GURL url = test_server()->GetURL("files/autofill/" + filename);
    ui_test_utils::NavigateToURL(browser(), url);

    std::string js;
    for (FormMap::const_iterator i = data.begin(); i != data.end(); ++i) {
      js += "document.getElementById('" + i->first + "').value = '" +
            i->second + "';";
    }
    js += "document.getElementById('testform').submit();";

    WindowedPersonalDataManagerObserver observer(browser());
    ASSERT_TRUE(content::ExecuteScript(render_view_host(), js));
    observer.Wait();
  }

  void SubmitCreditCard(const char* name,
                        const char* number,
                        const char* exp_month,
                        const char* exp_year) {
    FormMap data;
    data["CREDIT_CARD_NAME"] = name;
    data["CREDIT_CARD_NUMBER"] = number;
    data["CREDIT_CARD_EXP_MONTH"] = exp_month;
    data["CREDIT_CARD_EXP_4_DIGIT_YEAR"] = exp_year;
    FillFormAndSubmit("autofill_creditcard_form.html", data);
  }

  // Aggregate profiles from forms into Autofill preferences. Returns the number
  // of parsed profiles.
  int AggregateProfilesIntoAutofillPrefs(const std::string& filename) {
    CHECK(test_server()->Start());

    std::string data;
    base::FilePath data_file =
        ui_test_utils::GetTestFilePath(base::FilePath().AppendASCII("autofill"),
                                       base::FilePath().AppendASCII(filename));
    CHECK(file_util::ReadFileToString(data_file, &data));
    std::vector<std::string> lines;
    base::SplitString(data, '\n', &lines);
    for (size_t i = 0; i < lines.size(); ++i) {
      if (StartsWithASCII(lines[i], "#", false))
        continue;
      std::vector<std::string> fields;
      base::SplitString(lines[i], '|', &fields);
      if (fields.empty())
        continue;  // Blank line.
      CHECK_EQ(12u, fields.size());

      FormMap data;
      data["NAME_FIRST"] = fields[0];
      data["NAME_MIDDLE"] = fields[1];
      data["NAME_LAST"] = fields[2];
      data["EMAIL_ADDRESS"] = fields[3];
      data["COMPANY_NAME"] = fields[4];
      data["ADDRESS_HOME_LINE1"] = fields[5];
      data["ADDRESS_HOME_LINE2"] = fields[6];
      data["ADDRESS_HOME_CITY"] = fields[7];
      data["ADDRESS_HOME_STATE"] = fields[8];
      data["ADDRESS_HOME_ZIP"] = fields[9];
      data["ADDRESS_HOME_COUNTRY"] = fields[10];
      data["PHONE_HOME_WHOLE_NUMBER"] = fields[11];

      FillFormAndSubmit("duplicate_profiles_test.html", data);
    }
    return lines.size();
  }

  void ExpectFieldValue(const std::string& field_name,
                        const std::string& expected_value) {
    std::string value;
    ASSERT_TRUE(content::ExecuteScriptAndExtractString(
        browser()->tab_strip_model()->GetActiveWebContents(),
        "window.domAutomationController.send("
        "    document.getElementById('" + field_name + "').value);",
        &value));
    EXPECT_EQ(expected_value, value);
  }

  content::RenderViewHost* render_view_host() {
    return browser()->tab_strip_model()->GetActiveWebContents()->
        GetRenderViewHost();
  }

  void ExpectFilledTestForm() {
    ExpectFieldValue("firstname", "Milton");
    ExpectFieldValue("lastname", "Waddams");
    ExpectFieldValue("address1", "4120 Freidrich Lane");
    ExpectFieldValue("address2", "Basement");
    ExpectFieldValue("city", "Austin");
    ExpectFieldValue("state", "TX");
    ExpectFieldValue("zip", "78744");
    ExpectFieldValue("country", "US");
    ExpectFieldValue("phone", "5125551234");
  }

 private:
  net::TestURLFetcherFactory url_fetcher_factory_;
};

// Test filling profiles with unicode strings and crazy characters.
// TODO(isherman): rewrite as unit test under PersonalDataManagerTest.
IN_PROC_BROWSER_TEST_F(AutofillTest, FillProfileCrazyCharacters) {
  std::vector<AutofillProfile> profiles;
  AutofillProfile profile1;
  profile1.SetRawInfo(NAME_FIRST,
                      WideToUTF16(L"\u0623\u0648\u0628\u0627\u0645\u0627 "
                                  L"\u064a\u0639\u062a\u0630\u0631 "
                                  L"\u0647\u0627\u062a\u0641\u064a\u0627 "
                                  L"\u0644\u0645\u0648\u0638\u0641\u0629 "
                                  L"\u0633\u0648\u062f\u0627\u0621 "
                                  L"\u0627\u0633\u062a\u0642\u0627\u0644\u062a "
                                  L"\u0628\u0633\u0628\u0628 "
                                  L"\u062a\u0635\u0631\u064a\u062d\u0627\u062a "
                                  L"\u0645\u062c\u062a\u0632\u0623\u0629"));
  profile1.SetRawInfo(NAME_MIDDLE, WideToUTF16(L"BANK\xcBERF\xc4LLE"));
  profile1.SetRawInfo(EMAIL_ADDRESS,
                      WideToUTF16(L"\uacbd\uc81c \ub274\uc2a4 "
                                  L"\ub354\ubcf4\uae30@google.com"));
  profile1.SetRawInfo(ADDRESS_HOME_LINE1,
                      WideToUTF16(L"\uad6d\uc815\uc6d0\xb7\uac80\ucc30, "
                                  L"\ub178\ubb34\ud604\uc815\ubd80 "
                                  L"\ub300\ubd81\uc811\ucd09 \ub2f4\ub2f9 "
                                  L"\uc778\uc0ac\ub4e4 \uc870\uc0ac"));
  profile1.SetRawInfo(ADDRESS_HOME_CITY,
                      WideToUTF16(L"\u653f\u5e9c\u4e0d\u6392\u9664\u7acb\u6cd5"
                                  L"\u898f\u7ba1\u5c0e\u904a"));
  profile1.SetRawInfo(ADDRESS_HOME_ZIP, WideToUTF16(L"YOHO_54676"));
  profile1.SetRawInfo(PHONE_HOME_WHOLE_NUMBER, WideToUTF16(L"861088828000"));
  profile1.SetInfo(
      AutofillType(ADDRESS_HOME_COUNTRY), WideToUTF16(L"India"), "en-US");
  profiles.push_back(profile1);

  AutofillProfile profile2;
  profile2.SetRawInfo(NAME_FIRST,
                      WideToUTF16(L"\u4e0a\u6d77\u5e02\u91d1\u5c71\u533a "
                                  L"\u677e\u9690\u9547\u4ead\u67ab\u516c"
                                  L"\u8def1915\u53f7"));
  profile2.SetRawInfo(NAME_LAST, WideToUTF16(L"aguantó"));
  profile2.SetRawInfo(ADDRESS_HOME_ZIP, WideToUTF16(L"HOME 94043"));
  profiles.push_back(profile2);

  AutofillProfile profile3;
  profile3.SetRawInfo(EMAIL_ADDRESS, WideToUTF16(L"sue@example.com"));
  profile3.SetRawInfo(COMPANY_NAME, WideToUTF16(L"Company X"));
  profiles.push_back(profile3);

  AutofillProfile profile4;
  profile4.SetRawInfo(NAME_FIRST, WideToUTF16(L"Joe 3254"));
  profile4.SetRawInfo(NAME_LAST, WideToUTF16(L"\u8bb0\u8d262\u5e74\u591a"));
  profile4.SetRawInfo(ADDRESS_HOME_ZIP,
                      WideToUTF16(L"\uff08\u90ae\u7f16\uff1a201504\uff09"));
  profile4.SetRawInfo(EMAIL_ADDRESS, WideToUTF16(L"télévision@example.com"));
  profile4.SetRawInfo(COMPANY_NAME,
                      WideToUTF16(L"\u0907\u0932\u0947\u0915\u093f\u091f\u094d"
                                  L"\u0930\u0928\u093f\u0915\u094d\u0938, "
                                  L"\u0905\u092a\u094b\u0932\u094b "
                                  L"\u091f\u093e\u092f\u0930\u094d\u0938 "
                                  L"\u0906\u0926\u093f"));
  profiles.push_back(profile4);

  AutofillProfile profile5;
  profile5.SetRawInfo(NAME_FIRST, WideToUTF16(L"Larry"));
  profile5.SetRawInfo(NAME_LAST,
                      WideToUTF16(L"\u0938\u094d\u091f\u093e\u0902\u092a "
                                  L"\u0921\u094d\u092f\u0942\u091f\u0940"));
  profile5.SetRawInfo(ADDRESS_HOME_ZIP,
                      WideToUTF16(L"111111111111110000GOOGLE"));
  profile5.SetRawInfo(EMAIL_ADDRESS, WideToUTF16(L"page@000000.com"));
  profile5.SetRawInfo(COMPANY_NAME, WideToUTF16(L"Google"));
  profiles.push_back(profile5);

  AutofillProfile profile6;
  profile6.SetRawInfo(NAME_FIRST,
                      WideToUTF16(L"\u4e0a\u6d77\u5e02\u91d1\u5c71\u533a "
                                  L"\u677e\u9690\u9547\u4ead\u67ab\u516c"
                                  L"\u8def1915\u53f7"));
  profile6.SetRawInfo(NAME_LAST,
                      WideToUTF16(L"\u0646\u062c\u0627\u0645\u064a\u0646\u0627 "
                                  L"\u062f\u0639\u0645\u0647\u0627 "
                                  L"\u0644\u0644\u0631\u0626\u064a\u0633 "
                                  L"\u0627\u0644\u0633\u0648\u062f\u0627\u0646"
                                  L"\u064a \u0639\u0645\u0631 "
                                  L"\u0627\u0644\u0628\u0634\u064a\u0631"));
  profile6.SetRawInfo(ADDRESS_HOME_ZIP, WideToUTF16(L"HOME 94043"));
  profiles.push_back(profile6);

  AutofillProfile profile7;
  profile7.SetRawInfo(NAME_FIRST, WideToUTF16(L"&$%$$$ TESTO *&*&^&^& MOKO"));
  profile7.SetRawInfo(NAME_MIDDLE, WideToUTF16(L"WOHOOOO$$$$$$$$****"));
  profile7.SetRawInfo(EMAIL_ADDRESS, WideToUTF16(L"yuvu@example.com"));
  profile7.SetRawInfo(ADDRESS_HOME_LINE1,
                      WideToUTF16(L"34544, anderson ST.(120230)"));
  profile7.SetRawInfo(ADDRESS_HOME_CITY, WideToUTF16(L"Sunnyvale"));
  profile7.SetRawInfo(ADDRESS_HOME_STATE, WideToUTF16(L"CA"));
  profile7.SetRawInfo(ADDRESS_HOME_ZIP, WideToUTF16(L"94086"));
  profile7.SetRawInfo(PHONE_HOME_WHOLE_NUMBER, WideToUTF16(L"15466784565"));
  profile7.SetInfo(
      AutofillType(ADDRESS_HOME_COUNTRY), WideToUTF16(L"United States"),
      "en-US");
  profiles.push_back(profile7);

  SetProfiles(&profiles);
  ASSERT_EQ(profiles.size(), personal_data_manager()->GetProfiles().size());
  for (size_t i = 0; i < profiles.size(); ++i)
    ASSERT_EQ(profiles[i], *personal_data_manager()->GetProfiles()[i]);

  std::vector<CreditCard> cards;
  CreditCard card1;
  card1.SetRawInfo(CREDIT_CARD_NAME,
                   WideToUTF16(L"\u751f\u6d3b\u5f88\u6709\u89c4\u5f8b "
                               L"\u4ee5\u73a9\u4e3a\u4e3b"));
  card1.SetRawInfo(CREDIT_CARD_NUMBER, WideToUTF16(L"6011111111111117"));
  card1.SetRawInfo(CREDIT_CARD_EXP_MONTH, WideToUTF16(L"12"));
  card1.SetRawInfo(CREDIT_CARD_EXP_4_DIGIT_YEAR, WideToUTF16(L"2011"));
  cards.push_back(card1);

  CreditCard card2;
  card2.SetRawInfo(CREDIT_CARD_NAME, WideToUTF16(L"John Williams"));
  card2.SetRawInfo(CREDIT_CARD_NUMBER, WideToUTF16(L"WokoAwesome12345"));
  card2.SetRawInfo(CREDIT_CARD_EXP_MONTH, WideToUTF16(L"10"));
  card2.SetRawInfo(CREDIT_CARD_EXP_4_DIGIT_YEAR, WideToUTF16(L"2015"));
  cards.push_back(card2);

  CreditCard card3;
  card3.SetRawInfo(CREDIT_CARD_NAME,
                   WideToUTF16(L"\u0623\u062d\u0645\u062f\u064a "
                               L"\u0646\u062c\u0627\u062f "
                               L"\u0644\u0645\u062d\u0627\u0648\u0644\u0647 "
                               L"\u0627\u063a\u062a\u064a\u0627\u0644 "
                               L"\u0641\u064a \u0645\u062f\u064a\u0646\u0629 "
                               L"\u0647\u0645\u062f\u0627\u0646 "));
  card3.SetRawInfo(CREDIT_CARD_NUMBER,
                   WideToUTF16(L"\u092a\u0941\u0928\u0930\u094d\u091c\u0940"
                               L"\u0935\u093f\u0924 \u0939\u094b\u0917\u093e "
                               L"\u0928\u093e\u0932\u0902\u0926\u093e"));
  card3.SetRawInfo(CREDIT_CARD_EXP_MONTH, WideToUTF16(L"10"));
  card3.SetRawInfo(CREDIT_CARD_EXP_4_DIGIT_YEAR, WideToUTF16(L"2015"));
  cards.push_back(card3);

  CreditCard card4;
  card4.SetRawInfo(CREDIT_CARD_NAME,
                   WideToUTF16(L"\u039d\u03ad\u03b5\u03c2 "
                               L"\u03c3\u03c5\u03b3\u03c7\u03c9\u03bd\u03b5"
                               L"\u03cd\u03c3\u03b5\u03b9\u03c2 "
                               L"\u03ba\u03b1\u03b9 "
                               L"\u03ba\u03b1\u03c4\u03b1\u03c1\u03b3\u03ae"
                               L"\u03c3\u03b5\u03b9\u03c2"));
  card4.SetRawInfo(CREDIT_CARD_NUMBER, WideToUTF16(L"00000000000000000000000"));
  card4.SetRawInfo(CREDIT_CARD_EXP_MONTH, WideToUTF16(L"01"));
  card4.SetRawInfo(CREDIT_CARD_EXP_4_DIGIT_YEAR, WideToUTF16(L"2016"));
  cards.push_back(card4);

  SetCards(&cards);
  ASSERT_EQ(cards.size(), personal_data_manager()->GetCreditCards().size());
  for (size_t i = 0; i < cards.size(); ++i)
    ASSERT_EQ(cards[i], *personal_data_manager()->GetCreditCards()[i]);
}

// Test filling in invalid values for profiles are saved as-is. Phone
// information entered into the prefs UI is not validated or rejected except for
// duplicates.
// TODO(isherman): rewrite as WebUI test?
IN_PROC_BROWSER_TEST_F(AutofillTest, Invalid) {
  // First try profiles with invalid ZIP input.
  AutofillProfile without_invalid;
  without_invalid.SetRawInfo(NAME_FIRST, ASCIIToUTF16("Will"));
  without_invalid.SetRawInfo(ADDRESS_HOME_CITY, ASCIIToUTF16("Sunnyvale"));
  without_invalid.SetRawInfo(ADDRESS_HOME_STATE, ASCIIToUTF16("CA"));
  without_invalid.SetRawInfo(ADDRESS_HOME_ZIP, ASCIIToUTF16("my_zip"));
  without_invalid.SetInfo(
      AutofillType(ADDRESS_HOME_COUNTRY), ASCIIToUTF16("United States"),
      "en-US");

  AutofillProfile with_invalid = without_invalid;
  with_invalid.SetRawInfo(PHONE_HOME_WHOLE_NUMBER,
                          ASCIIToUTF16("Invalid_Phone_Number"));
  SetProfile(with_invalid);

  ASSERT_EQ(1u, personal_data_manager()->GetProfiles().size());
  AutofillProfile profile = *personal_data_manager()->GetProfiles()[0];
  ASSERT_NE(without_invalid.GetRawInfo(PHONE_HOME_WHOLE_NUMBER),
            profile.GetRawInfo(PHONE_HOME_WHOLE_NUMBER));
}

// Test invalid credit card numbers typed in prefs should be saved as-is.
// TODO(isherman): rewrite as WebUI test?
IN_PROC_BROWSER_TEST_F(AutofillTest, PrefsStringSavedAsIs) {
  CreditCard card;
  card.SetRawInfo(CREDIT_CARD_NUMBER, ASCIIToUTF16("Not_0123-5Checked"));
  SetCard(card);

  ASSERT_EQ(1u, personal_data_manager()->GetCreditCards().size());
  ASSERT_EQ(card, *personal_data_manager()->GetCreditCards()[0]);
}

// Test credit card info with an invalid number is not aggregated.
// When filling out a form with an invalid credit card number (one that does not
// pass the Luhn test) the credit card info should not be saved into Autofill
// preferences.
IN_PROC_BROWSER_TEST_F(AutofillTest, InvalidCreditCardNumberIsNotAggregated) {
#if defined(OS_WIN) && defined(USE_ASH)
  // Disable this test in Metro+Ash for now (http://crbug.com/262796).
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kAshBrowserTests))
    return;
#endif

  ASSERT_TRUE(test_server()->Start());
  std::string card("4408 0412 3456 7890");
  ASSERT_FALSE(autofill::IsValidCreditCardNumber(ASCIIToUTF16(card)));
  SubmitCreditCard("Bob Smith", card.c_str(), "12", "2014");
  ASSERT_EQ(0u,
            InfoBarService::FromWebContents(
                browser()->tab_strip_model()->GetActiveWebContents())->
                    infobar_count());
}

// Test whitespaces and separator chars are stripped for valid CC numbers.
// The credit card numbers used in this test pass the Luhn test. For reference:
// http://www.merriampark.com/anatomycc.htm
IN_PROC_BROWSER_TEST_F(AutofillTest,
                       WhitespacesAndSeparatorCharsStrippedForValidCCNums) {
#if defined(OS_WIN) && defined(USE_ASH)
  // Disable this test in Metro+Ash for now (http://crbug.com/262796).
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kAshBrowserTests))
    return;
#endif

  ASSERT_TRUE(test_server()->Start());
  SubmitCreditCard("Bob Smith", "4408 0412 3456 7893", "12", "2014");
  SubmitCreditCard("Jane Doe", "4417-1234-5678-9113", "10", "2013");

  ASSERT_EQ(2u, personal_data_manager()->GetCreditCards().size());
  string16 cc1 = personal_data_manager()->GetCreditCards()[0]->GetRawInfo(
      CREDIT_CARD_NUMBER);
  ASSERT_TRUE(autofill::IsValidCreditCardNumber(cc1));
  string16 cc2 = personal_data_manager()->GetCreditCards()[1]->GetRawInfo(
      CREDIT_CARD_NUMBER);
  ASSERT_TRUE(autofill::IsValidCreditCardNumber(cc2));
}

// Test that Autofill aggregates a minimum valid profile.
// The minimum required address fields must be specified: First Name, Last Name,
// Address Line 1, City, Zip Code, and State.
IN_PROC_BROWSER_TEST_F(AutofillTest, AggregatesMinValidProfile) {
  ASSERT_TRUE(test_server()->Start());
  FormMap data;
  data["NAME_FIRST"] = "Bob";
  data["NAME_LAST"] = "Smith";
  data["ADDRESS_HOME_LINE1"] = "1234 H St.";
  data["ADDRESS_HOME_CITY"] = "Mountain View";
  data["ADDRESS_HOME_STATE"] = "CA";
  data["ADDRESS_HOME_ZIP"] = "94043";
  FillFormAndSubmit("duplicate_profiles_test.html", data);

  ASSERT_EQ(1u, personal_data_manager()->GetProfiles().size());
}

// Test Autofill does not aggregate profiles with no address info.
// The minimum required address fields must be specified: First Name, Last Name,
// Address Line 1, City, Zip Code, and State.
IN_PROC_BROWSER_TEST_F(AutofillTest, ProfilesNotAggregatedWithNoAddress) {
  ASSERT_TRUE(test_server()->Start());
  FormMap data;
  data["NAME_FIRST"] = "Bob";
  data["NAME_LAST"] = "Smith";
  data["EMAIL_ADDRESS"] = "bsmith@example.com";
  data["COMPANY_NAME"] = "Mountain View";
  data["ADDRESS_HOME_CITY"] = "Mountain View";
  data["PHONE_HOME_WHOLE_NUMBER"] = "650-555-4567";
  FillFormAndSubmit("duplicate_profiles_test.html", data);

  ASSERT_TRUE(personal_data_manager()->GetProfiles().empty());
}

// Test Autofill does not aggregate profiles with an invalid email.
IN_PROC_BROWSER_TEST_F(AutofillTest, ProfilesNotAggregatedWithInvalidEmail) {
  ASSERT_TRUE(test_server()->Start());
  FormMap data;
  data["NAME_FIRST"] = "Bob";
  data["NAME_LAST"] = "Smith";
  data["EMAIL_ADDRESS"] = "garbage";
  data["ADDRESS_HOME_LINE1"] = "1234 H St.";
  data["ADDRESS_HOME_CITY"] = "San Jose";
  data["ADDRESS_HOME_STATE"] = "CA";
  data["ADDRESS_HOME_ZIP"] = "95110";
  data["COMPANY_NAME"] = "Company X";
  data["PHONE_HOME_WHOLE_NUMBER"] = "408-871-4567";
  FillFormAndSubmit("duplicate_profiles_test.html", data);

  ASSERT_TRUE(personal_data_manager()->GetProfiles().empty());
}

// Test profile is saved if phone number is valid in selected country.
// The data file contains two profiles with valid phone numbers and two
// profiles with invalid phone numbers from their respective country.
// DISABLED: http://crbug.com/150084
IN_PROC_BROWSER_TEST_F(AutofillTest,
                       DISABLED_ProfileSavedWithValidCountryPhone) {
  ASSERT_TRUE(test_server()->Start());
  std::vector<FormMap> profiles;

  FormMap data1;
  data1["NAME_FIRST"] = "Bob";
  data1["NAME_LAST"] = "Smith";
  data1["ADDRESS_HOME_LINE1"] = "123 Cherry Ave";
  data1["ADDRESS_HOME_CITY"] = "Mountain View";
  data1["ADDRESS_HOME_STATE"] = "CA";
  data1["ADDRESS_HOME_ZIP"] = "94043";
  data1["ADDRESS_HOME_COUNTRY"] = "United States";
  data1["PHONE_HOME_WHOLE_NUMBER"] = "408-871-4567";
  profiles.push_back(data1);

  FormMap data2;
  data2["NAME_FIRST"] = "John";
  data2["NAME_LAST"] = "Doe";
  data2["ADDRESS_HOME_LINE1"] = "987 H St";
  data2["ADDRESS_HOME_CITY"] = "San Jose";
  data2["ADDRESS_HOME_STATE"] = "CA";
  data2["ADDRESS_HOME_ZIP"] = "95510";
  data2["ADDRESS_HOME_COUNTRY"] = "United States";
  data2["PHONE_HOME_WHOLE_NUMBER"] = "408-123-456";
  profiles.push_back(data2);

  FormMap data3;
  data3["NAME_FIRST"] = "Jane";
  data3["NAME_LAST"] = "Doe";
  data3["ADDRESS_HOME_LINE1"] = "1523 Garcia St";
  data3["ADDRESS_HOME_CITY"] = "Mountain View";
  data3["ADDRESS_HOME_STATE"] = "CA";
  data3["ADDRESS_HOME_ZIP"] = "94043";
  data3["ADDRESS_HOME_COUNTRY"] = "Germany";
  data3["PHONE_HOME_WHOLE_NUMBER"] = "+49 40-80-81-79-000";
  profiles.push_back(data3);

  FormMap data4;
  data4["NAME_FIRST"] = "Bonnie";
  data4["NAME_LAST"] = "Smith";
  data4["ADDRESS_HOME_LINE1"] = "6723 Roadway Rd";
  data4["ADDRESS_HOME_CITY"] = "San Jose";
  data4["ADDRESS_HOME_STATE"] = "CA";
  data4["ADDRESS_HOME_ZIP"] = "95510";
  data4["ADDRESS_HOME_COUNTRY"] = "Germany";
  data4["PHONE_HOME_WHOLE_NUMBER"] = "+21 08450 777 777";
  profiles.push_back(data4);

  for (size_t i = 0; i < profiles.size(); ++i)
    FillFormAndSubmit("autofill_test_form.html", profiles[i]);

  ASSERT_EQ(2u, personal_data_manager()->GetProfiles().size());
  ASSERT_EQ(ASCIIToUTF16("(408) 871-4567"),
            personal_data_manager()->GetProfiles()[0]->GetRawInfo(
                PHONE_HOME_WHOLE_NUMBER));
  ASSERT_EQ(ASCIIToUTF16("+49 40/808179000"),
            personal_data_manager()->GetProfiles()[1]->GetRawInfo(
                PHONE_HOME_WHOLE_NUMBER));
}

// Test Autofill appends country codes to aggregated phone numbers.
// The country code is added for the following case:
//   The phone number contains the correct national number size and
//   is a valid format.
IN_PROC_BROWSER_TEST_F(AutofillTest, AppendCountryCodeForAggregatedPhones) {
  ASSERT_TRUE(test_server()->Start());
  FormMap data;
  data["NAME_FIRST"] = "Bob";
  data["NAME_LAST"] = "Smith";
  data["ADDRESS_HOME_LINE1"] = "1234 H St.";
  data["ADDRESS_HOME_CITY"] = "San Jose";
  data["ADDRESS_HOME_STATE"] = "CA";
  data["ADDRESS_HOME_ZIP"] = "95110";
  data["ADDRESS_HOME_COUNTRY"] = "Germany";
  data["PHONE_HOME_WHOLE_NUMBER"] = "(08) 450 777-777";
  FillFormAndSubmit("autofill_test_form.html", data);

  ASSERT_EQ(1u, personal_data_manager()->GetProfiles().size());
  string16 phone = personal_data_manager()->GetProfiles()[0]->GetRawInfo(
      PHONE_HOME_WHOLE_NUMBER);
  ASSERT_TRUE(StartsWith(phone, ASCIIToUTF16("+49"), true));
}

// Test CC info not offered to be saved when autocomplete=off for CC field.
// If the credit card number field has autocomplete turned off, then the credit
// card infobar should not offer to save the credit card info. The credit card
// number must be a valid Luhn number.
IN_PROC_BROWSER_TEST_F(AutofillTest, CCInfoNotStoredWhenAutocompleteOff) {
#if defined(OS_WIN) && defined(USE_ASH)
  // Disable this test in Metro+Ash for now (http://crbug.com/262796).
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kAshBrowserTests))
    return;
#endif

  ASSERT_TRUE(test_server()->Start());
  FormMap data;
  data["CREDIT_CARD_NAME"] = "Bob Smith";
  data["CREDIT_CARD_NUMBER"] = "4408041234567893";
  data["CREDIT_CARD_EXP_MONTH"] = "12";
  data["CREDIT_CARD_EXP_4_DIGIT_YEAR"] = "2014";
  FillFormAndSubmit("cc_autocomplete_off_test.html", data);

  ASSERT_EQ(0u,
            InfoBarService::FromWebContents(
                browser()->tab_strip_model()->GetActiveWebContents())->
                    infobar_count());
}

// Test profile not aggregated if email found in non-email field.
IN_PROC_BROWSER_TEST_F(AutofillTest, ProfileWithEmailInOtherFieldNotSaved) {
  ASSERT_TRUE(test_server()->Start());

  FormMap data;
  data["NAME_FIRST"] = "Bob";
  data["NAME_LAST"] = "Smith";
  data["ADDRESS_HOME_LINE1"] = "bsmith@gmail.com";
  data["ADDRESS_HOME_CITY"] = "San Jose";
  data["ADDRESS_HOME_STATE"] = "CA";
  data["ADDRESS_HOME_ZIP"] = "95110";
  data["COMPANY_NAME"] = "Company X";
  data["PHONE_HOME_WHOLE_NUMBER"] = "408-871-4567";
  FillFormAndSubmit("duplicate_profiles_test.html", data);

  ASSERT_EQ(0u, personal_data_manager()->GetProfiles().size());
}

// Test that profiles merge for aggregated data with same address.
// The criterion for when two profiles are expected to be merged is when their
// 'Address Line 1' and 'City' data match. When two profiles are merged, any
// remaining address fields are expected to be overwritten. Any non-address
// fields should accumulate multi-valued data.
// DISABLED: http://crbug.com/150084
IN_PROC_BROWSER_TEST_F(AutofillTest,
                       DISABLED_MergeAggregatedProfilesWithSameAddress) {
  AggregateProfilesIntoAutofillPrefs("dataset_2.txt");

  ASSERT_EQ(3u, personal_data_manager()->GetProfiles().size());
}

// Test profiles are not merged without mininum address values.
// Mininum address values needed during aggregation are: address line 1, city,
// state, and zip code.
// Profiles are merged when data for address line 1 and city match.
// DISABLED: http://crbug.com/150084
IN_PROC_BROWSER_TEST_F(AutofillTest,
                       DISABLED_ProfilesNotMergedWhenNoMinAddressData) {
  AggregateProfilesIntoAutofillPrefs("dataset_no_address.txt");

  ASSERT_EQ(0u, personal_data_manager()->GetProfiles().size());
}

// Test Autofill ability to merge duplicate profiles and throw away junk.
// TODO(isherman): this looks redundant, consider removing.
// DISABLED: http://crbug.com/150084
IN_PROC_BROWSER_TEST_F(AutofillTest,
                       DISABLED_MergeAggregatedDuplicatedProfiles) {
  int num_of_profiles =
      AggregateProfilesIntoAutofillPrefs("dataset_no_address.txt");

  ASSERT_GT(num_of_profiles,
            static_cast<int>(personal_data_manager()->GetProfiles().size()));
}

}  // namespace autofill
