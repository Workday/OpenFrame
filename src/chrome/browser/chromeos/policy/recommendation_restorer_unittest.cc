// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/policy/recommendation_restorer.h"

#include "ash/magnifier/magnifier_constants.h"
#include "base/memory/scoped_ptr.h"
#include "base/prefs/pref_notifier_impl.h"
#include "base/prefs/testing_pref_store.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/test_simple_task_runner.h"
#include "base/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/chromeos/policy/recommendation_restorer_factory.h"
#include "chrome/browser/prefs/browser_prefs.h"
#include "chrome/browser/prefs/pref_service_syncable.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_pref_service_syncable.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/base/testing_profile_manager.h"
#include "components/user_prefs/pref_registry_syncable.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace policy {

namespace {
  // The amount of idle time after which recommended values are restored.
  const int kRestoreDelayInMs = 60 * 1000;  // 1 minute.
}  // namespace

class RecommendationRestorerTest : public testing::Test {
 protected:
  RecommendationRestorerTest();

  // testing::Test:
  virtual void SetUp() OVERRIDE;

  void RegisterUserProfilePrefs();
  void RegisterLoginProfilePrefs();

  void SetRecommendedValues();
  void SetUserSettings();

  void CreateLoginProfile();
  void CreateUserProfile();

  void NotifyOfSessionStart();
  void NotifyOfUserActivity();

  void VerifyPrefFollowsUser(const char* pref_name,
                             const base::Value& expected_value) const;
  void VerifyPrefsFollowUser() const;
  void VerifyPrefFollowsRecommendation(const char* pref_name,
                                       const base::Value& expected_value) const;
  void VerifyPrefsFollowRecommendations() const;

  void VerifyNotListeningForNotifications() const;
  void VerifyTimerIsStopped() const;
  void VerifyTimerIsRunning() const;

  TestingPrefStore* recommended_prefs_;  // Not owned.
  TestingPrefServiceSyncable* prefs_;    // Not owned.
  RecommendationRestorer* restorer_;     // Not owned.

  scoped_refptr<base::TestSimpleTaskRunner> runner_;
  base::ThreadTaskRunnerHandle runner_handler_;

 private:
  scoped_ptr<PrefServiceSyncable> prefs_owner_;

  TestingProfileManager profile_manager_;

  DISALLOW_COPY_AND_ASSIGN(RecommendationRestorerTest);
};

RecommendationRestorerTest::RecommendationRestorerTest()
    : recommended_prefs_(new TestingPrefStore),
      prefs_(new TestingPrefServiceSyncable(
          new TestingPrefStore,
          new TestingPrefStore,
          recommended_prefs_,
          new user_prefs::PrefRegistrySyncable,
          new PrefNotifierImpl)),
      restorer_(NULL),
      runner_(new base::TestSimpleTaskRunner),
      runner_handler_(runner_),
      prefs_owner_(prefs_),
      profile_manager_(TestingBrowserProcess::GetGlobal()) {
}

void RecommendationRestorerTest::SetUp() {
  testing::Test::SetUp();
  ASSERT_TRUE(profile_manager_.SetUp());
}

void RecommendationRestorerTest::RegisterUserProfilePrefs() {
  chrome::RegisterUserProfilePrefs(prefs_->registry());
}

void RecommendationRestorerTest::RegisterLoginProfilePrefs() {
  chrome::RegisterLoginProfilePrefs(prefs_->registry());
}

void RecommendationRestorerTest::SetRecommendedValues() {
  recommended_prefs_->SetBoolean(prefs::kLargeCursorEnabled, false);
  recommended_prefs_->SetBoolean(prefs::kSpokenFeedbackEnabled, false);
  recommended_prefs_->SetBoolean(prefs::kHighContrastEnabled, false);
  recommended_prefs_->SetBoolean(prefs::kScreenMagnifierEnabled, false);
  recommended_prefs_->SetInteger(prefs::kScreenMagnifierType, 0);
}

void RecommendationRestorerTest::SetUserSettings() {
  prefs_->SetBoolean(prefs::kLargeCursorEnabled, true);
  prefs_->SetBoolean(prefs::kSpokenFeedbackEnabled, true);
  prefs_->SetBoolean(prefs::kHighContrastEnabled, true);
  prefs_->SetBoolean(prefs::kScreenMagnifierEnabled, true);
  prefs_->SetInteger(prefs::kScreenMagnifierType, ash::MAGNIFIER_FULL);
}

