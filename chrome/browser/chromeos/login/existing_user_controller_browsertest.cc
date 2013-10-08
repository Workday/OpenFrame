// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/callback.h"
#include "base/command_line.h"
#include "base/location.h"
#include "base/run_loop.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/chromeos/login/authenticator.h"
#include "chrome/browser/chromeos/login/existing_user_controller.h"
#include "chrome/browser/chromeos/login/helper.h"
#include "chrome/browser/chromeos/login/mock_authenticator.h"
#include "chrome/browser/chromeos/login/mock_login_display.h"
#include "chrome/browser/chromeos/login/mock_login_display_host.h"
#include "chrome/browser/chromeos/login/mock_login_utils.h"
#include "chrome/browser/chromeos/login/mock_url_fetchers.h"
#include "chrome/browser/chromeos/login/mock_user_manager.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/login/wizard_controller.h"
#include "chrome/browser/chromeos/policy/device_local_account.h"
#include "chrome/browser/chromeos/policy/device_local_account_policy_service.h"
#include "chrome/browser/chromeos/policy/device_policy_cros_browser_test.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/browser/chromeos/settings/cros_settings_names.h"
#include "chrome/browser/policy/browser_policy_connector.h"
#include "chrome/browser/policy/cloud/cloud_policy_constants.h"
#include "chrome/browser/policy/cloud/cloud_policy_core.h"
#include "chrome/browser/policy/cloud/cloud_policy_store.h"
#include "chrome/browser/policy/cloud/mock_cloud_policy_store.h"
#include "chrome/browser/policy/cloud/policy_builder.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/base/ui_test_utils.h"
#include "chromeos/chromeos_switches.h"
#include "chromeos/dbus/fake_session_manager_client.h"
#include "content/public/test/mock_notification_observer.h"
#include "content/public/test/test_utils.h"
#include "google_apis/gaia/mock_url_fetcher_factory.h"
#include "grit/generated_resources.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/l10n/l10n_util.h"

using ::testing::AnyNumber;
using ::testing::Invoke;
using ::testing::InvokeWithoutArgs;
using ::testing::Return;
using ::testing::ReturnNull;
using ::testing::Sequence;
using ::testing::WithArg;
using ::testing::_;

namespace em = enterprise_management;

