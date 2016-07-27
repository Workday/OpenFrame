// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/policy/display_rotation_default_handler.h"

#include "ash/display/display_manager.h"
#include "ash/shell.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/location.h"
#include "base/message_loop/message_loop.h"
#include "chrome/browser/chromeos/login/ui/login_display_host_impl.h"
#include "chrome/browser/chromeos/policy/device_policy_builder.h"
#include "chrome/browser/chromeos/policy/device_policy_cros_browser_test.h"
#include "chrome/browser/chromeos/policy/proto/chrome_device_policy.pb.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chromeos/chromeos_switches.h"
#include "chromeos/settings/cros_settings_names.h"
#include "content/public/test/test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace em = enterprise_management;

namespace policy {

class DisplayRotationDefaultTest
    : public policy::DevicePolicyCrosBrowserTest,
      public testing::WithParamInterface<gfx::Display::Rotation> {
 public:
  DisplayRotationDefaultTest() {}

  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitch(chromeos::switches::kLoginManager);
    command_line->AppendSwitch(chromeos::switches::kForceLoginManagerInTests);
  }

  void SetUpInProcessBrowserTestFixture() override {
    InstallOwnerKey();
    MarkAsEnterpriseOwned();
    DevicePolicyCrosBrowserTest::SetUpInProcessBrowserTestFixture();
  }

  void TearDownOnMainThread() override {
    // If the login display is still showing, exit gracefully.
    if (chromeos::LoginDisplayHostImpl::default_host()) {
      base::MessageLoop::current()->PostTask(FROM_HERE,
                                             base::Bind(&chrome::AttemptExit));
      content::RunMessageLoop();
    }
  }

 protected:
  void SetPolicy(int rotation) {
    em::ChromeDeviceSettingsProto& proto(device_policy()->payload());
    proto.mutable_display_rotation_default()->set_display_rotation_default(
        static_cast<em::DisplayRotationDefaultProto::Rotation>(rotation));
    RefreshPolicyAndWaitUntilDeviceSettingsUpdated();
  }

  // This is needed to test that refreshing the settings without change to the
  // DisplayRotationDefault policy will not rotate the display again.
  void SetADifferentPolicy() {
    em::ChromeDeviceSettingsProto& proto(device_policy()->payload());
    const bool clock24 = proto.use_24hour_clock().use_24hour_clock();
    proto.mutable_use_24hour_clock()->set_use_24hour_clock(!clock24);
    RefreshPolicyAndWaitUntilDeviceSettingsUpdated();
  }

  void UnsetPolicy() {
    em::ChromeDeviceSettingsProto& proto(device_policy()->payload());
    proto.clear_display_rotation_default();
    RefreshPolicyAndWaitUntilDeviceSettingsUpdated();
  }

  ash::DisplayManager* GetDisplayManager() const {
    return ash::Shell::GetInstance()->display_manager();
  }

  gfx::Display::Rotation GetRotationOfFirstDisplay() const {
    const ash::DisplayManager* const display_manager = GetDisplayManager();
    const int64_t first_display_id = display_manager->first_display_id();
    const gfx::Display& first_display =
        display_manager->GetDisplayForId(first_display_id);
    return first_display.rotation();
  }

  // Fails the test and returns ROTATE_0 if there is no second display.
  gfx::Display::Rotation GetRotationOfSecondDisplay() const {
    const ash::DisplayManager* const display_manager = GetDisplayManager();
    if (display_manager->GetNumDisplays() < 2) {
      ADD_FAILURE()
          << "Requested rotation of second display while there was only one.";
      return gfx::Display::ROTATE_0;
    }
    const ash::DisplayIdPair display_id_pair =
        display_manager->GetCurrentDisplayIdPair();
    const gfx::Display& second_display =
        display_manager->GetDisplayForId(display_id_pair.second);
    return second_display.rotation();
  }

  // Creates second display if there is none yet, or removes it if there is one.
  void ToggleSecondDisplay() {
    GetDisplayManager()->AddRemoveDisplay();
    base::RunLoop run_loop;
    run_loop.RunUntilIdle();
  }