void RecommendationRestorerTest::CreateLoginProfile() {
  ASSERT_FALSE(restorer_);
  TestingProfile* profile = profile_manager_.CreateTestingProfile(
      chrome::kInitialProfile, prefs_owner_.Pass(),
      UTF8ToUTF16(chrome::kInitialProfile), 0, std::string());
  restorer_ = RecommendationRestorerFactory::GetForProfile(profile);
  EXPECT_TRUE(restorer_);
}

void RecommendationRestorerTest::CreateUserProfile() {
  ASSERT_FALSE(restorer_);
  TestingProfile* profile = profile_manager_.CreateTestingProfile(
      "user", prefs_owner_.Pass(), UTF8ToUTF16("user"), 0, std::string());
  restorer_ = RecommendationRestorerFactory::GetForProfile(profile);
  EXPECT_TRUE(restorer_);
}

void RecommendationRestorerTest::NotifyOfSessionStart() {
  ASSERT_TRUE(restorer_);
  restorer_->Observe(chrome::NOTIFICATION_LOGIN_USER_CHANGED,
                     content::Source<RecommendationRestorerTest>(this),
                     content::NotificationService::NoDetails());
}

void RecommendationRestorerTest::NotifyOfUserActivity() {
  ASSERT_TRUE(restorer_);
  restorer_->OnUserActivity(NULL);
}

void RecommendationRestorerTest::VerifyPrefFollowsUser(
    const char* pref_name,
    const base::Value& expected_value) const {
  const PrefServiceSyncable::Preference* pref =
      prefs_->FindPreference(pref_name);
  ASSERT_TRUE(pref);
  EXPECT_TRUE(pref->HasUserSetting());
  const base::Value* value = pref->GetValue();
  ASSERT_TRUE(value);
  EXPECT_TRUE(expected_value.Equals(value));
}

void RecommendationRestorerTest::VerifyPrefsFollowUser() const {
  VerifyPrefFollowsUser(prefs::kLargeCursorEnabled,
                        base::FundamentalValue(true));
  VerifyPrefFollowsUser(prefs::kSpokenFeedbackEnabled,
                        base::FundamentalValue(true));
  VerifyPrefFollowsUser(prefs::kHighContrastEnabled,
                        base::FundamentalValue(true));
  VerifyPrefFollowsUser(prefs::kScreenMagnifierEnabled,
                        base::FundamentalValue(true));
  VerifyPrefFollowsUser(prefs::kScreenMagnifierType,
                        base::FundamentalValue(ash::MAGNIFIER_FULL));
}

void RecommendationRestorerTest::VerifyPrefFollowsRecommendation(
    const char* pref_name,
    const base::Value& expected_value) const {
  const PrefServiceSyncable::Preference* pref =
      prefs_->FindPreference(pref_name);
  ASSERT_TRUE(pref);
  EXPECT_TRUE(pref->IsRecommended());
  EXPECT_FALSE(pref->HasUserSetting());
  const base::Value* value = pref->GetValue();
  ASSERT_TRUE(value);
  EXPECT_TRUE(expected_value.Equals(value));
}

void RecommendationRestorerTest::VerifyPrefsFollowRecommendations() const {
  VerifyPrefFollowsRecommendation(prefs::kLargeCursorEnabled,
                                  base::FundamentalValue(false));
  VerifyPrefFollowsRecommendation(prefs::kSpokenFeedbackEnabled,
                                  base::FundamentalValue(false));
  VerifyPrefFollowsRecommendation(prefs::kHighContrastEnabled,
                                  base::FundamentalValue(false));
  VerifyPrefFollowsRecommendation(prefs::kScreenMagnifierEnabled,
                                  base::FundamentalValue(false));
  VerifyPrefFollowsRecommendation(prefs::kScreenMagnifierType,
                                  base::FundamentalValue(0));
}

void RecommendationRestorerTest::VerifyNotListeningForNotifications() const {
  ASSERT_TRUE(restorer_);
  EXPECT_TRUE(restorer_->pref_change_registrar_.IsEmpty());
  EXPECT_TRUE(restorer_->notification_registrar_.IsEmpty());
}

