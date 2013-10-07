// Copyright (c) 2013 The Chromium Authors. All rights reserved.
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
#include "chrome/browser/translate/translate_infobar_delegate.h"
#include "chrome/browser/translate/translate_manager.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/render_messages.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/test_switches.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/autofill/content/browser/autofill_driver_impl.h"
#include "components/autofill/core/browser/autofill_common_test.h"
#include "components/autofill/core/browser/autofill_external_delegate.h"
#include "components/autofill/core/browser/autofill_manager.h"
#include "components/autofill/core/browser/autofill_manager_test_delegate.h"
#include "components/autofill/core/browser/autofill_profile.h"
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

static const char* kDataURIPrefix = "data:text/html;charset=utf-8,";
static const char* kTestFormString =
    "<form action=\"http://www.example.com/\" method=\"POST\">"
    "<label for=\"firstname\">First name:</label>"
    " <input type=\"text\" id=\"firstname\""
    "        onFocus=\"domAutomationController.send(true)\"><br>"
    "<label for=\"lastname\">Last name:</label>"
    " <input type=\"text\" id=\"lastname\"><br>"
    "<label for=\"address1\">Address line 1:</label>"
    " <input type=\"text\" id=\"address1\"><br>"
    "<label for=\"address2\">Address line 2:</label>"
    " <input type=\"text\" id=\"address2\"><br>"
    "<label for=\"city\">City:</label>"
    " <input type=\"text\" id=\"city\"><br>"
    "<label for=\"state\">State:</label>"
    " <select id=\"state\">"
    " <option value=\"\" selected=\"yes\">--</option>"
    " <option value=\"CA\">California</option>"
    " <option value=\"TX\">Texas</option>"
    " </select><br>"
    "<label for=\"zip\">ZIP code:</label>"
    " <input type=\"text\" id=\"zip\"><br>"
    "<label for=\"country\">Country:</label>"
    " <select id=\"country\">"
    " <option value=\"\" selected=\"yes\">--</option>"
    " <option value=\"CA\">Canada</option>"
    " <option value=\"US\">United States</option>"
    " </select><br>"
    "<label for=\"phone\">Phone number:</label>"
    " <input type=\"text\" id=\"phone\"><br>"
    "</form>";


// AutofillManagerTestDelegateImpl --------------------------------------------

class AutofillManagerTestDelegateImpl
    : public autofill::AutofillManagerTestDelegate {
 public:
  AutofillManagerTestDelegateImpl() {}
  virtual ~AutofillManagerTestDelegateImpl() {}

  // autofill::AutofillManagerTestDelegate:
  virtual void DidPreviewFormData() OVERRIDE {
    loop_runner_->Quit();
  }

  virtual void DidFillFormData() OVERRIDE {
    loop_runner_->Quit();
  }

  virtual void DidShowSuggestions() OVERRIDE {
    loop_runner_->Quit();
  }

  void Reset() {
    loop_runner_ = new content::MessageLoopRunner();
  }

  void Wait() {
    loop_runner_->Run();
  }

 private:
  scoped_refptr<content::MessageLoopRunner> loop_runner_;

  DISALLOW_COPY_AND_ASSIGN(AutofillManagerTestDelegateImpl);
};


// WindowedPersonalDataManagerObserver ----------------------------------------

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
    if (infobar_service_) {
      while (infobar_service_->infobar_count() > 0) {
        infobar_service_->RemoveInfoBar(infobar_service_->infobar_at(0));
      }
    }
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
    infobar_service_ = InfoBarService::FromWebContents(
        browser_->tab_strip_model()->GetActiveWebContents());
    infobar_service_->infobar_at(0)->AsConfirmInfoBarDelegate()->Accept();
  }

  void Wait() {
    if (!alerted_) {
      has_run_message_loop_ = true;
      content::RunMessageLoop();
    }
    PersonalDataManagerFactory::GetForProfile(browser_->profile())->
        RemoveObserver(this);
  }

 private:
  bool alerted_;
  bool has_run_message_loop_;
  Browser* browser_;
  content::NotificationRegistrar registrar_;
  InfoBarService* infobar_service_;

  DISALLOW_COPY_AND_ASSIGN(WindowedPersonalDataManagerObserver);
};


// TestAutofillExternalDelegate -----------------------------------------------

class TestAutofillExternalDelegate : public AutofillExternalDelegate {
 public:
  TestAutofillExternalDelegate(content::WebContents* web_contents,
                               AutofillManager* autofill_manager,
                               AutofillDriver* autofill_driver)
      : AutofillExternalDelegate(web_contents, autofill_manager,
                                 autofill_driver),
        keyboard_listener_(NULL) {
  }

  virtual ~TestAutofillExternalDelegate() {}

  // AutofillExternalDelegate:
  virtual void OnPopupShown(content::KeyboardListener* listener) OVERRIDE {
    AutofillExternalDelegate::OnPopupShown(listener);
    keyboard_listener_ = listener;
  }

  virtual void OnPopupHidden(content::KeyboardListener* listener) OVERRIDE {
    keyboard_listener_ = NULL;
    AutofillExternalDelegate::OnPopupHidden(listener);
  }

  content::KeyboardListener* keyboard_listener() {
    return keyboard_listener_;
  }

 private:
  // The popup that is currently registered as a keyboard listener, or NULL if
  // there is none.
  content::KeyboardListener* keyboard_listener_;