namespace chromeos {

namespace {

const char kUsername[] = "test_user@gmail.com";
const char kNewUsername[] = "test_new_user@gmail.com";
const char kPassword[] = "test_password";

const char kPublicSessionAccountId[] = "public_session_user@localhost";
const int kAutoLoginNoDelay = 0;
const int kAutoLoginShortDelay = 1;
const int kAutoLoginLongDelay = 10000;


ACTION_P2(CreateAuthenticator, username, password) {
  return new MockAuthenticator(arg0, username, password);
}

}  // namespace

class ExistingUserControllerTest : public policy::DevicePolicyCrosBrowserTest,
                                   public testing::WithParamInterface<bool> {
 protected:
  ExistingUserControllerTest()
      : mock_login_display_(NULL), mock_user_manager_(NULL) {}

  ExistingUserController* existing_user_controller() {
    return ExistingUserController::current_controller();
  }

  const ExistingUserController* existing_user_controller() const {
    return ExistingUserController::current_controller();
  }

  virtual void SetUpInProcessBrowserTestFixture() OVERRIDE {
    SetUpSessionManager();

    DevicePolicyCrosBrowserTest::SetUpInProcessBrowserTestFixture();

    mock_login_utils_ = new MockLoginUtils();
    LoginUtils::Set(mock_login_utils_);
    EXPECT_CALL(*mock_login_utils_, StopBackgroundFetchers())
        .Times(AnyNumber());
    EXPECT_CALL(*mock_login_utils_, DelegateDeleted(_))
        .Times(1);

    mock_login_display_host_.reset(new MockLoginDisplayHost());
    mock_login_display_ = new MockLoginDisplay();
    SetUpLoginDisplay();
  }

  virtual void SetUpSessionManager() {
  }

  virtual void SetUpLoginDisplay() {
    EXPECT_CALL(*mock_login_display_host_.get(), CreateLoginDisplay(_))
        .Times(1)
        .WillOnce(Return(mock_login_display_));
    EXPECT_CALL(*mock_login_display_host_.get(), GetNativeWindow())
        .Times(1)
        .WillOnce(ReturnNull());
    EXPECT_CALL(*mock_login_display_host_.get(), OnPreferencesChanged())
        .Times(1);
    EXPECT_CALL(*mock_login_display_, Init(_, false, true, true))
        .Times(1);
  }

  virtual void SetUpCommandLine(CommandLine* command_line) OVERRIDE {
    command_line->AppendSwitch(switches::kLoginManager);
    if (GetParam())
      command_line->AppendSwitch(::switches::kMultiProfiles);
  }

  virtual void SetUpUserManager() {
    // Replace the UserManager singleton with a mock.
    mock_user_manager_ = new MockUserManager;
    user_manager_enabler_.reset(
        new ScopedUserManagerEnabler(mock_user_manager_));
    EXPECT_CALL(*mock_user_manager_, IsKnownUser(kUsername))
        .Times(AnyNumber())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*mock_user_manager_, IsKnownUser(kNewUsername))
        .Times(AnyNumber())
        .WillRepeatedly(Return(false));
    EXPECT_CALL(*mock_user_manager_, IsUserLoggedIn())
        .Times(AnyNumber())
        .WillRepeatedly(Return(false));
    EXPECT_CALL(*mock_user_manager_, IsLoggedInAsGuest())
        .Times(AnyNumber())
        .WillRepeatedly(Return(false));
    EXPECT_CALL(*mock_user_manager_, IsLoggedInAsDemoUser())
        .Times(AnyNumber())
        .WillRepeatedly(Return(false));
    EXPECT_CALL(*mock_user_manager_, IsLoggedInAsPublicAccount())
        .Times(AnyNumber())
        .WillRepeatedly(Return(false));
    EXPECT_CALL(*mock_user_manager_, IsSessionStarted())
        .Times(AnyNumber())
        .WillRepeatedly(Return(false));
    EXPECT_CALL(*mock_user_manager_, IsCurrentUserNew())
        .Times(AnyNumber())
        .WillRepeatedly(Return(false));
    EXPECT_CALL(*mock_user_manager_, Shutdown())
        .Times(1);
  }

  virtual void SetUpOnMainThread() OVERRIDE {
    SetUpUserManager();
    testing_profile_.reset(new TestingProfile());
    existing_user_controller_.reset(
        new ExistingUserController(mock_login_display_host_.get()));
    ASSERT_EQ(existing_user_controller(), existing_user_controller_.get());
    existing_user_controller_->Init(UserList());
    profile_prepared_cb_ =
        base::Bind(&ExistingUserController::OnProfilePrepared,
                   base::Unretained(existing_user_controller()),
                   testing_profile_.get());
  }

  virtual void CleanUpOnMainThread() OVERRIDE {
    // ExistingUserController must be deleted before the thread is cleaned up:
    // If there is an outstanding login attempt when ExistingUserController is
    // deleted, its LoginPerformer instance will be deleted, which in turn
    // deletes its OnlineAttemptHost instance.  However, OnlineAttemptHost must
    // be deleted on the UI thread.
    existing_user_controller_.reset();
    CrosInProcessBrowserTest::CleanUpOnMainThread();
    testing_profile_.reset(NULL);
    user_manager_enabler_.reset();
  }

  // ExistingUserController private member accessors.
  base::OneShotTimer<ExistingUserController>* auto_login_timer() {
    return existing_user_controller()->auto_login_timer_.get();
  }

  const std::string& auto_login_username() const {
    return existing_user_controller()->public_session_auto_login_username_;
  }

  int auto_login_delay() const {
    return existing_user_controller()->public_session_auto_login_delay_;
  }

  bool is_login_in_progress() const {
    return existing_user_controller()->is_login_in_progress_;
  }

  scoped_ptr<ExistingUserController> existing_user_controller_;

  // |mock_login_display_| is owned by the ExistingUserController, which calls
  // CreateLoginDisplay() on the |mock_login_display_host_| to get it.
  MockLoginDisplay* mock_login_display_;
  scoped_ptr<MockLoginDisplayHost> mock_login_display_host_;