void RecommendationRestorerTest::VerifyTimerIsStopped() const {
  ASSERT_TRUE(restorer_);
  EXPECT_FALSE(restorer_->restore_timer_.IsRunning());
}

void RecommendationRestorerTest::VerifyTimerIsRunning() const {
  ASSERT_TRUE(restorer_);
  EXPECT_TRUE(restorer_->restore_timer_.IsRunning());
  EXPECT_EQ(base::TimeDelta::FromMilliseconds(kRestoreDelayInMs),
            restorer_->restore_timer_.GetCurrentDelay());
}

TEST_F(RecommendationRestorerTest, CreateForUserProfile) {
  // Verifies that when a RecommendationRestorer is created for a user profile,
  // it does not start listening for any notifications, does not clear user
  // settings on initialization and does not start a timer that will clear user
  // settings eventually.
  RegisterUserProfilePrefs();
  SetRecommendedValues();
  SetUserSettings();

  CreateUserProfile();
  VerifyNotListeningForNotifications();
  VerifyPrefsFollowUser();
  VerifyTimerIsStopped();
}

TEST_F(RecommendationRestorerTest, NoRecommendations) {
  // Verifies that when no recommended values have been set and a
  // RecommendationRestorer is created for the login profile, it does not clear
  // user settings on initialization and does not start a timer that will clear
  // user settings eventually.
  RegisterLoginProfilePrefs();
  SetUserSettings();

  CreateLoginProfile();
  VerifyPrefsFollowUser();
  VerifyTimerIsStopped();
}

TEST_F(RecommendationRestorerTest, RestoreOnStartup) {
  // Verifies that when recommended values have been set and a
  // RecommendationRestorer is created for the login profile, it clears user
  // settings on initialization.
  RegisterLoginProfilePrefs();
  SetRecommendedValues();
  SetUserSettings();

  CreateLoginProfile();
  VerifyPrefsFollowRecommendations();
  VerifyTimerIsStopped();
}

TEST_F(RecommendationRestorerTest, RestoreOnRecommendationChangeOnLoginScreen) {
  // Verifies that if recommended values change while the login screen is being
  // shown, a timer is started that will clear user settings eventually.
  RegisterLoginProfilePrefs();
  SetUserSettings();

  CreateLoginProfile();

  VerifyTimerIsStopped();
  recommended_prefs_->SetBoolean(prefs::kLargeCursorEnabled, false);
  VerifyPrefFollowsUser(prefs::kLargeCursorEnabled,
                        base::FundamentalValue(true));
  VerifyTimerIsRunning();
  runner_->RunUntilIdle();
  VerifyPrefFollowsRecommendation(prefs::kLargeCursorEnabled,
                                  base::FundamentalValue(false));

  VerifyTimerIsStopped();
  recommended_prefs_->SetBoolean(prefs::kSpokenFeedbackEnabled, false);
  VerifyPrefFollowsUser(prefs::kSpokenFeedbackEnabled,
                        base::FundamentalValue(true));
  VerifyTimerIsRunning();
  runner_->RunUntilIdle();
  VerifyPrefFollowsRecommendation(prefs::kSpokenFeedbackEnabled,
                                  base::FundamentalValue(false));

  VerifyTimerIsStopped();
  recommended_prefs_->SetBoolean(prefs::kHighContrastEnabled, false);
  VerifyPrefFollowsUser(prefs::kHighContrastEnabled,
                        base::FundamentalValue(true));
  VerifyTimerIsRunning();
  runner_->RunUntilIdle();
  VerifyPrefFollowsRecommendation(prefs::kHighContrastEnabled,
                                  base::FundamentalValue(false));

  VerifyTimerIsStopped();
  recommended_prefs_->SetBoolean(prefs::kScreenMagnifierEnabled, false);
  recommended_prefs_->SetInteger(prefs::kScreenMagnifierType, 0);
  VerifyPrefFollowsUser(prefs::kScreenMagnifierEnabled,
                        base::FundamentalValue(true));
  VerifyPrefFollowsUser(prefs::kScreenMagnifierType,
                        base::FundamentalValue(ash::MAGNIFIER_FULL));
  VerifyTimerIsRunning();
  runner_->RunUntilIdle();
  VerifyPrefFollowsRecommendation(prefs::kScreenMagnifierEnabled,
                                  base::FundamentalValue(false));
  VerifyPrefFollowsRecommendation(prefs::kScreenMagnifierType,
                                  base::FundamentalValue(0));

  VerifyTimerIsStopped();
}