  DISALLOW_COPY_AND_ASSIGN(TestAutofillExternalDelegate);
};


// AutofillInteractiveTest ----------------------------------------------------

class AutofillInteractiveTest : public InProcessBrowserTest {
 protected:
  AutofillInteractiveTest() {}
  virtual ~AutofillInteractiveTest() {}

  // InProcessBrowserTest:
  virtual void SetUpOnMainThread() OVERRIDE {
    // Don't want Keychain coming up on Mac.
    test::DisableSystemServices(browser()->profile());

    // When testing the native UI, hook up a test external delegate, which
    // allows us to forward keyboard events to the popup directly.
    content::WebContents* web_contents =
        browser()->tab_strip_model()->GetActiveWebContents();
    AutofillDriverImpl* autofill_driver =
        AutofillDriverImpl::FromWebContents(web_contents);
    AutofillManager* autofill_manager = autofill_driver->autofill_manager();
    scoped_ptr<AutofillExternalDelegate> external_delegate(
        new TestAutofillExternalDelegate(web_contents, autofill_manager,
                                         autofill_driver));
    autofill_driver->SetAutofillExternalDelegate(external_delegate.Pass());
    autofill_manager->SetTestDelegate(&test_delegate_);
  }

  virtual void CleanUpOnMainThread() OVERRIDE {
    // Make sure to close any showing popups prior to tearing down the UI.
    content::WebContents* web_contents =
        browser()->tab_strip_model()->GetActiveWebContents();
    AutofillManager* autofill_manager =
        AutofillDriverImpl::FromWebContents(web_contents)->autofill_manager();
    autofill_manager->delegate()->HideAutofillPopup();
  }

  PersonalDataManager* GetPersonalDataManager() {
    return PersonalDataManagerFactory::GetForProfile(browser()->profile());
  }

  content::RenderViewHost* GetRenderViewHost() {
    return browser()->tab_strip_model()->GetActiveWebContents()->
        GetRenderViewHost();
  }

  TestAutofillExternalDelegate* GetExternalDelegate() {
    content::WebContents* web_contents =
        browser()->tab_strip_model()->GetActiveWebContents();
    AutofillDriverImpl* autofill_driver =
        AutofillDriverImpl::FromWebContents(web_contents);
    return static_cast<TestAutofillExternalDelegate*>(
        autofill_driver->autofill_external_delegate());
  }

  void CreateTestProfile() {
    AutofillProfile profile;
    test::SetProfileInfo(
        &profile, "Milton", "C.", "Waddams",
        "red.swingline@initech.com", "Initech", "4120 Freidrich Lane",
        "Basement", "Austin", "Texas", "78744", "US", "5125551234");

    WindowedPersonalDataManagerObserver observer(browser());
    GetPersonalDataManager()->AddProfile(profile);

    // AddProfile is asynchronous. Wait for it to finish before continuing the
    // tests.
    observer.Wait();
  }

  void SetProfiles(std::vector<AutofillProfile>* profiles) {
    WindowedPersonalDataManagerObserver observer(browser());
    GetPersonalDataManager()->SetProfiles(profiles);
    observer.Wait();
  }

  void SetProfile(const AutofillProfile& profile) {
    std::vector<AutofillProfile> profiles;
    profiles.push_back(profile);
    SetProfiles(&profiles);
  }

