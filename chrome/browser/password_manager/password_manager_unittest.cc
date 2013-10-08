// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "base/message_loop/message_loop.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/password_manager/mock_password_store.h"
#include "chrome/browser/password_manager/password_manager.h"
#include "chrome/browser/password_manager/password_manager_delegate.h"
#include "chrome/browser/password_manager/password_store.h"
#include "chrome/browser/password_manager/password_store_factory.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "chrome/test/base/testing_pref_service_syncable.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/common/frame_navigate_params.h"
#include "content/public/test/test_browser_thread.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using content::PasswordForm;
using testing::_;
using testing::DoAll;
using testing::Exactly;
using testing::Return;
using testing::WithArg;

class MockPasswordManagerDelegate : public PasswordManagerDelegate {
 public:
  MOCK_METHOD1(FillPasswordForm, void(const autofill::PasswordFormFillData&));
  MOCK_METHOD1(AddSavePasswordInfoBarIfPermitted, void(PasswordFormManager*));
  MOCK_METHOD0(GetProfile, Profile*());
  MOCK_METHOD0(DidLastPageLoadEncounterSSLErrors, bool());
};

ACTION_P(InvokeConsumer, forms) {
  arg0->OnGetPasswordStoreResults(forms);
}

ACTION_P(SaveToScopedPtr, scoped) {
  scoped->reset(arg0);
}

class PasswordManagerTest : public ChromeRenderViewHostTestHarness {
 protected:
  virtual void SetUp() {
    ChromeRenderViewHostTestHarness::SetUp();
    store_ = static_cast<MockPasswordStore*>(
        PasswordStoreFactory::GetInstance()->SetTestingFactoryAndUse(
            profile(), MockPasswordStore::Build).get());

    EXPECT_CALL(delegate_, GetProfile()).WillRepeatedly(Return(profile()));
    PasswordManager::CreateForWebContentsAndDelegate(
        web_contents(), &delegate_);
    EXPECT_CALL(delegate_, DidLastPageLoadEncounterSSLErrors())
        .WillRepeatedly(Return(false));
  }

  virtual void TearDown() {
    store_ = NULL;
    ChromeRenderViewHostTestHarness::TearDown();
  }

  PasswordForm MakeSimpleForm() {
    PasswordForm form;
    form.origin = GURL("http://www.google.com/a/LoginAuth");
    form.action = GURL("http://www.google.com/a/Login");
    form.username_element = ASCIIToUTF16("Email");
    form.password_element = ASCIIToUTF16("Passwd");
    form.username_value = ASCIIToUTF16("google");
    form.password_value = ASCIIToUTF16("password");
    // Default to true so we only need to add tests in autocomplete=off cases.
    form.password_autocomplete_set = true;
    form.submit_element = ASCIIToUTF16("signIn");
    form.signon_realm = "http://www.google.com";
    return form;
  }

  PasswordManager* manager() {
    return PasswordManager::FromWebContents(web_contents());
  }

  scoped_refptr<MockPasswordStore> store_;
  MockPasswordManagerDelegate delegate_;  // Owned by manager_.
};

MATCHER_P(FormMatches, form, "") {
  return form.signon_realm == arg.signon_realm &&
         form.origin == arg.origin &&
         form.action == arg.action &&
         form.username_element == arg.username_element &&
         form.password_element == arg.password_element &&
         form.password_autocomplete_set ==
             arg.password_autocomplete_set &&
         form.submit_element == arg.submit_element;
}

TEST_F(PasswordManagerTest, FormSubmitEmptyStore) {
  // Test that observing a newly submitted form shows the save password bar.
  std::vector<PasswordForm*> result;  // Empty password store.
  EXPECT_CALL(delegate_, FillPasswordForm(_)).Times(Exactly(0));
  EXPECT_CALL(*store_.get(), GetLogins(_, _))
      .WillOnce(DoAll(WithArg<1>(InvokeConsumer(result)), Return(1)));
  std::vector<PasswordForm> observed;
  PasswordForm form(MakeSimpleForm());
  observed.push_back(form);
  manager()->OnPasswordFormsParsed(observed);  // The initial load.
  manager()->OnPasswordFormsRendered(observed);  // The initial layout.

  // And the form submit contract is to call ProvisionallySavePassword.
  manager()->ProvisionallySavePassword(form);

  scoped_ptr<PasswordFormManager> form_to_save;
  EXPECT_CALL(delegate_, AddSavePasswordInfoBarIfPermitted(_))
      .WillOnce(WithArg<0>(SaveToScopedPtr(&form_to_save)));

  // Now the password manager waits for the navigation to complete.
  observed.clear();
  manager()->OnPasswordFormsParsed(observed);  // The post-navigation load.
  manager()->OnPasswordFormsRendered(observed);  // The post-navigation layout.

  ASSERT_TRUE(form_to_save.get());
  EXPECT_CALL(*store_.get(), AddLogin(FormMatches(form)));

  // Simulate saving the form, as if the info bar was accepted.
  form_to_save->Save();
}