  // Owned by LoginUtilsWrapper.
  MockLoginUtils* mock_login_utils_;

  MockUserManager* mock_user_manager_;  // Not owned.
  scoped_ptr<ScopedUserManagerEnabler> user_manager_enabler_;

  scoped_ptr<TestingProfile> testing_profile_;

  // Mock URLFetcher.
  MockURLFetcherFactory<SuccessFetcher> factory_;

  base::Callback<void(void)> profile_prepared_cb_;

 private:
  DISALLOW_COPY_AND_ASSIGN(ExistingUserControllerTest);
};

IN_PROC_BROWSER_TEST_P(ExistingUserControllerTest, ExistingUserLogin) {
  // This is disabled twice: once right after signin but before checking for
  // auto-enrollment, and again after doing an ownership status check.
  EXPECT_CALL(*mock_login_display_, SetUIEnabled(false))
      .Times(2);
  EXPECT_CALL(*mock_login_utils_, CreateAuthenticator(_))
      .Times(1)
      .WillOnce(WithArg<0>(CreateAuthenticator(kUsername, kPassword)));
  EXPECT_CALL(*mock_login_utils_,
              PrepareProfile(UserContext(kUsername, kPassword, "", kUsername),
                             _, _, _, _, _))
      .Times(1)
      .WillOnce(InvokeWithoutArgs(&profile_prepared_cb_,
                                  &base::Callback<void(void)>::Run));
  EXPECT_CALL(*mock_login_utils_,
              DoBrowserLaunch(testing_profile_.get(),
                              mock_login_display_host_.get()))
      .Times(1);
  EXPECT_CALL(*mock_login_display_, OnLoginSuccess(kUsername))
      .Times(1);
  EXPECT_CALL(*mock_login_display_, SetUIEnabled(true))
      .Times(1);
  EXPECT_CALL(*mock_login_display_, OnFadeOut())
      .Times(1);
  EXPECT_CALL(*mock_login_display_host_,
              StartWizardPtr(WizardController::kTermsOfServiceScreenName, NULL))
      .Times(0);
  EXPECT_CALL(*mock_user_manager_, IsCurrentUserNew())
      .Times(AnyNumber())
      .WillRepeatedly(Return(false));
  existing_user_controller()->Login(UserContext(kUsername, kPassword, ""));
  content::RunAllPendingInMessageLoop();
}

IN_PROC_BROWSER_TEST_P(ExistingUserControllerTest, AutoEnrollAfterSignIn) {
  EXPECT_CALL(*mock_login_display_host_,
              StartWizardPtr(WizardController::kEnrollmentScreenName,
                             _))
      .Times(1);
  EXPECT_CALL(*mock_login_display_, OnFadeOut())
      .Times(1);
  EXPECT_CALL(*mock_login_display_host_.get(), OnCompleteLogin())
      .Times(1);
  EXPECT_CALL(*mock_user_manager_, IsCurrentUserNew())
      .Times(AnyNumber())
      .WillRepeatedly(Return(false));
  // The order of these expected calls matters: the UI if first disabled
  // during the login sequence, and is enabled again for the enrollment screen.
  Sequence uiEnabledSequence;
  EXPECT_CALL(*mock_login_display_, SetUIEnabled(false))
      .Times(1)
      .InSequence(uiEnabledSequence);
  EXPECT_CALL(*mock_login_display_, SetUIEnabled(true))
      .Times(1)
      .InSequence(uiEnabledSequence);
  existing_user_controller()->DoAutoEnrollment();
  existing_user_controller()->CompleteLogin(
      UserContext(kUsername, kPassword, ""));
  content::RunAllPendingInMessageLoop();
}