  // Populates a webpage form using autofill data and keypress events.
  // This function focuses the specified input field in the form, and then
  // sends keypress events to the tab to cause the form to be populated.
  void PopulateForm(const std::string& field_id) {
    std::string js("document.getElementById('" + field_id + "').focus();");
    ASSERT_TRUE(content::ExecuteScript(GetRenderViewHost(), js));

    SendKeyToPageAndWait(ui::VKEY_DOWN);
    SendKeyToPopupAndWait(ui::VKEY_DOWN);
    SendKeyToPopupAndWait(ui::VKEY_RETURN);
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

  void SimulateURLFetch(bool success) {
    net::TestURLFetcher* fetcher = url_fetcher_factory_.GetFetcherByID(0);
    ASSERT_TRUE(fetcher);
    net::URLRequestStatus status;
    status.set_status(success ? net::URLRequestStatus::SUCCESS :
                                net::URLRequestStatus::FAILED);

    std::string script = " var google = {};"
        "google.translate = (function() {"
        "  return {"
        "    TranslateService: function() {"
        "      return {"
        "        isAvailable : function() {"
        "          return true;"
        "        },"
        "        restore : function() {"
        "          return;"
        "        },"
        "        getDetectedLanguage : function() {"
        "          return \"ja\";"
        "        },"
        "        translatePage : function(originalLang, targetLang,"
        "                                 onTranslateProgress) {"
        "          document.getElementsByTagName(\"body\")[0].innerHTML = '" +
        std::string(kTestFormString) +
        "              ';"
        "          onTranslateProgress(100, true, false);"
        "        }"
        "      };"
        "    }"
        "  };"
        "})();";

    fetcher->set_url(fetcher->GetOriginalURL());
    fetcher->set_status(status);
    fetcher->set_response_code(success ? 200 : 500);
    fetcher->SetResponseString(script);
    fetcher->delegate()->OnURLFetchComplete(fetcher);
  }

  void FocusFirstNameField() {
    bool result = false;
    ASSERT_TRUE(content::ExecuteScriptAndExtractBool(
        GetRenderViewHost(),
        "if (document.readyState === 'complete')"
        "  document.getElementById('firstname').focus();"
        "else"
        "  domAutomationController.send(false);",
        &result));
    ASSERT_TRUE(result);
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

  void SendKeyToPageAndWait(ui::KeyboardCode key) {
    test_delegate_.Reset();
    content::SimulateKeyPress(
        browser()->tab_strip_model()->GetActiveWebContents(),
        key, false, false, false, false);
    test_delegate_.Wait();
  }

  void SendKeyToPopupAndWait(ui::KeyboardCode key) {
    // When testing the native UI, route popup-targeted key presses via the
    // external delegate.
    content::NativeWebKeyboardEvent event;
    event.windowsKeyCode = key;
    test_delegate_.Reset();
    GetExternalDelegate()->keyboard_listener()->HandleKeyPressEvent(event);
    test_delegate_.Wait();
  }

  void TryBasicFormFill() {
    FocusFirstNameField();

    // Start filling the first name field with "M" and wait for the popup to be
    // shown.
    SendKeyToPageAndWait(ui::VKEY_M);

    // Press the down arrow to select the suggestion and preview the autofilled
    // form.
    SendKeyToPopupAndWait(ui::VKEY_DOWN);

    // The previewed values should not be accessible to JavaScript.
    ExpectFieldValue("firstname", "M");
    ExpectFieldValue("lastname", std::string());
    ExpectFieldValue("address1", std::string());
    ExpectFieldValue("address2", std::string());
    ExpectFieldValue("city", std::string());
    ExpectFieldValue("state", std::string());
    ExpectFieldValue("zip", std::string());
    ExpectFieldValue("country", std::string());
    ExpectFieldValue("phone", std::string());
    // TODO(isherman): It would be nice to test that the previewed values are
    // displayed: http://crbug.com/57220

    // Press Enter to accept the autofill suggestions.
    SendKeyToPopupAndWait(ui::VKEY_RETURN);

    // The form should be filled.
    ExpectFilledTestForm();
  }

 private:
  AutofillManagerTestDelegateImpl test_delegate_;

  net::TestURLFetcherFactory url_fetcher_factory_;

  DISALLOW_COPY_AND_ASSIGN(AutofillInteractiveTest);
};

// Test that basic form fill is working.
IN_PROC_BROWSER_TEST_F(AutofillInteractiveTest, BasicFormFill) {
  CreateTestProfile();

  // Load the test page.
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(browser(),
      GURL(std::string(kDataURIPrefix) + kTestFormString)));

  // Invoke Autofill.
  TryBasicFormFill();
}

// Test that form filling can be initiated by pressing the down arrow.
IN_PROC_BROWSER_TEST_F(AutofillInteractiveTest, AutofillViaDownArrow) {
  CreateTestProfile();

  // Load the test page.
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(browser(),
      GURL(std::string(kDataURIPrefix) + kTestFormString)));

  // Focus a fillable field.
  FocusFirstNameField();

  // Press the down arrow to initiate Autofill and wait for the popup to be
  // shown.
  SendKeyToPageAndWait(ui::VKEY_DOWN);

  // Press the down arrow to select the suggestion and preview the autofilled
  // form.
  SendKeyToPopupAndWait(ui::VKEY_DOWN);

  // Press Enter to accept the autofill suggestions.
  SendKeyToPopupAndWait(ui::VKEY_RETURN);

  // The form should be filled.
  ExpectFilledTestForm();
}

IN_PROC_BROWSER_TEST_F(AutofillInteractiveTest, AutofillSelectViaTab) {
  CreateTestProfile();

  // Load the test page.
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(browser(),
      GURL(std::string(kDataURIPrefix) + kTestFormString)));

  // Focus a fillable field.
  FocusFirstNameField();

  // Press the down arrow to initiate Autofill and wait for the popup to be
  // shown.
  SendKeyToPageAndWait(ui::VKEY_DOWN);

  // Press the down arrow to select the suggestion and preview the autofilled
  // form.
  SendKeyToPopupAndWait(ui::VKEY_DOWN);

  // Press tab to accept the autofill suggestions.
  SendKeyToPopupAndWait(ui::VKEY_TAB);

  // The form should be filled.
  ExpectFilledTestForm();
}