TEST_F(PasswordManagerTest, GeneratedPasswordFormSubmitEmptyStore) {
  // This test is the same FormSubmitEmptyStore, except that it simulates the
  // user generating the password through the browser.
  std::vector<PasswordForm*> result;  // Empty password store.
  EXPECT_CALL(delegate_, FillPasswordForm(_)).Times(Exactly(0));
  EXPECT_CALL(*store_.get(), GetLogins(_, _))
      .WillOnce(DoAll(WithArg<1>(InvokeConsumer(result)), Return(1)));
  std::vector<PasswordForm> observed;
  PasswordForm form(MakeSimpleForm());
  observed.push_back(form);
  manager()->OnPasswordFormsParsed(observed);  // The initial load.
  manager()->OnPasswordFormsRendered(observed);  // The initial layout.

  // Simulate the user generating the password and submitting the form.
  manager()->SetFormHasGeneratedPassword(form);
  manager()->ProvisionallySavePassword(form);

  // The user should not be presented with an infobar as they have already given
  // consent by using the generated password. The form should be saved once
  // navigation occurs.
  EXPECT_CALL(delegate_,
              AddSavePasswordInfoBarIfPermitted(_)).Times(Exactly(0));
  EXPECT_CALL(*store_.get(), AddLogin(FormMatches(form)));

  // Now the password manager waits for the navigation to complete.
  observed.clear();
  manager()->OnPasswordFormsParsed(observed);  // The post-navigation load.
  manager()->OnPasswordFormsRendered(observed);  // The post-navigation layout.
}

TEST_F(PasswordManagerTest, FormSubmitNoGoodMatch) {
  // Same as above, except with an existing form for the same signon realm,
  // but different origin.  Detailed cases like this are covered by
  // PasswordFormManagerTest.
  std::vector<PasswordForm*> result;
  PasswordForm* existing_different = new PasswordForm(MakeSimpleForm());
  existing_different->username_value = ASCIIToUTF16("google2");
  result.push_back(existing_different);
  EXPECT_CALL(delegate_, FillPasswordForm(_));
  EXPECT_CALL(*store_.get(), GetLogins(_, _))
      .WillOnce(DoAll(WithArg<1>(InvokeConsumer(result)), Return(1)));

  std::vector<PasswordForm> observed;
  PasswordForm form(MakeSimpleForm());
  observed.push_back(form);
  manager()->OnPasswordFormsParsed(observed);  // The initial load.
  manager()->OnPasswordFormsRendered(observed);  // The initial layout.
  manager()->ProvisionallySavePassword(form);

  // We still expect an add, since we didn't have a good match.
  scoped_ptr<PasswordFormManager> form_to_save;
  EXPECT_CALL(delegate_, AddSavePasswordInfoBarIfPermitted(_))
      .WillOnce(WithArg<0>(SaveToScopedPtr(&form_to_save)));

  // Now the password manager waits for the navigation to complete.
  observed.clear();
  manager()->OnPasswordFormsParsed(observed);  // The post-navigation load.
  manager()->OnPasswordFormsRendered(observed);  // The post-navigation layout.

  ASSERT_TRUE(form_to_save.get());
  EXPECT_CALL(*store_.get(), AddLogin(FormMatches(form)));

  // Simulate saving the form.
  form_to_save->Save();
}