IN_PROC_BROWSER_TEST_P(ExistingUserControllerTest,
                       NewUserDontAutoEnrollAfterSignIn) {
  EXPECT_CALL(*mock_login_display_host_,
              StartWizardPtr(WizardController::kEnrollmentScreenName,
                             _))
      .Times(0);
  EXPECT_CALL(*mock_login_display_host_,
              StartWizardPtr(WizardController::kTermsOfServiceScreenName,
                             NULL))
      .Times(1);
  EXPECT_CALL(*mock_login_utils_, CreateAuthenticator(_))
      .Times(1)
      .WillOnce(WithArg<0>(CreateAuthenticator(kNewUsername, kPassword)));
  EXPECT_CALL(*mock_login_utils_,
              PrepareProfile(UserContext(kNewUsername,
                                         kPassword,
                                         std::string(),
                                         kNewUsername),
                             _, _, _, _, _))
      .Times(1)
      .WillOnce(InvokeWithoutArgs(&profile_prepared_cb_,
                                  &base::Callback<void(void)>::Run));
  EXPECT_CALL(*mock_login_display_, OnLoginSuccess(kNewUsername))
      .Times(1);
  EXPECT_CALL(*mock_login_display_, OnFadeOut())
      .Times(1);
  EXPECT_CALL(*mock_login_display_host_.get(), OnCompleteLogin())
      .Times(1);
  EXPECT_CALL(*mock_user_manager_, IsCurrentUserNew())
      .Times(AnyNumber())
      .WillRepeatedly(Return(true));

  // The order of these expected calls matters: the UI if first disabled
  // during the login sequence, and is enabled again after login completion.
  Sequence uiEnabledSequence;
  // This is disabled twice: once right after signin but before checking for
  // auto-enrollment, and again after doing an ownership status check.
  EXPECT_CALL(*mock_login_display_, SetUIEnabled(false))
      .Times(2)
      .InSequence(uiEnabledSequence);
  EXPECT_CALL(*mock_login_display_, SetUIEnabled(true))
      .Times(1)
      .InSequence(uiEnabledSequence);

  existing_user_controller()->CompleteLogin(
      UserContext(kNewUsername, kPassword, ""));
  content::RunAllPendingInMessageLoop();
}

MATCHER_P(HasDetails, expected, "") {
  return expected == *content::Details<const std::string>(arg).ptr();
}