// Test that a JavaScript onchange event is fired after auto-filling a form.
IN_PROC_BROWSER_TEST_F(AutofillInteractiveTest, OnChangeAfterAutofill) {
  CreateTestProfile();

  const char* kOnChangeScript =
      "<script>"
      "focused_fired = false;"
      "unfocused_fired = false;"
      "changed_select_fired = false;"
      "unchanged_select_fired = false;"
      "document.getElementById('firstname').onchange = function() {"
      "  focused_fired = true;"
      "};"
      "document.getElementById('lastname').onchange = function() {"
      "  unfocused_fired = true;"
      "};"
      "document.getElementById('state').onchange = function() {"
      "  changed_select_fired = true;"
      "};"
      "document.getElementById('country').onchange = function() {"
      "  unchanged_select_fired = true;"
      "};"
      "document.getElementById('country').value = 'US';"
      "</script>";

  // Load the test page.
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(browser(),
      GURL(std::string(kDataURIPrefix) + kTestFormString + kOnChangeScript)));

  // Invoke Autofill.
  FocusFirstNameField();

  // Start filling the first name field with "M" and wait for the popup to be
  // shown.
  SendKeyToPageAndWait(ui::VKEY_M);

  // Press the down arrow to select the suggestion and preview the autofilled
  // form.
  SendKeyToPopupAndWait(ui::VKEY_DOWN);

  // Press Enter to accept the autofill suggestions.
  SendKeyToPopupAndWait(ui::VKEY_RETURN);

  // The form should be filled.
  ExpectFilledTestForm();

  // The change event should have already fired for unfocused fields, both of
  // <input> and of <select> type. However, it should not yet have fired for the
  // focused field.
  bool focused_fired = false;
  bool unfocused_fired = false;
  bool changed_select_fired = false;
  bool unchanged_select_fired = false;
  ASSERT_TRUE(content::ExecuteScriptAndExtractBool(
      GetRenderViewHost(),
      "domAutomationController.send(focused_fired);",
      &focused_fired));
  ASSERT_TRUE(content::ExecuteScriptAndExtractBool(
      GetRenderViewHost(),
      "domAutomationController.send(unfocused_fired);",
      &unfocused_fired));
  ASSERT_TRUE(content::ExecuteScriptAndExtractBool(
      GetRenderViewHost(),
      "domAutomationController.send(changed_select_fired);",
      &changed_select_fired));
  ASSERT_TRUE(content::ExecuteScriptAndExtractBool(
      GetRenderViewHost(),
      "domAutomationController.send(unchanged_select_fired);",
      &unchanged_select_fired));
  EXPECT_FALSE(focused_fired);
  EXPECT_TRUE(unfocused_fired);
  EXPECT_TRUE(changed_select_fired);
  EXPECT_FALSE(unchanged_select_fired);

  // Unfocus the first name field. Its change event should fire.
  ASSERT_TRUE(content::ExecuteScriptAndExtractBool(
      GetRenderViewHost(),
      "document.getElementById('firstname').blur();"
      "domAutomationController.send(focused_fired);", &focused_fired));
  EXPECT_TRUE(focused_fired);
}

// Test that we can autofill forms distinguished only by their |id| attribute.
IN_PROC_BROWSER_TEST_F(AutofillInteractiveTest,
                       AutofillFormsDistinguishedById) {
  CreateTestProfile();

  // Load the test page.
  const std::string kURL =
      std::string(kDataURIPrefix) + kTestFormString +
      "<script>"
      "var mainForm = document.forms[0];"
      "mainForm.id = 'mainForm';"
      "var newForm = document.createElement('form');"
      "newForm.action = mainForm.action;"
      "newForm.method = mainForm.method;"
      "newForm.id = 'newForm';"
      "mainForm.parentNode.insertBefore(newForm, mainForm);"
      "</script>";
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(browser(), GURL(kURL)));

  // Invoke Autofill.
  TryBasicFormFill();
}

// Test that we properly autofill forms with repeated fields.
// In the wild, the repeated fields are typically either email fields
// (duplicated for "confirmation"); or variants that are hot-swapped via
// JavaScript, with only one actually visible at any given time.
IN_PROC_BROWSER_TEST_F(AutofillInteractiveTest, AutofillFormWithRepeatedField) {
  CreateTestProfile();

  // Load the test page.
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(browser(),
      GURL(std::string(kDataURIPrefix) +
           "<form action=\"http://www.example.com/\" method=\"POST\">"
           "<label for=\"firstname\">First name:</label>"
           " <input type=\"text\" id=\"firstname\""
           "        onFocus=\"domAutomationController.send(true)\"><br>"
           "<label for=\"lastname\">Last name:</label>"
           " <input type=\"text\" id=\"lastname\"><br>"
           "<label for=\"address1\">Address line 1:</label>"
           " <input type=\"text\" id=\"address1\"><br>"
           "<label for=\"address2\">Address line 2:</label>"
           " <input type=\"text\" id=\"address2\"><br>"
           "<label for=\"city\">City:</label>"
           " <input type=\"text\" id=\"city\"><br>"
           "<label for=\"state\">State:</label>"
           " <select id=\"state\">"
           " <option value=\"\" selected=\"yes\">--</option>"
           " <option value=\"CA\">California</option>"
           " <option value=\"TX\">Texas</option>"
           " </select><br>"
           "<label for=\"state_freeform\" style=\"display:none\">State:</label>"
           " <input type=\"text\" id=\"state_freeform\""
           "        style=\"display:none\"><br>"
           "<label for=\"zip\">ZIP code:</label>"
           " <input type=\"text\" id=\"zip\"><br>"
           "<label for=\"country\">Country:</label>"
           " <select id=\"country\">"
           " <option value=\"\" selected=\"yes\">--</option>"
           " <option value=\"CA\">Canada</option>"
           " <option value=\"US\">United States</option>"
           " </select><br>"
           "<label for=\"phone\">Phone number:</label>"
           " <input type=\"text\" id=\"phone\"><br>"
           "</form>")));

  // Invoke Autofill.
  TryBasicFormFill();
  ExpectFieldValue("state_freeform", std::string());
}