TEST_F(PasswordManagerTest, FormSeenThenLeftPage) {
  std::vector<PasswordForm*> result;  // Empty password store.
  EXPECT_CALL(delegate_, FillPasswordForm(_)).Times(Exactly(0));
  EXPECT_CALL(*store_.get(), GetLogins(_, _))
      .WillOnce(DoAll(WithArg<1>(InvokeConsumer(result)), Return(1)));
  std::vector<PasswordForm> observed;
  PasswordForm form(MakeSimpleForm());
  observed.push_back(form);
  manager()->OnPasswordFormsParsed(observed);  // The initial load.
  manager()->OnPasswordFormsRendered(observed);  // The initial layout.

  PasswordForm empty_form(form);
  empty_form.username_value = string16();
  empty_form.password_value = string16();
  content::LoadCommittedDetails details;
  content::FrameNavigateParams params;
  params.password_form = empty_form;
  manager()->DidNavigateAnyFrame(details, params);

  // No expected calls.
  EXPECT_CALL(delegate_, AddSavePasswordInfoBarIfPermitted(_)).Times(0);
  observed.clear();
  manager()->OnPasswordFormsParsed(observed);  // The post-navigation load.
  manager()->OnPasswordFormsRendered(observed);  // The post-navigation layout.
}

TEST_F(PasswordManagerTest, FormSubmitAfterNavigateSubframe) {
  // Test that navigating a subframe does not prevent us from showing the save
  // password infobar.
  std::vector<PasswordForm*> result;  // Empty password store.
  EXPECT_CALL(delegate_, FillPasswordForm(_)).Times(Exactly(0));
  EXPECT_CALL(*store_.get(), GetLogins(_, _))
      .WillOnce(DoAll(WithArg<1>(InvokeConsumer(result)), Return(1)));
  std::vector<PasswordForm> observed;
  PasswordForm form(MakeSimpleForm());
  observed.push_back(form);
  manager()->OnPasswordFormsParsed(observed);  // The initial load.
  manager()->OnPasswordFormsRendered(observed);  // The initial layout.

  // Simulate navigating a sub-frame.
  content::LoadCommittedDetails details;
  details.is_main_frame = false;
  content::FrameNavigateParams params;
  manager()->DidNavigateAnyFrame(details, params);

  // Simulate navigating the real page.
  details.is_main_frame = true;
  params.password_form = form;
  manager()->DidNavigateAnyFrame(details, params);

  // Now the password manager waits for the navigation to complete.
  scoped_ptr<PasswordFormManager> form_to_save;
  EXPECT_CALL(delegate_, AddSavePasswordInfoBarIfPermitted(_))
      .WillOnce(WithArg<0>(SaveToScopedPtr(&form_to_save)));

  observed.clear();
  manager()->OnPasswordFormsParsed(observed);  // The post-navigation load.
  manager()->OnPasswordFormsRendered(observed);  // The post-navigation layout.

  ASSERT_FALSE(NULL == form_to_save.get());
  EXPECT_CALL(*store_.get(), AddLogin(FormMatches(form)));

  // Simulate saving the form, as if the info bar was accepted.
  form_to_save->Save();
}

// This test verifies a fix for http://crbug.com/236673
TEST_F(PasswordManagerTest, FormSubmitWithFormOnPreviousPage) {
  std::vector<PasswordForm*> result;  // Empty password store.
  EXPECT_CALL(delegate_, FillPasswordForm(_)).Times(Exactly(0));
  EXPECT_CALL(*store_.get(), GetLogins(_, _))
      .WillRepeatedly(DoAll(WithArg<1>(InvokeConsumer(result)), Return(1)));
  PasswordForm first_form(MakeSimpleForm());
  first_form.origin = GURL("http://www.nytimes.com/");
  first_form.action = GURL("https://myaccount.nytimes.com/auth/login");
  first_form.signon_realm = "http://www.nytimes.com/";
  PasswordForm second_form(MakeSimpleForm());
  second_form.origin = GURL("https://myaccount.nytimes.com/auth/login");
  second_form.action = GURL("https://myaccount.nytimes.com/auth/login");
  second_form.signon_realm = "https://myaccount.nytimes.com/";

  // Pretend that the form is hidden on the first page.
  std::vector<PasswordForm> observed;
  observed.push_back(first_form);
  manager()->OnPasswordFormsParsed(observed);
  observed.clear();
  manager()->OnPasswordFormsRendered(observed);

  // Now navigate to a second page.
  content::LoadCommittedDetails details;
  details.is_main_frame = true;
  content::FrameNavigateParams params;
  manager()->DidNavigateAnyFrame(details, params);

  // This page contains a form with the same markup, but on a different
  // URL.
  observed.push_back(second_form);
  manager()->OnPasswordFormsParsed(observed);
  manager()->OnPasswordFormsRendered(observed);

  // Now submit this form
  params.password_form = second_form;
  manager()->DidNavigateAnyFrame(details, params);

  // Navigation after form submit.
  scoped_ptr<PasswordFormManager> form_to_save;
  EXPECT_CALL(delegate_, AddSavePasswordInfoBarIfPermitted(_))
      .WillOnce(WithArg<0>(SaveToScopedPtr(&form_to_save)));
  observed.clear();
  manager()->OnPasswordFormsParsed(observed);
  manager()->OnPasswordFormsRendered(observed);

  // Make sure that the saved form matches the second form, not the first.
  ASSERT_TRUE(form_to_save.get());
  EXPECT_CALL(*store_.get(), AddLogin(FormMatches(second_form)));

  // Simulate saving the form, as if the info bar was accepted.
  form_to_save->Save();
}

