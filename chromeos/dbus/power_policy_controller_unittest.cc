// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/dbus/power_policy_controller.h"

#include "base/memory/scoped_ptr.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/fake_power_manager_client.h"
#include "chromeos/dbus/mock_dbus_thread_manager_without_gmock.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::_;
using ::testing::SaveArg;

namespace chromeos {

class PowerPolicyControllerTest : public testing::Test {
 public:
  PowerPolicyControllerTest() {}
  virtual ~PowerPolicyControllerTest() {}

  virtual void SetUp() OVERRIDE {
    dbus_manager_ = new MockDBusThreadManagerWithoutGMock;
    DBusThreadManager::InitializeForTesting(dbus_manager_);  // Takes ownership.

    policy_controller_.reset(
        new PowerPolicyController(dbus_manager_, &fake_power_client_));
  }

  virtual void TearDown() OVERRIDE {
    policy_controller_.reset();
    DBusThreadManager::Shutdown();
  }

 protected:
  MockDBusThreadManagerWithoutGMock* dbus_manager_;  // Not owned.
  FakePowerManagerClient fake_power_client_;
  scoped_ptr<PowerPolicyController> policy_controller_;
};

TEST_F(PowerPolicyControllerTest, Prefs) {
  PowerPolicyController::PrefValues prefs;
  prefs.ac_screen_dim_delay_ms = 600000;
  prefs.ac_screen_off_delay_ms = 660000;
  prefs.ac_idle_delay_ms = 720000;
  prefs.battery_screen_dim_delay_ms = 300000;
  prefs.battery_screen_off_delay_ms = 360000;
  prefs.battery_idle_delay_ms = 420000;
  prefs.ac_idle_action = PowerPolicyController::ACTION_SUSPEND;
  prefs.battery_idle_action = PowerPolicyController::ACTION_STOP_SESSION;
  prefs.lid_closed_action = PowerPolicyController::ACTION_SHUT_DOWN;
  prefs.use_audio_activity = true;
  prefs.use_video_activity = true;
  prefs.enable_screen_lock = false;
  prefs.presentation_screen_dim_delay_factor = 3.0;
  prefs.user_activity_screen_dim_delay_factor = 2.0;
  policy_controller_->ApplyPrefs(prefs);

  power_manager::PowerManagementPolicy expected_policy;
  expected_policy.mutable_ac_delays()->set_screen_dim_ms(600000);
  expected_policy.mutable_ac_delays()->set_screen_off_ms(660000);
  expected_policy.mutable_ac_delays()->set_screen_lock_ms(-1);
  expected_policy.mutable_ac_delays()->set_idle_warning_ms(-1);
  expected_policy.mutable_ac_delays()->set_idle_ms(720000);
  expected_policy.mutable_battery_delays()->set_screen_dim_ms(300000);
  expected_policy.mutable_battery_delays()->set_screen_off_ms(360000);
  expected_policy.mutable_battery_delays()->set_screen_lock_ms(-1);
  expected_policy.mutable_battery_delays()->set_idle_warning_ms(-1);
  expected_policy.mutable_battery_delays()->set_idle_ms(420000);
  expected_policy.set_ac_idle_action(
      power_manager::PowerManagementPolicy_Action_SUSPEND);
  expected_policy.set_battery_idle_action(
      power_manager::PowerManagementPolicy_Action_STOP_SESSION);
  expected_policy.set_lid_closed_action(
      power_manager::PowerManagementPolicy_Action_SHUT_DOWN);
  expected_policy.set_use_audio_activity(true);
  expected_policy.set_use_video_activity(true);
  expected_policy.set_presentation_screen_dim_delay_factor(3.0);
  expected_policy.set_user_activity_screen_dim_delay_factor(2.0);
  expected_policy.set_reason("Prefs");
  EXPECT_EQ(PowerPolicyController::GetPolicyDebugString(expected_policy),
            PowerPolicyController::GetPolicyDebugString(
                fake_power_client_.get_policy()));

  // Change some prefs and check that an updated policy is sent.
  prefs.ac_idle_warning_delay_ms = 700000;
  prefs.battery_idle_warning_delay_ms = 400000;
  prefs.lid_closed_action = PowerPolicyController::ACTION_SUSPEND;
  policy_controller_->ApplyPrefs(prefs);
  expected_policy.mutable_ac_delays()->set_idle_warning_ms(700000);
  expected_policy.mutable_battery_delays()->set_idle_warning_ms(400000);
  expected_policy.set_lid_closed_action(
      power_manager::PowerManagementPolicy_Action_SUSPEND);
  EXPECT_EQ(PowerPolicyController::GetPolicyDebugString(expected_policy),
            PowerPolicyController::GetPolicyDebugString(
                fake_power_client_.get_policy()));

  // The enable-screen-lock pref should force the screen-lock delays to
  // match the screen-off delays plus a constant value.
  prefs.enable_screen_lock = true;
  policy_controller_->ApplyPrefs(prefs);
  expected_policy.mutable_ac_delays()->set_screen_lock_ms(
      660000 + PowerPolicyController::kScreenLockAfterOffDelayMs);
  expected_policy.mutable_battery_delays()->set_screen_lock_ms(
      360000 + PowerPolicyController::kScreenLockAfterOffDelayMs);
  EXPECT_EQ(PowerPolicyController::GetPolicyDebugString(expected_policy),
            PowerPolicyController::GetPolicyDebugString(
                fake_power_client_.get_policy()));

  // If the screen-lock-delay prefs are set to lower values than the
  // screen-off delays plus the constant, the lock prefs should take
  // precedence.
  prefs.ac_screen_lock_delay_ms = 70000;
  prefs.battery_screen_lock_delay_ms = 60000;
  policy_controller_->ApplyPrefs(prefs);
  expected_policy.mutable_ac_delays()->set_screen_lock_ms(70000);
  expected_policy.mutable_battery_delays()->set_screen_lock_ms(60000);
  EXPECT_EQ(PowerPolicyController::GetPolicyDebugString(expected_policy),
            PowerPolicyController::GetPolicyDebugString(
                fake_power_client_.get_policy()));

  // If the artificial screen-lock delays would exceed the idle delay, they
  // shouldn't be set -- the power manager would ignore them since the
  // idle action should lock the screen in this case.
  prefs.ac_screen_off_delay_ms = prefs.ac_idle_delay_ms - 1;
  prefs.battery_screen_off_delay_ms = prefs.battery_idle_delay_ms - 1;
  prefs.ac_screen_lock_delay_ms = -1;
  prefs.battery_screen_lock_delay_ms = -1;
  policy_controller_->ApplyPrefs(prefs);
  expected_policy.mutable_ac_delays()->set_screen_off_ms(
      prefs.ac_screen_off_delay_ms);
  expected_policy.mutable_battery_delays()->set_screen_off_ms(
      prefs.battery_screen_off_delay_ms);
  expected_policy.mutable_ac_delays()->set_screen_lock_ms(-1);
  expected_policy.mutable_battery_delays()->set_screen_lock_ms(-1);
  EXPECT_EQ(PowerPolicyController::GetPolicyDebugString(expected_policy),
            PowerPolicyController::GetPolicyDebugString(
                fake_power_client_.get_policy()));

  // Set the "allow screen wake locks" pref to false.  The system should be
  // prevented from suspending due to user inactivity on AC power but the
  // pref-supplied screen-related delays should be left untouched.
  prefs.allow_screen_wake_locks = false;
  policy_controller_->ApplyPrefs(prefs);
  policy_controller_->AddScreenWakeLock("Screen");
  expected_policy.set_ac_idle_action(
      power_manager::PowerManagementPolicy_Action_DO_NOTHING);
  expected_policy.set_reason("Prefs, Screen");
  EXPECT_EQ(PowerPolicyController::GetPolicyDebugString(expected_policy),
            PowerPolicyController::GetPolicyDebugString(
                fake_power_client_.get_policy()));
}

TEST_F(PowerPolicyControllerTest, WakeLocks) {
  const char kSystemWakeLockReason[] = "system";
  const int system_id =
      policy_controller_->AddSystemWakeLock(kSystemWakeLockReason);
  power_manager::PowerManagementPolicy expected_policy;
  expected_policy.set_ac_idle_action(
      power_manager::PowerManagementPolicy_Action_DO_NOTHING);
  expected_policy.set_battery_idle_action(
      power_manager::PowerManagementPolicy_Action_DO_NOTHING);
  expected_policy.set_reason(kSystemWakeLockReason);
  EXPECT_EQ(PowerPolicyController::GetPolicyDebugString(expected_policy),
            PowerPolicyController::GetPolicyDebugString(
                fake_power_client_.get_policy()));

  const char kScreenWakeLockReason[] = "screen";
  const int screen_id = policy_controller_->AddScreenWakeLock(
      kScreenWakeLockReason);
  expected_policy.mutable_ac_delays()->set_screen_dim_ms(0);
  expected_policy.mutable_ac_delays()->set_screen_off_ms(0);
  expected_policy.mutable_ac_delays()->set_screen_lock_ms(0);
  expected_policy.mutable_battery_delays()->set_screen_dim_ms(0);
  expected_policy.mutable_battery_delays()->set_screen_off_ms(0);
  expected_policy.mutable_battery_delays()->set_screen_lock_ms(0);
  expected_policy.set_reason(
      std::string(kScreenWakeLockReason) + ", " + kSystemWakeLockReason);
  EXPECT_EQ(PowerPolicyController::GetPolicyDebugString(expected_policy),
            PowerPolicyController::GetPolicyDebugString(
                fake_power_client_.get_policy()));

  policy_controller_->RemoveWakeLock(system_id);
  expected_policy.set_reason(kScreenWakeLockReason);
  EXPECT_EQ(PowerPolicyController::GetPolicyDebugString(expected_policy),
            PowerPolicyController::GetPolicyDebugString(
                fake_power_client_.get_policy()));

  policy_controller_->RemoveWakeLock(screen_id);
  expected_policy.Clear();
  EXPECT_EQ(PowerPolicyController::GetPolicyDebugString(expected_policy),
            PowerPolicyController::GetPolicyDebugString(
                fake_power_client_.get_policy()));
}

}  // namespace chromeos