// Test that we properly autofill forms with non-autofillable fields.
IN_PROC_BROWSER_TEST_F(AutofillInteractiveTest,
                       AutofillFormWithNonAutofillableField) {
  CreateTestProfile();

  // Load the test page.
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(browser(),
      GURL(std::string(kDataURIPrefix) +
           "<form action=\"http://www.example.com/\" method=\"POST\">"
           "<label for=\"firstname\">First name:</label>"
           " <input type=\"text\" id=\"firstname\""
           "        onFocus=\"domAutomationController.send(true)\"><br>"
           "<label for=\"middlename\">Middle name:</label>"
           " <input type=\"text\" id=\"middlename\" autocomplete=\"off\" /><br>"
           "<label for=\"lastname\">Last name:</label>"
           " <input type=\"text\" id=\"lastname\"><br>"
           "<label for=\"address1\">Address line 1:</label>"
           " <input type=\"text\" id=\"address1\"><br>"
           "<label for=\"address2\">Address line 2:</label>"
           " <input type=\"text\" id=\"address2\"><br>"
           "<label for=\"city\">City:</label>"
           " <input type=\"text\" id=\"city\"><br>"
           "<label for=\"state\">State:</label>"
           " <select id=\"state\">"
           " <option value=\"\" selected=\"yes\">--</option>"
           " <option value=\"CA\">California</option>"
           " <option value=\"TX\">Texas</option>"
           " </select><br>"
           "<label for=\"zip\">ZIP code:</label>"
           " <input type=\"text\" id=\"zip\"><br>"
           "<label for=\"country\">Country:</label>"
           " <select id=\"country\">"
           " <option value=\"\" selected=\"yes\">--</option>"
           " <option value=\"CA\">Canada</option>"
           " <option value=\"US\">United States</option>"
           " </select><br>"
           "<label for=\"phone\">Phone number:</label>"
           " <input type=\"text\" id=\"phone\"><br>"
           "</form>")));

  // Invoke Autofill.
  TryBasicFormFill();
}

// Test that we can Autofill dynamically generated forms.
IN_PROC_BROWSER_TEST_F(AutofillInteractiveTest, DynamicFormFill) {
  CreateTestProfile();

  // Load the test page.
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(browser(),
      GURL(std::string(kDataURIPrefix) +
           "<form id=\"form\" action=\"http://www.example.com/\""
           "      method=\"POST\"></form>"
           "<script>"
           "function AddElement(name, label) {"
           "  var form = document.getElementById('form');"
           ""
           "  var label_text = document.createTextNode(label);"
           "  var label_element = document.createElement('label');"
           "  label_element.setAttribute('for', name);"
           "  label_element.appendChild(label_text);"
           "  form.appendChild(label_element);"
           ""
           "  if (name === 'state' || name === 'country') {"
           "    var select_element = document.createElement('select');"
           "    select_element.setAttribute('id', name);"
           "    select_element.setAttribute('name', name);"
           ""
           "    /* Add an empty selected option. */"
           "    var default_option = new Option('--', '', true);"
           "    select_element.appendChild(default_option);"
           ""
           "    /* Add the other options. */"
           "    if (name == 'state') {"
           "      var option1 = new Option('California', 'CA');"
           "      select_element.appendChild(option1);"
           "      var option2 = new Option('Texas', 'TX');"
           "      select_element.appendChild(option2);"
           "    } else {"
           "      var option1 = new Option('Canada', 'CA');"
           "      select_element.appendChild(option1);"
           "      var option2 = new Option('United States', 'US');"
           "      select_element.appendChild(option2);"
           "    }"
           ""
           "    form.appendChild(select_element);"
           "  } else {"
           "    var input_element = document.createElement('input');"
           "    input_element.setAttribute('id', name);"
           "    input_element.setAttribute('name', name);"
           ""
           "    /* Add the onFocus listener to the 'firstname' field. */"
           "    if (name === 'firstname') {"
           "      input_element.setAttribute("
           "          'onFocus', 'domAutomationController.send(true)');"
           "    }"
           ""
           "    form.appendChild(input_element);"
           "  }"
           ""
           "  form.appendChild(document.createElement('br'));"
           "};"
           ""
           "function BuildForm() {"
           "  var elements = ["
           "    ['firstname', 'First name:'],"
           "    ['lastname', 'Last name:'],"
           "    ['address1', 'Address line 1:'],"
           "    ['address2', 'Address line 2:'],"
           "    ['city', 'City:'],"
           "    ['state', 'State:'],"
           "    ['zip', 'ZIP code:'],"
           "    ['country', 'Country:'],"
           "    ['phone', 'Phone number:'],"
           "  ];"
           ""
           "  for (var i = 0; i < elements.length; i++) {"
           "    var name = elements[i][0];"
           "    var label = elements[i][1];"
           "    AddElement(name, label);"
           "  }"
           "};"
           "</script>")));

  // Dynamically construct the form.
  ASSERT_TRUE(content::ExecuteScript(GetRenderViewHost(), "BuildForm();"));

  // Invoke Autofill.
  TryBasicFormFill();
}

// Test that form filling works after reloading the current page.
IN_PROC_BROWSER_TEST_F(AutofillInteractiveTest, AutofillAfterReload) {
  CreateTestProfile();

  // Load the test page.
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(browser(),
      GURL(std::string(kDataURIPrefix) + kTestFormString)));

  // Reload the page.
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  web_contents->GetController().Reload(false);
  content::WaitForLoadStop(web_contents);

  // Invoke Autofill.
  TryBasicFormFill();
}