TEST_F(PasswordManagerTest, FormSubmitFailedLogin) {
  std::vector<PasswordForm*> result;  // Empty password store.
  EXPECT_CALL(delegate_, FillPasswordForm(_)).Times(Exactly(0));
  EXPECT_CALL(*store_.get(), GetLogins(_, _))
      .WillRepeatedly(DoAll(WithArg<1>(InvokeConsumer(result)), Return(1)));
  std::vector<PasswordForm> observed;
  PasswordForm form(MakeSimpleForm());
  observed.push_back(form);
  manager()->OnPasswordFormsParsed(observed);  // The initial load.
  manager()->OnPasswordFormsRendered(observed);  // The initial layout.

  manager()->ProvisionallySavePassword(form);

  // The form reappears, and is visible in the layout:
  // No expected calls to the PasswordStore...
  manager()->OnPasswordFormsParsed(observed);
  manager()->OnPasswordFormsRendered(observed);
}

TEST_F(PasswordManagerTest, FormSubmitInvisibleLogin) {
  // Tests fix of issue 28911: if the login form reappears on the subsequent
  // page, but is invisible, it shouldn't count as a failed login.
  std::vector<PasswordForm*> result;  // Empty password store.
  EXPECT_CALL(delegate_, FillPasswordForm(_)).Times(Exactly(0));
  EXPECT_CALL(*store_.get(), GetLogins(_, _))
      .WillRepeatedly(DoAll(WithArg<1>(InvokeConsumer(result)), Return(1)));
  std::vector<PasswordForm> observed;
  PasswordForm form(MakeSimpleForm());
  observed.push_back(form);
  manager()->OnPasswordFormsParsed(observed);  // The initial load.
  manager()->OnPasswordFormsRendered(observed);  // The initial layout.

  manager()->ProvisionallySavePassword(form);

  // Expect info bar to appear:
  scoped_ptr<PasswordFormManager> form_to_save;
  EXPECT_CALL(delegate_, AddSavePasswordInfoBarIfPermitted(_))
      .WillOnce(WithArg<0>(SaveToScopedPtr(&form_to_save)));

  // The form reappears, but is not visible in the layout:
  manager()->OnPasswordFormsParsed(observed);
  observed.clear();
  manager()->OnPasswordFormsRendered(observed);

  ASSERT_TRUE(form_to_save.get());
  EXPECT_CALL(*store_.get(), AddLogin(FormMatches(form)));

  // Simulate saving the form.
  form_to_save->Save();
}

TEST_F(PasswordManagerTest, InitiallyInvisibleForm) {
  // Make sure an invisible login form still gets autofilled.
  std::vector<PasswordForm*> result;
  PasswordForm* existing = new PasswordForm(MakeSimpleForm());
  result.push_back(existing);
  EXPECT_CALL(delegate_, FillPasswordForm(_));
  EXPECT_CALL(*store_.get(), GetLogins(_, _))
      .WillRepeatedly(DoAll(WithArg<1>(InvokeConsumer(result)), Return(1)));
  std::vector<PasswordForm> observed;
  PasswordForm form(MakeSimpleForm());
  observed.push_back(form);
  manager()->OnPasswordFormsParsed(observed);  // The initial load.
  observed.clear();
  manager()->OnPasswordFormsRendered(observed);  // The initial layout.

  manager()->OnPasswordFormsParsed(observed);  // The post-navigation load.
  manager()->OnPasswordFormsRendered(observed);  // The post-navigation layout.
}