class ExistingUserControllerPublicSessionTest
    : public ExistingUserControllerTest {
 protected:
  ExistingUserControllerPublicSessionTest()
      : public_session_user_id_(policy::GenerateDeviceLocalAccountUserId(
            kPublicSessionAccountId,
            policy::DeviceLocalAccount::TYPE_PUBLIC_SESSION)) {
  }

  virtual void SetUpOnMainThread() OVERRIDE {
    ExistingUserControllerTest::SetUpOnMainThread();

    // Wait for the public session user to be created.
    if (!chromeos::UserManager::Get()->IsKnownUser(public_session_user_id_)) {
      content::WindowedNotificationObserver(
          chrome::NOTIFICATION_USER_LIST_CHANGED,
          base::Bind(&chromeos::UserManager::IsKnownUser,
                     base::Unretained(chromeos::UserManager::Get()),
                     public_session_user_id_)).Wait();
    }

    // Wait for the device local account policy to be installed.
    policy::CloudPolicyStore* store = TestingBrowserProcess::GetGlobal()->
        browser_policy_connector()->GetDeviceLocalAccountPolicyService()->
        GetBrokerForUser(public_session_user_id_)->core()->store();
    if (!store->has_policy()) {
      policy::MockCloudPolicyStoreObserver observer;

      base::RunLoop loop;
      store->AddObserver(&observer);
      EXPECT_CALL(observer, OnStoreLoaded(store))
          .Times(1)
          .WillOnce(InvokeWithoutArgs(&loop, &base::RunLoop::Quit));
      loop.Run();
      store->RemoveObserver(&observer);
    }
  }

  virtual void SetUpSessionManager() OVERRIDE {
    InstallOwnerKey();

    // Setup the device policy.
    em::ChromeDeviceSettingsProto& proto(device_policy()->payload());
    em::DeviceLocalAccountInfoProto* account =
        proto.mutable_device_local_accounts()->add_account();
    account->set_account_id(kPublicSessionAccountId);
    account->set_type(
        em::DeviceLocalAccountInfoProto::ACCOUNT_TYPE_PUBLIC_SESSION);
    RefreshDevicePolicy();

    // Setup the device local account policy.
    policy::UserPolicyBuilder device_local_account_policy;
    device_local_account_policy.policy_data().set_username(
        kPublicSessionAccountId);
    device_local_account_policy.policy_data().set_policy_type(
        policy::dm_protocol::kChromePublicAccountPolicyType);
    device_local_account_policy.policy_data().set_settings_entity_id(
        kPublicSessionAccountId);
    device_local_account_policy.Build();
    session_manager_client()->set_device_local_account_policy(
        kPublicSessionAccountId,
        device_local_account_policy.GetBlob());
  }

  virtual void SetUpLoginDisplay() OVERRIDE {
    EXPECT_CALL(*mock_login_display_host_.get(), CreateLoginDisplay(_))
        .Times(1)
        .WillOnce(Return(mock_login_display_));
    EXPECT_CALL(*mock_login_display_host_.get(), GetNativeWindow())
      .Times(AnyNumber())
      .WillRepeatedly(ReturnNull());
    EXPECT_CALL(*mock_login_display_host_.get(), OnPreferencesChanged())
      .Times(AnyNumber());
    EXPECT_CALL(*mock_login_display_, Init(_, _, _, _))
      .Times(AnyNumber());
  }

  virtual void SetUpUserManager() OVERRIDE {
  }

  void ExpectSuccessfulLogin(const std::string& username,
                             const std::string& password) {
    EXPECT_CALL(*mock_login_display_, SetUIEnabled(false))
        .Times(AnyNumber());
    EXPECT_CALL(*mock_login_utils_, CreateAuthenticator(_))
        .Times(1)
        .WillOnce(WithArg<0>(CreateAuthenticator(username, password)));
    EXPECT_CALL(*mock_login_utils_,
                PrepareProfile(UserContext(username, password, "", username),
                               _, _, _, _, _))
        .Times(1)
        .WillOnce(InvokeWithoutArgs(&profile_prepared_cb_,
                                    &base::Callback<void(void)>::Run));
    EXPECT_CALL(*mock_login_utils_,
                DoBrowserLaunch(testing_profile_.get(),
                                mock_login_display_host_.get()))
        .Times(1);
    EXPECT_CALL(*mock_login_display_, OnLoginSuccess(username))
        .Times(1);
    EXPECT_CALL(*mock_login_display_, SetUIEnabled(true))
        .Times(1);
    EXPECT_CALL(*mock_login_display_, OnFadeOut())
        .Times(1);
    EXPECT_CALL(*mock_login_display_host_,
                StartWizardPtr(WizardController::kTermsOfServiceScreenName,
                               NULL))
        .Times(0);
  }

  scoped_ptr<base::RunLoop> CreateSettingsObserverRunLoop(
      content::MockNotificationObserver& observer, const char* setting) {
    base::RunLoop* loop = new base::RunLoop;
    EXPECT_CALL(observer, Observe(chrome::NOTIFICATION_SYSTEM_SETTING_CHANGED,
                                  _, HasDetails(setting)))
        .Times(1)
        .WillOnce(InvokeWithoutArgs(loop, &base::RunLoop::Quit));
    CrosSettings::Get()->AddSettingsObserver(setting, &observer);
    return make_scoped_ptr(loop);
  }

  void SetAutoLoginPolicy(const std::string& username, int delay) {
    // Wait until ExistingUserController has finished auto-login
    // configuration by observing the same settings that trigger
    // ConfigurePublicSessionAutoLogin.
    content::MockNotificationObserver observer;

    em::ChromeDeviceSettingsProto& proto(device_policy()->payload());

    // If both settings have changed we need to wait for both to
    // propagate, so check the new values against the old ones.
    scoped_ptr<base::RunLoop> runner1;
    if (!proto.has_device_local_accounts() ||
        !proto.device_local_accounts().has_auto_login_id() ||
        proto.device_local_accounts().auto_login_id() != username) {
      runner1 = CreateSettingsObserverRunLoop(
          observer, kAccountsPrefDeviceLocalAccountAutoLoginId);
    }
    scoped_ptr<base::RunLoop> runner2;
    if (!proto.has_device_local_accounts() ||
        !proto.device_local_accounts().has_auto_login_delay() ||
        proto.device_local_accounts().auto_login_delay() != delay) {
      runner2 = CreateSettingsObserverRunLoop(
          observer, kAccountsPrefDeviceLocalAccountAutoLoginDelay);
    }

    // Update the policy.
    proto.mutable_device_local_accounts()->set_auto_login_id(username);
    proto.mutable_device_local_accounts()->set_auto_login_delay(delay);
    RefreshDevicePolicy();

    // Wait for ExistingUserController to read the updated settings.
    if (runner1)
      runner1->Run();
    if (runner2)
      runner2->Run();

    // Clean up.
    CrosSettings::Get()->RemoveSettingsObserver(
        kAccountsPrefDeviceLocalAccountAutoLoginId,
        &observer);
    CrosSettings::Get()->RemoveSettingsObserver(
        kAccountsPrefDeviceLocalAccountAutoLoginDelay,
        &observer);
  }

  void ConfigureAutoLogin() {
    existing_user_controller()->ConfigurePublicSessionAutoLogin();
  }

  void FireAutoLogin() {
    existing_user_controller()->OnPublicSessionAutoLoginTimerFire();
  }

  const std::string public_session_user_id_;

 private:
  DISALLOW_COPY_AND_ASSIGN(ExistingUserControllerPublicSessionTest);
};