// DISABLED: http://crbug.com/150084
IN_PROC_BROWSER_TEST_F(AutofillInteractiveTest,
                       DISABLED_AutofillAfterTranslate) {
  CreateTestProfile();

  GURL url(std::string(kDataURIPrefix) +
               "<form action=\"http://www.example.com/\" method=\"POST\">"
               "<label for=\"fn\">なまえ</label>"
               " <input type=\"text\" id=\"fn\""
               "        onFocus=\"domAutomationController.send(true)\""
               "><br>"
               "<label for=\"ln\">みょうじ</label>"
               " <input type=\"text\" id=\"ln\"><br>"
               "<label for=\"a1\">Address line 1:</label>"
               " <input type=\"text\" id=\"a1\"><br>"
               "<label for=\"a2\">Address line 2:</label>"
               " <input type=\"text\" id=\"a2\"><br>"
               "<label for=\"ci\">City:</label>"
               " <input type=\"text\" id=\"ci\"><br>"
               "<label for=\"st\">State:</label>"
               " <select id=\"st\">"
               " <option value=\"\" selected=\"yes\">--</option>"
               " <option value=\"CA\">California</option>"
               " <option value=\"TX\">Texas</option>"
               " </select><br>"
               "<label for=\"z\">ZIP code:</label>"
               " <input type=\"text\" id=\"z\"><br>"
               "<label for=\"co\">Country:</label>"
               " <select id=\"co\">"
               " <option value=\"\" selected=\"yes\">--</option>"
               " <option value=\"CA\">Canada</option>"
               " <option value=\"US\">United States</option>"
               " </select><br>"
               "<label for=\"ph\">Phone number:</label>"
               " <input type=\"text\" id=\"ph\"><br>"
               "</form>");
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(browser(), url));

  // Get translation bar.
  LanguageDetectionDetails details;
  details.adopted_language = "ja";
  content::RenderViewHostTester::TestOnMessageReceived(
      GetRenderViewHost(),
      ChromeViewHostMsg_TranslateLanguageDetermined(0, details, true));
  TranslateInfoBarDelegate* delegate = InfoBarService::FromWebContents(
      browser()->tab_strip_model()->GetActiveWebContents())->infobar_at(0)->
          AsTranslateInfoBarDelegate();
  ASSERT_TRUE(delegate);
  EXPECT_EQ(TranslateInfoBarDelegate::BEFORE_TRANSLATE,
            delegate->infobar_type());

  // Simulate translation button press.
  delegate->Translate();

  // Simulate the translate script being retrieved.
  // Pass fake google.translate lib as the translate script.
  SimulateURLFetch(true);

  content::WindowedNotificationObserver translation_observer(
      chrome::NOTIFICATION_PAGE_TRANSLATED,
      content::NotificationService::AllSources());

  // Simulate translation to kick onTranslateElementLoad.
  // But right now, the call stucks here.
  // Once click the text field, it starts again.
  ASSERT_TRUE(content::ExecuteScript(
      GetRenderViewHost(), "cr.googleTranslate.onTranslateElementLoad();"));

  // Simulate the render notifying the translation has been done.
  translation_observer.Wait();

  TryBasicFormFill();
}

// Test phone fields parse correctly from a given profile.
// The high level key presses execute the following: Select the first text
// field, invoke the autofill popup list, select the first profile within the
// list, and commit to the profile to populate the form.
IN_PROC_BROWSER_TEST_F(AutofillInteractiveTest, ComparePhoneNumbers) {
  ASSERT_TRUE(test_server()->Start());

  AutofillProfile profile;
  profile.SetRawInfo(NAME_FIRST, ASCIIToUTF16("Bob"));
  profile.SetRawInfo(NAME_LAST, ASCIIToUTF16("Smith"));
  profile.SetRawInfo(ADDRESS_HOME_LINE1, ASCIIToUTF16("1234 H St."));
  profile.SetRawInfo(ADDRESS_HOME_CITY, ASCIIToUTF16("San Jose"));
  profile.SetRawInfo(ADDRESS_HOME_STATE, ASCIIToUTF16("CA"));
  profile.SetRawInfo(ADDRESS_HOME_ZIP, ASCIIToUTF16("95110"));
  profile.SetRawInfo(PHONE_HOME_WHOLE_NUMBER, ASCIIToUTF16("1-408-555-4567"));
  SetProfile(profile);

  GURL url = test_server()->GetURL("files/autofill/form_phones.html");
  ui_test_utils::NavigateToURL(browser(), url);
  PopulateForm("NAME_FIRST");

  ExpectFieldValue("NAME_FIRST", "Bob");
  ExpectFieldValue("NAME_LAST", "Smith");
  ExpectFieldValue("ADDRESS_HOME_LINE1", "1234 H St.");
  ExpectFieldValue("ADDRESS_HOME_CITY", "San Jose");
  ExpectFieldValue("ADDRESS_HOME_STATE", "CA");
  ExpectFieldValue("ADDRESS_HOME_ZIP", "95110");
  ExpectFieldValue("PHONE_HOME_WHOLE_NUMBER", "14085554567");
  ExpectFieldValue("PHONE_HOME_CITY_CODE-1", "408");
  ExpectFieldValue("PHONE_HOME_CITY_CODE-2", "408");
  ExpectFieldValue("PHONE_HOME_NUMBER", "5554567");
  ExpectFieldValue("PHONE_HOME_NUMBER_3-1", "555");
  ExpectFieldValue("PHONE_HOME_NUMBER_3-2", "555");
  ExpectFieldValue("PHONE_HOME_NUMBER_4-1", "4567");
  ExpectFieldValue("PHONE_HOME_NUMBER_4-2", "4567");
  ExpectFieldValue("PHONE_HOME_EXT-1", std::string());
  ExpectFieldValue("PHONE_HOME_EXT-2", std::string());
  ExpectFieldValue("PHONE_HOME_COUNTRY_CODE-1", "1");
}