  void RefreshPolicyAndWaitUntilDeviceSettingsUpdated() {
    base::RunLoop run_loop;
    // For calls from SetPolicy().
    scoped_ptr<chromeos::CrosSettings::ObserverSubscription> observer =
        chromeos::CrosSettings::Get()->AddSettingsObserver(
            chromeos::kDisplayRotationDefault, run_loop.QuitClosure());
    // For calls from SetADifferentPolicy().
    scoped_ptr<chromeos::CrosSettings::ObserverSubscription> observer2 =
        chromeos::CrosSettings::Get()->AddSettingsObserver(
            chromeos::kSystemUse24HourClock, run_loop.QuitClosure());
    RefreshDevicePolicy();
    run_loop.Run();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(DisplayRotationDefaultTest);
};

// If gfx::Display::Rotation is ever modified and this test fails, there are
// hardcoded enum-values in the following files that might need adjustment:
// * this file: range check in this function, initializations, expected values,
//              the list of parameters at the bottom of the file
// * display_rotation_default_handler.cc: Range check in UpdateFromCrosSettings,
//                                        initialization to ROTATE_0
// * display_rotation_default_handler.h: initialization to ROTATE_0
// * chrome/browser/chromeos/policy/proto/chrome_device_policy.proto:
//       DisplayRotationDefaultProto::Rotation should match one to one
IN_PROC_BROWSER_TEST_P(DisplayRotationDefaultTest, EnumsInSync) {
  const gfx::Display::Rotation rotation = GetParam();
  EXPECT_EQ(em::DisplayRotationDefaultProto::Rotation_ARRAYSIZE,
            gfx::Display::ROTATE_270 + 1)
      << "Enums gfx::Display::Rotation and "
      << "em::DisplayRotationDefaultProto::Rotation are not in sync.";
  EXPECT_TRUE(em::DisplayRotationDefaultProto::Rotation_IsValid(rotation))
      << rotation << " is invalid as rotation. All valid values lie in "
      << "the range from " << em::DisplayRotationDefaultProto::Rotation_MIN
      << " to " << em::DisplayRotationDefaultProto::Rotation_MAX << ".";
}

IN_PROC_BROWSER_TEST_P(DisplayRotationDefaultTest, FirstDisplay) {
  const gfx::Display::Rotation policy_rotation = GetParam();
  EXPECT_EQ(gfx::Display::ROTATE_0, GetRotationOfFirstDisplay())
      << "Initial primary rotation before policy";

  SetPolicy(policy_rotation);
  int settings_rotation;
  EXPECT_TRUE(chromeos::CrosSettings::Get()->GetInteger(
      chromeos::kDisplayRotationDefault, &settings_rotation));
  EXPECT_EQ(policy_rotation, settings_rotation)
      << "Value of CrosSettings after policy value changed";
  EXPECT_EQ(policy_rotation, GetRotationOfFirstDisplay())
      << "Rotation of primary display after policy";
}

IN_PROC_BROWSER_TEST_P(DisplayRotationDefaultTest, RefreshSecondDisplay) {
  const gfx::Display::Rotation policy_rotation = GetParam();
  ToggleSecondDisplay();
  EXPECT_EQ(gfx::Display::ROTATE_0, GetRotationOfSecondDisplay())
      << "Rotation of secondary display before policy";
  SetPolicy(policy_rotation);
  EXPECT_EQ(policy_rotation, GetRotationOfSecondDisplay())
      << "Rotation of already connected secondary display after policy";
}

IN_PROC_BROWSER_TEST_P(DisplayRotationDefaultTest, ConnectSecondDisplay) {
  const gfx::Display::Rotation policy_rotation = GetParam();
  SetPolicy(policy_rotation);
  ToggleSecondDisplay();
  EXPECT_EQ(policy_rotation, GetRotationOfFirstDisplay())
      << "Rotation of primary display after policy";
  EXPECT_EQ(policy_rotation, GetRotationOfSecondDisplay())
      << "Rotation of newly connected secondary display after policy";
}

IN_PROC_BROWSER_TEST_P(DisplayRotationDefaultTest, UserInteraction) {
  const gfx::Display::Rotation policy_rotation = GetParam();
  const gfx::Display::Rotation user_rotation = gfx::Display::ROTATE_90;
  GetDisplayManager()->SetDisplayRotation(
      GetDisplayManager()->first_display_id(), user_rotation,
      gfx::Display::ROTATION_SOURCE_USER);
  EXPECT_EQ(user_rotation, GetRotationOfFirstDisplay())
      << "Rotation of primary display after user change";
  SetPolicy(policy_rotation);
  EXPECT_EQ(policy_rotation, GetRotationOfFirstDisplay())
      << "Rotation of primary display after policy overrode user change";
  GetDisplayManager()->SetDisplayRotation(
      GetDisplayManager()->first_display_id(), user_rotation,
      gfx::Display::ROTATION_SOURCE_USER);
  EXPECT_EQ(user_rotation, GetRotationOfFirstDisplay())
      << "Rotation of primary display after user overrode policy change";
  SetADifferentPolicy();
  EXPECT_EQ(user_rotation, GetRotationOfFirstDisplay())
      << "Rotation of primary display after policy reloaded without change";
}

IN_PROC_BROWSER_TEST_P(DisplayRotationDefaultTest, SetAndUnsetPolicy) {
  const gfx::Display::Rotation policy_rotation = GetParam();
  SetPolicy(policy_rotation);
  UnsetPolicy();
  EXPECT_EQ(policy_rotation, GetRotationOfFirstDisplay())
      << "Rotation of primary display after policy was set and removed.";
}

IN_PROC_BROWSER_TEST_P(DisplayRotationDefaultTest,
                       SetAndUnsetPolicyWithUserInteraction) {
  const gfx::Display::Rotation policy_rotation = GetParam();
  const gfx::Display::Rotation user_rotation = gfx::Display::ROTATE_90;
  SetPolicy(policy_rotation);
  GetDisplayManager()->SetDisplayRotation(
      GetDisplayManager()->first_display_id(), user_rotation,
      gfx::Display::ROTATION_SOURCE_USER);
  UnsetPolicy();
  EXPECT_EQ(user_rotation, GetRotationOfFirstDisplay())
      << "Rotation of primary display after policy was set to "
      << policy_rotation << ", user changed the rotation to " << user_rotation
      << ", and policy was removed.";
}

INSTANTIATE_TEST_CASE_P(PolicyDisplayRotationDefault,
                        DisplayRotationDefaultTest,
                        testing::Values(gfx::Display::ROTATE_0,
                                        gfx::Display::ROTATE_90,
                                        gfx::Display::ROTATE_180,
                                        gfx::Display::ROTATE_270));
}  // namespace policy