IN_PROC_BROWSER_TEST_P(ExistingUserControllerPublicSessionTest,
                       ConfigureAutoLoginUsingPolicy) {
  existing_user_controller()->OnSigninScreenReady();
  EXPECT_EQ("", auto_login_username());
  EXPECT_EQ(0, auto_login_delay());
  EXPECT_FALSE(auto_login_timer());

  // Set the policy.
  SetAutoLoginPolicy(kPublicSessionAccountId, kAutoLoginLongDelay);
  EXPECT_EQ(public_session_user_id_, auto_login_username());
  EXPECT_EQ(kAutoLoginLongDelay, auto_login_delay());
  ASSERT_TRUE(auto_login_timer());
  EXPECT_TRUE(auto_login_timer()->IsRunning());

  // Unset the policy.
  SetAutoLoginPolicy("", 0);
  EXPECT_EQ("", auto_login_username());
  EXPECT_EQ(0, auto_login_delay());
  ASSERT_TRUE(auto_login_timer());
  EXPECT_FALSE(auto_login_timer()->IsRunning());
}

IN_PROC_BROWSER_TEST_P(ExistingUserControllerPublicSessionTest,
                       AutoLoginNoDelay) {
  // Set up mocks to check login success.
  ExpectSuccessfulLogin(public_session_user_id_, "");
  existing_user_controller()->OnSigninScreenReady();

  // Start auto-login and wait for login tasks to complete.
  SetAutoLoginPolicy(kPublicSessionAccountId, kAutoLoginNoDelay);
  content::RunAllPendingInMessageLoop();
}

IN_PROC_BROWSER_TEST_P(ExistingUserControllerPublicSessionTest,
                       AutoLoginShortDelay) {
  // Set up mocks to check login success.
  ExpectSuccessfulLogin(public_session_user_id_, "");
  existing_user_controller()->OnSigninScreenReady();
  SetAutoLoginPolicy(kPublicSessionAccountId, kAutoLoginShortDelay);
  ASSERT_TRUE(auto_login_timer());
  // Don't assert that timer is running: with the short delay sometimes
  // the trigger happens before the assert.  We've already tested that
  // the timer starts when it should.

  // Wait for the timer to fire.
  base::RunLoop runner;
  base::OneShotTimer<base::RunLoop> timer;
  timer.Start(FROM_HERE,
              base::TimeDelta::FromMilliseconds(kAutoLoginShortDelay + 1),
              runner.QuitClosure());
  runner.Run();

  // Wait for login tasks to complete.
  content::RunAllPendingInMessageLoop();
}

IN_PROC_BROWSER_TEST_P(ExistingUserControllerPublicSessionTest,
                       LoginStopsAutoLogin) {
  // Set up mocks to check login success.
  ExpectSuccessfulLogin(kUsername, kPassword);

  existing_user_controller()->OnSigninScreenReady();
  SetAutoLoginPolicy(kPublicSessionAccountId, kAutoLoginLongDelay);
  ASSERT_TRUE(auto_login_timer());

  // Login and check that it stopped the timer.
  existing_user_controller()->Login(UserContext(kUsername, kPassword, ""));
  EXPECT_TRUE(is_login_in_progress());
  ASSERT_TRUE(auto_login_timer());
  EXPECT_FALSE(auto_login_timer()->IsRunning());

  // Wait for login tasks to complete.
  content::RunAllPendingInMessageLoop();

  // Timer should still be stopped after login completes.
  ASSERT_TRUE(auto_login_timer());
  EXPECT_FALSE(auto_login_timer()->IsRunning());
}