// Test that Autofill does not fill in read-only fields.
IN_PROC_BROWSER_TEST_F(AutofillInteractiveTest, NoAutofillForReadOnlyFields) {
  ASSERT_TRUE(test_server()->Start());

  std::string addr_line1("1234 H St.");

  AutofillProfile profile;
  profile.SetRawInfo(NAME_FIRST, ASCIIToUTF16("Bob"));
  profile.SetRawInfo(NAME_LAST, ASCIIToUTF16("Smith"));
  profile.SetRawInfo(EMAIL_ADDRESS, ASCIIToUTF16("bsmith@gmail.com"));
  profile.SetRawInfo(ADDRESS_HOME_LINE1, ASCIIToUTF16(addr_line1));
  profile.SetRawInfo(ADDRESS_HOME_CITY, ASCIIToUTF16("San Jose"));
  profile.SetRawInfo(ADDRESS_HOME_STATE, ASCIIToUTF16("CA"));
  profile.SetRawInfo(ADDRESS_HOME_ZIP, ASCIIToUTF16("95110"));
  profile.SetRawInfo(COMPANY_NAME, ASCIIToUTF16("Company X"));
  profile.SetRawInfo(PHONE_HOME_WHOLE_NUMBER, ASCIIToUTF16("408-871-4567"));
  SetProfile(profile);

  GURL url = test_server()->GetURL("files/autofill/read_only_field_test.html");
  ui_test_utils::NavigateToURL(browser(), url);
  PopulateForm("firstname");

  ExpectFieldValue("email", std::string());
  ExpectFieldValue("address", addr_line1);
}

// Test form is fillable from a profile after form was reset.
// Steps:
//   1. Fill form using a saved profile.
//   2. Reset the form.
//   3. Fill form using a saved profile.
// Flakily times out: http://crbug.com/270341
IN_PROC_BROWSER_TEST_F(AutofillInteractiveTest, DISABLED_FormFillableOnReset) {
  ASSERT_TRUE(test_server()->Start());

  CreateTestProfile();

  GURL url = test_server()->GetURL("files/autofill/autofill_test_form.html");
  ui_test_utils::NavigateToURL(browser(), url);
  PopulateForm("NAME_FIRST");

  ASSERT_TRUE(content::ExecuteScript(
      browser()->tab_strip_model()->GetActiveWebContents(),
      "document.getElementById('testform').reset()"));

  PopulateForm("NAME_FIRST");

  ExpectFieldValue("NAME_FIRST", "Milton");
  ExpectFieldValue("NAME_LAST", "Waddams");
  ExpectFieldValue("EMAIL_ADDRESS", "red.swingline@initech.com");
  ExpectFieldValue("ADDRESS_HOME_LINE1", "4120 Freidrich Lane");
  ExpectFieldValue("ADDRESS_HOME_CITY", "Austin");
  ExpectFieldValue("ADDRESS_HOME_STATE", "Texas");
  ExpectFieldValue("ADDRESS_HOME_ZIP", "78744");
  ExpectFieldValue("ADDRESS_HOME_COUNTRY", "United States");
  ExpectFieldValue("PHONE_HOME_WHOLE_NUMBER", "5125551234");
}

// Test Autofill distinguishes a middle initial in a name.
// Flakily times out: http://crbug.com/270341
IN_PROC_BROWSER_TEST_F(AutofillInteractiveTest,
                       DISABLED_DistinguishMiddleInitialWithinName) {
  ASSERT_TRUE(test_server()->Start());

  CreateTestProfile();

  GURL url = test_server()->GetURL(
      "files/autofill/autofill_middleinit_form.html");
  ui_test_utils::NavigateToURL(browser(), url);
  PopulateForm("NAME_FIRST");

  ExpectFieldValue("NAME_MIDDLE", "C");
}

// Test forms with multiple email addresses are filled properly.
// Entire form should be filled with one user gesture.
// Flakily times out: http://crbug.com/270341
IN_PROC_BROWSER_TEST_F(AutofillInteractiveTest,
                       DISABLED_MultipleEmailFilledByOneUserGesture) {
  ASSERT_TRUE(test_server()->Start());

  std::string email("bsmith@gmail.com");

  AutofillProfile profile;
  profile.SetRawInfo(NAME_FIRST, ASCIIToUTF16("Bob"));
  profile.SetRawInfo(NAME_LAST, ASCIIToUTF16("Smith"));
  profile.SetRawInfo(EMAIL_ADDRESS, ASCIIToUTF16(email));
  profile.SetRawInfo(PHONE_HOME_WHOLE_NUMBER, ASCIIToUTF16("4088714567"));
  SetProfile(profile);

  GURL url = test_server()->GetURL(
      "files/autofill/autofill_confirmemail_form.html");
  ui_test_utils::NavigateToURL(browser(), url);
  PopulateForm("NAME_FIRST");

  ExpectFieldValue("EMAIL_CONFIRM", email);
  // TODO(isherman): verify entire form.
}