TEST_F(PasswordManagerTest, SavingDependsOnManagerEnabledPreference) {
  // Test that saving passwords depends on the password manager enabled
  // preference.
  TestingPrefServiceSyncable* prefService = profile()->GetTestingPrefService();
  prefService->SetUserPref(prefs::kPasswordManagerEnabled,
                           Value::CreateBooleanValue(true));
  EXPECT_TRUE(manager()->IsSavingEnabled());
  prefService->SetUserPref(prefs::kPasswordManagerEnabled,
                           Value::CreateBooleanValue(false));
  EXPECT_FALSE(manager()->IsSavingEnabled());
}

TEST_F(PasswordManagerTest, FillPasswordsOnDisabledManager) {
  // Test fix for issue 158296: Passwords must be filled even if the password
  // manager is disabled.
  std::vector<PasswordForm*> result;
  PasswordForm* existing = new PasswordForm(MakeSimpleForm());
  result.push_back(existing);
  TestingPrefServiceSyncable* prefService = profile()->GetTestingPrefService();
  prefService->SetUserPref(prefs::kPasswordManagerEnabled,
                           Value::CreateBooleanValue(false));
  EXPECT_CALL(delegate_, FillPasswordForm(_));
  EXPECT_CALL(*store_.get(), GetLogins(_, _))
      .WillRepeatedly(DoAll(WithArg<1>(InvokeConsumer(result)), Return(1)));
  std::vector<PasswordForm> observed;
  PasswordForm form(MakeSimpleForm());
  observed.push_back(form);
  manager()->OnPasswordFormsParsed(observed);
}

TEST_F(PasswordManagerTest, FormNotSavedAutocompleteOff) {
  // Test password form with non-generated password will not be saved if
  // autocomplete=off.
  std::vector<PasswordForm*> result;  // Empty password store.
  EXPECT_CALL(delegate_, FillPasswordForm(_)).Times(Exactly(0));
  EXPECT_CALL(*store_.get(), GetLogins(_, _))
      .WillOnce(DoAll(WithArg<1>(InvokeConsumer(result)), Return(1)));
  std::vector<PasswordForm> observed;
  PasswordForm form(MakeSimpleForm());
  form.password_autocomplete_set = false;
  observed.push_back(form);
  manager()->OnPasswordFormsParsed(observed);  // The initial load.
  manager()->OnPasswordFormsRendered(observed);  // The initial layout.

  // And the form submit contract is to call ProvisionallySavePassword.
  manager()->ProvisionallySavePassword(form);

  // Password form should not be saved.
  EXPECT_CALL(delegate_,
              AddSavePasswordInfoBarIfPermitted(_)).Times(Exactly(0));
  EXPECT_CALL(*store_.get(), AddLogin(FormMatches(form))).Times(Exactly(0));

  // Now the password manager waits for the navigation to complete.
  observed.clear();
  manager()->OnPasswordFormsParsed(observed);  // The post-navigation load.
  manager()->OnPasswordFormsRendered(observed);  // The post-navigation layout.
}

TEST_F(PasswordManagerTest, GeneratedPasswordFormSavedAutocompleteOff) {
  // Test password form with generated password will still be saved if
  // autocomplete=off.
  std::vector<PasswordForm*> result;  // Empty password store.
  EXPECT_CALL(delegate_, FillPasswordForm(_)).Times(Exactly(0));
  EXPECT_CALL(*store_.get(), GetLogins(_, _))
      .WillOnce(DoAll(WithArg<1>(InvokeConsumer(result)), Return(1)));
  std::vector<PasswordForm> observed;
  PasswordForm form(MakeSimpleForm());
  form.password_autocomplete_set = false;
  observed.push_back(form);
  manager()->OnPasswordFormsParsed(observed);  // The initial load.
  manager()->OnPasswordFormsRendered(observed);  // The initial layout.

  // Simulate the user generating the password and submitting the form.
  manager()->SetFormHasGeneratedPassword(form);
  manager()->ProvisionallySavePassword(form);

  // The user should not be presented with an infobar as they have already given
  // consent by using the generated password. The form should be saved once
  // navigation occurs.
  EXPECT_CALL(delegate_,
              AddSavePasswordInfoBarIfPermitted(_)).Times(Exactly(0));
  EXPECT_CALL(*store_.get(), AddLogin(FormMatches(form)));

  // Now the password manager waits for the navigation to complete.
  observed.clear();
  manager()->OnPasswordFormsParsed(observed);  // The post-navigation load.
  manager()->OnPasswordFormsRendered(observed);  // The post-navigation layout.
}