IN_PROC_BROWSER_TEST_P(ExistingUserControllerPublicSessionTest,
                       GuestModeLoginStopsAutoLogin) {
  EXPECT_CALL(*mock_login_display_, SetUIEnabled(false))
      .Times(1);
  EXPECT_CALL(*mock_login_utils_, CreateAuthenticator(_))
      .Times(1)
      .WillOnce(WithArg<0>(CreateAuthenticator(kUsername, kPassword)));
  EXPECT_CALL(*mock_login_utils_, CompleteOffTheRecordLogin(_))
      .Times(1);

  existing_user_controller()->OnSigninScreenReady();
  SetAutoLoginPolicy(kPublicSessionAccountId, kAutoLoginLongDelay);
  ASSERT_TRUE(auto_login_timer());

  // Login and check that it stopped the timer.
  existing_user_controller()->LoginAsGuest();
  EXPECT_TRUE(is_login_in_progress());
  ASSERT_TRUE(auto_login_timer());
  EXPECT_FALSE(auto_login_timer()->IsRunning());

  // Wait for login tasks to complete.
  content::RunAllPendingInMessageLoop();

  // Timer should still be stopped after login completes.
  ASSERT_TRUE(auto_login_timer());
  EXPECT_FALSE(auto_login_timer()->IsRunning());
}

IN_PROC_BROWSER_TEST_P(ExistingUserControllerPublicSessionTest,
                       CompleteLoginStopsAutoLogin) {
  // Set up mocks to check login success.
  ExpectSuccessfulLogin(kUsername, kPassword);
  EXPECT_CALL(*mock_login_display_host_, OnCompleteLogin())
      .Times(1);

  existing_user_controller()->OnSigninScreenReady();
  SetAutoLoginPolicy(kPublicSessionAccountId, kAutoLoginLongDelay);
  ASSERT_TRUE(auto_login_timer());

  // Check that login completes and stops the timer.
  existing_user_controller()->CompleteLogin(
      UserContext(kUsername, kPassword, ""));
  ASSERT_TRUE(auto_login_timer());
  EXPECT_FALSE(auto_login_timer()->IsRunning());

  // Wait for login tasks to complete.
  content::RunAllPendingInMessageLoop();

  // Timer should still be stopped after login completes.
  ASSERT_TRUE(auto_login_timer());
  EXPECT_FALSE(auto_login_timer()->IsRunning());
}

IN_PROC_BROWSER_TEST_P(ExistingUserControllerPublicSessionTest,
                       PublicSessionLoginStopsAutoLogin) {
  // Set up mocks to check login success.
  ExpectSuccessfulLogin(public_session_user_id_, "");
  existing_user_controller()->OnSigninScreenReady();
  SetAutoLoginPolicy(kPublicSessionAccountId, kAutoLoginLongDelay);
  ASSERT_TRUE(auto_login_timer());

  // Login and check that it stopped the timer.
  existing_user_controller()->LoginAsPublicAccount(public_session_user_id_);
  EXPECT_TRUE(is_login_in_progress());
  ASSERT_TRUE(auto_login_timer());
  EXPECT_FALSE(auto_login_timer()->IsRunning());

  // Wait for login tasks to complete.
  content::RunAllPendingInMessageLoop();

  // Timer should still be stopped after login completes.
  ASSERT_TRUE(auto_login_timer());
  EXPECT_FALSE(auto_login_timer()->IsRunning());
}

INSTANTIATE_TEST_CASE_P(ExistingUserControllerTestInstantiation,
                        ExistingUserControllerTest,
                        testing::Bool());

INSTANTIATE_TEST_CASE_P(ExistingUserControllerPublicSessionTestInstantiation,
                        ExistingUserControllerPublicSessionTest,
                        testing::Bool());

}  // namespace chromeos