// http://crbug.com/150084
#if defined(OS_MACOSX)
#define MAYBE_FormFillLatencyAfterSubmit FormFillLatencyAfterSubmit
#else
#define MAYBE_FormFillLatencyAfterSubmit DISABLED_FormFillLatencyAfterSubmit
#endif
// Test latency time on form submit with lots of stored Autofill profiles.
// This test verifies when a profile is selected from the Autofill dictionary
// that consists of thousands of profiles, the form does not hang after being
// submitted.
IN_PROC_BROWSER_TEST_F(AutofillInteractiveTest,
                       MAYBE_FormFillLatencyAfterSubmit) {
  ASSERT_TRUE(test_server()->Start());

  std::vector<std::string> cities;
  cities.push_back("San Jose");
  cities.push_back("San Francisco");
  cities.push_back("Sacramento");
  cities.push_back("Los Angeles");

  std::vector<std::string> streets;
  streets.push_back("St");
  streets.push_back("Ave");
  streets.push_back("Ln");
  streets.push_back("Ct");

  const int kNumProfiles = 1500;
  base::Time start_time = base::Time::Now();
  std::vector<AutofillProfile> profiles;
  for (int i = 0; i < kNumProfiles; i++) {
    AutofillProfile profile;
    string16 name(base::IntToString16(i));
    string16 email(name + ASCIIToUTF16("@example.com"));
    string16 street = ASCIIToUTF16(
        base::IntToString(base::RandInt(0, 10000)) + " " +
        streets[base::RandInt(0, streets.size() - 1)]);
    string16 city = ASCIIToUTF16(cities[base::RandInt(0, cities.size() - 1)]);
    string16 zip(base::IntToString16(base::RandInt(0, 10000)));
    profile.SetRawInfo(NAME_FIRST, name);
    profile.SetRawInfo(EMAIL_ADDRESS, email);
    profile.SetRawInfo(ADDRESS_HOME_LINE1, street);
    profile.SetRawInfo(ADDRESS_HOME_CITY, city);
    profile.SetRawInfo(ADDRESS_HOME_STATE, WideToUTF16(L"CA"));
    profile.SetRawInfo(ADDRESS_HOME_ZIP, zip);
    profile.SetRawInfo(ADDRESS_HOME_COUNTRY, WideToUTF16(L"US"));
    profiles.push_back(profile);
  }
  SetProfiles(&profiles);
  // TODO(isherman): once we're sure this test doesn't timeout on any bots, this
  // can be removd.
  LOG(INFO) << "Created " << kNumProfiles << " profiles in " <<
               (base::Time::Now() - start_time).InSeconds() << " seconds.";

  GURL url = test_server()->GetURL(
      "files/autofill/latency_after_submit_test.html");
  ui_test_utils::NavigateToURL(browser(), url);
  PopulateForm("NAME_FIRST");

  content::WindowedNotificationObserver load_stop_observer(
      content::NOTIFICATION_LOAD_STOP,
      content::Source<content::NavigationController>(
          &browser()->tab_strip_model()->GetActiveWebContents()->
              GetController()));

  ASSERT_TRUE(content::ExecuteScript(
      GetRenderViewHost(),
      "document.getElementById('testform').submit();"));
  // This will ensure the test didn't hang.
  load_stop_observer.Wait();
}

// Test that Chrome doesn't crash when autocomplete is disabled while the user
// is interacting with the form.  This is a regression test for
// http://crbug.com/160476
IN_PROC_BROWSER_TEST_F(AutofillInteractiveTest,
                       DisableAutocompleteWhileFilling) {
  CreateTestProfile();

  // Load the test page.
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(browser(),
      GURL(std::string(kDataURIPrefix) + kTestFormString)));

  // Invoke Autofill: Start filling the first name field with "M" and wait for
  // the popup to be shown.
  FocusFirstNameField();
  SendKeyToPageAndWait(ui::VKEY_M);

  // Now that the popup with suggestions is showing, disable autocomplete for
  // the active field.
  ASSERT_TRUE(content::ExecuteScript(
      GetRenderViewHost(),
      "document.querySelector('input').autocomplete = 'off';"));

  // Press the down arrow to select the suggestion and attempt to preview the
  // autofilled form.
  content::NativeWebKeyboardEvent event;
  event.windowsKeyCode = ui::VKEY_DOWN;
  GetExternalDelegate()->keyboard_listener()->HandleKeyPressEvent(event);

  // Wait for any IPCs to complete by performing an action that generates an
  // IPC that's easy to wait for.  Chrome shouldn't crash.
  bool result = false;
  ASSERT_TRUE(content::ExecuteScriptAndExtractBool(
      GetRenderViewHost(),
      "var city = document.getElementById('city');"
      "city.onfocus = function() { domAutomationController.send(true); };"
      "city.focus()",
      &result));
  ASSERT_TRUE(result);
  SendKeyToPageAndWait(ui::VKEY_A);
}

}  // namespace autofill