TEST_F(RecommendationRestorerTest, RestoreOnRecommendationChangeInUserSession) {
  // Verifies that if recommended values change while a user session is in
  // progress, user settings are cleared immediately.
  RegisterLoginProfilePrefs();
  SetUserSettings();

  CreateLoginProfile();
  NotifyOfSessionStart();

  VerifyPrefFollowsUser(prefs::kLargeCursorEnabled,
                        base::FundamentalValue(true));
  recommended_prefs_->SetBoolean(prefs::kLargeCursorEnabled, false);
  VerifyTimerIsStopped();
  VerifyPrefFollowsRecommendation(prefs::kLargeCursorEnabled,
                                  base::FundamentalValue(false));

  VerifyPrefFollowsUser(prefs::kSpokenFeedbackEnabled,
                        base::FundamentalValue(true));
  recommended_prefs_->SetBoolean(prefs::kSpokenFeedbackEnabled, false);
  VerifyTimerIsStopped();
  VerifyPrefFollowsRecommendation(prefs::kSpokenFeedbackEnabled,
                                  base::FundamentalValue(false));

  VerifyPrefFollowsUser(prefs::kHighContrastEnabled,
                        base::FundamentalValue(true));
  recommended_prefs_->SetBoolean(prefs::kHighContrastEnabled, false);
  VerifyTimerIsStopped();
  VerifyPrefFollowsRecommendation(prefs::kHighContrastEnabled,
                                  base::FundamentalValue(false));

  VerifyPrefFollowsUser(prefs::kScreenMagnifierEnabled,
                        base::FundamentalValue(true));
  VerifyPrefFollowsUser(prefs::kScreenMagnifierType,
                        base::FundamentalValue(ash::MAGNIFIER_FULL));
  recommended_prefs_->SetBoolean(prefs::kScreenMagnifierEnabled, false);
  recommended_prefs_->SetInteger(prefs::kScreenMagnifierType, 0);
  VerifyTimerIsStopped();
  VerifyPrefFollowsRecommendation(prefs::kScreenMagnifierEnabled,
                                  base::FundamentalValue(false));
  VerifyPrefFollowsRecommendation(prefs::kScreenMagnifierType,
                                  base::FundamentalValue(0));
}

TEST_F(RecommendationRestorerTest, DoNothingOnUserChange) {
  // Verifies that if no recommended values have been set and user settings
  // change, the user settings are not cleared immediately and no timer is
  // started that will clear the user settings eventually.
  RegisterLoginProfilePrefs();
  CreateLoginProfile();

  prefs_->SetBoolean(prefs::kLargeCursorEnabled, true);
  VerifyPrefFollowsUser(prefs::kLargeCursorEnabled,
                        base::FundamentalValue(true));
  VerifyTimerIsStopped();

  prefs_->SetBoolean(prefs::kSpokenFeedbackEnabled, true);
  VerifyPrefFollowsUser(prefs::kSpokenFeedbackEnabled,
                        base::FundamentalValue(true));
  VerifyTimerIsStopped();

  prefs_->SetBoolean(prefs::kHighContrastEnabled, true);
  VerifyPrefFollowsUser(prefs::kHighContrastEnabled,
                        base::FundamentalValue(true));
  VerifyTimerIsStopped();

  prefs_->SetBoolean(prefs::kScreenMagnifierEnabled, true);
  VerifyPrefFollowsUser(prefs::kScreenMagnifierEnabled,
                        base::FundamentalValue(true));
  VerifyTimerIsStopped();

  prefs_->SetBoolean(prefs::kScreenMagnifierEnabled, true);
  prefs_->SetInteger(prefs::kScreenMagnifierType, ash::MAGNIFIER_FULL);
  VerifyPrefFollowsUser(prefs::kScreenMagnifierEnabled,
                        base::FundamentalValue(true));
  VerifyPrefFollowsUser(prefs::kScreenMagnifierType,
                        base::FundamentalValue(ash::MAGNIFIER_FULL));
  VerifyTimerIsStopped();
}

TEST_F(RecommendationRestorerTest, RestoreOnUserChange) {
  // Verifies that if recommended values have been set and user settings change
  // while the login screen is being shown, a timer is started that will clear
  // the user settings eventually.
  RegisterLoginProfilePrefs();
  SetRecommendedValues();

  CreateLoginProfile();

  VerifyTimerIsStopped();
  prefs_->SetBoolean(prefs::kLargeCursorEnabled, true);
  VerifyPrefFollowsUser(prefs::kLargeCursorEnabled,
                        base::FundamentalValue(true));
  VerifyTimerIsRunning();
  runner_->RunUntilIdle();
  VerifyPrefFollowsRecommendation(prefs::kLargeCursorEnabled,
                                  base::FundamentalValue(false));

  VerifyTimerIsStopped();
  prefs_->SetBoolean(prefs::kSpokenFeedbackEnabled, true);
  VerifyPrefFollowsUser(prefs::kSpokenFeedbackEnabled,
                        base::FundamentalValue(true));
  VerifyTimerIsRunning();
  runner_->RunUntilIdle();
  VerifyPrefFollowsRecommendation(prefs::kSpokenFeedbackEnabled,
                                  base::FundamentalValue(false));

  VerifyTimerIsStopped();
  prefs_->SetBoolean(prefs::kHighContrastEnabled, true);
  VerifyPrefFollowsUser(prefs::kHighContrastEnabled,
                        base::FundamentalValue(true));
  VerifyTimerIsRunning();
  runner_->RunUntilIdle();
  VerifyPrefFollowsRecommendation(prefs::kHighContrastEnabled,
                                  base::FundamentalValue(false));

  VerifyTimerIsStopped();
  prefs_->SetBoolean(prefs::kScreenMagnifierEnabled, true);
  prefs_->SetInteger(prefs::kScreenMagnifierType, ash::MAGNIFIER_FULL);
  VerifyPrefFollowsUser(prefs::kScreenMagnifierEnabled,
                        base::FundamentalValue(true));
  VerifyPrefFollowsUser(prefs::kScreenMagnifierType,
                        base::FundamentalValue(ash::MAGNIFIER_FULL));
  VerifyTimerIsRunning();
  runner_->RunUntilIdle();
  VerifyPrefFollowsRecommendation(prefs::kScreenMagnifierEnabled,
                                  base::FundamentalValue(false));
  VerifyPrefFollowsRecommendation(prefs::kScreenMagnifierType,
                                  base::FundamentalValue(0));

  VerifyTimerIsStopped();
}

TEST_F(RecommendationRestorerTest, RestoreOnSessionStart) {
  // Verifies that if recommended values have been set, user settings have
  // changed and a session is then started, the user settings are cleared
  // immediately and the timer that would have cleared them eventually on the
  // login screen is stopped.
  RegisterLoginProfilePrefs();
  SetRecommendedValues();

  CreateLoginProfile();
  SetUserSettings();

  NotifyOfSessionStart();
  VerifyPrefsFollowRecommendations();
  VerifyTimerIsStopped();
}

TEST_F(RecommendationRestorerTest, DoNothingOnSessionStart) {
  // Verifies that if recommended values have not been set, user settings have
  // changed and a session is then started, the user settings are not cleared
  // immediately.
  RegisterLoginProfilePrefs();
  CreateLoginProfile();
  SetUserSettings();

  NotifyOfSessionStart();
  VerifyPrefsFollowUser();
  VerifyTimerIsStopped();
}

TEST_F(RecommendationRestorerTest, UserActivityResetsTimer) {
  // Verifies that user activity resets the timer which clears user settings.
  RegisterLoginProfilePrefs();

  recommended_prefs_->SetBoolean(prefs::kLargeCursorEnabled, false);

  CreateLoginProfile();

  prefs_->SetBoolean(prefs::kLargeCursorEnabled, true);
  VerifyTimerIsRunning();

  // Notify that there is user activity, then fast forward until the originally
  // set timer fires.
  NotifyOfUserActivity();
  runner_->RunPendingTasks();
  VerifyPrefFollowsUser(prefs::kLargeCursorEnabled,
                        base::FundamentalValue(true));

  // Fast forward until the reset timer fires.
  VerifyTimerIsRunning();
  runner_->RunUntilIdle();
  VerifyPrefFollowsRecommendation(prefs::kLargeCursorEnabled,
                                  base::FundamentalValue(false));
  VerifyTimerIsStopped();
}

}  // namespace policy
