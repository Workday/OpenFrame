// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/scoped_ptr.h"
#include "chrome/browser/chromeos/login/helper.h"
#include "chrome/browser/chromeos/login/screens/mock_screen_observer.h"
#include "chrome/browser/chromeos/login/screens/network_screen.h"
#include "chrome/browser/chromeos/login/screens/wizard_screen.h"
#include "chrome/browser/chromeos/login/wizard_controller.h"
#include "chrome/browser/chromeos/login/wizard_in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "chromeos/dbus/fake_session_manager_client.h"
#include "chromeos/dbus/mock_dbus_thread_manager_without_gmock.h"
#include "content/public/test/test_utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/cros_system_api/dbus/service_constants.h"
#include "ui/views/controls/button/button.h"

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Return;
using ::testing::ReturnRef;
using views::Button;

namespace chromeos {

class DummyButtonListener : public views::ButtonListener {
 public:
  virtual void ButtonPressed(views::Button* sender,
                             const ui::Event& event) OVERRIDE {}
};

namespace login {

class MockNetworkStateHelper : public NetworkStateHelper {
 public:
  MOCK_CONST_METHOD0(GetCurrentNetworkName, string16(void));
  MOCK_CONST_METHOD0(IsConnected, bool(void));
  MOCK_CONST_METHOD0(IsConnecting, bool(void));
};

}  // namespace login

class NetworkScreenTest : public WizardInProcessBrowserTest {
 public:
  NetworkScreenTest(): WizardInProcessBrowserTest("network"),
                       fake_session_manager_client_(NULL) {
  }

 protected:
  virtual void SetUpInProcessBrowserTestFixture() OVERRIDE {
    WizardInProcessBrowserTest::SetUpInProcessBrowserTestFixture();

    MockDBusThreadManagerWithoutGMock* mock_dbus_thread_manager =
        new MockDBusThreadManagerWithoutGMock;
    DBusThreadManager::InitializeForTesting(mock_dbus_thread_manager);
    fake_session_manager_client_ =
        mock_dbus_thread_manager->fake_session_manager_client();
  }

  virtual void SetUpOnMainThread() OVERRIDE {
    WizardInProcessBrowserTest::SetUpOnMainThread();
    mock_screen_observer_.reset(new MockScreenObserver());
    ASSERT_TRUE(WizardController::default_controller() != NULL);
    network_screen_ =
        WizardController::default_controller()->GetNetworkScreen();
    ASSERT_TRUE(network_screen_ != NULL);
    ASSERT_EQ(WizardController::default_controller()->current_screen(),
              network_screen_);
    network_screen_->screen_observer_ = mock_screen_observer_.get();
    ASSERT_TRUE(network_screen_->actor() != NULL);

    mock_network_state_helper_ = new login::MockNetworkStateHelper;
    SetDefaultNetworkStateHelperExpectations();
    network_screen_->SetNetworkStateHelperForTest(mock_network_state_helper_);
  }

  virtual void TearDownInProcessBrowserTestFixture() OVERRIDE {
    CrosInProcessBrowserTest::TearDownInProcessBrowserTestFixture();
    DBusThreadManager::Shutdown();
  }

  void EmulateContinueButtonExit(NetworkScreen* network_screen) {
    EXPECT_CALL(*mock_screen_observer_,
                OnExit(ScreenObserver::NETWORK_CONNECTED))
        .Times(1);
    EXPECT_CALL(*mock_network_state_helper_, IsConnected())
        .WillOnce(Return(true));
    network_screen->OnContinuePressed();
    content::RunAllPendingInMessageLoop();
  }

  void SetDefaultNetworkStateHelperExpectations() {
    EXPECT_CALL(*mock_network_state_helper_, GetCurrentNetworkName())
        .Times(AnyNumber())
        .WillRepeatedly((Return(string16())));
    EXPECT_CALL(*mock_network_state_helper_, IsConnected())
        .Times(AnyNumber())
        .WillRepeatedly((Return(false)));
    EXPECT_CALL(*mock_network_state_helper_, IsConnecting())
        .Times(AnyNumber())
        .WillRepeatedly((Return(false)));
  }

  scoped_ptr<MockScreenObserver> mock_screen_observer_;
  login::MockNetworkStateHelper* mock_network_state_helper_;
  NetworkScreen* network_screen_;
  FakeSessionManagerClient* fake_session_manager_client_;

 private:
  DISALLOW_COPY_AND_ASSIGN(NetworkScreenTest);
};

IN_PROC_BROWSER_TEST_F(NetworkScreenTest, CanConnect) {
  EXPECT_CALL(*mock_network_state_helper_, IsConnecting())
      .WillOnce((Return(true)));
  // EXPECT_FALSE(actor_->IsContinueEnabled());
  network_screen_->NetworkManagerChanged();

  EXPECT_CALL(*mock_network_state_helper_, IsConnected())
      .Times(2)
      .WillRepeatedly(Return(true));
  // TODO(nkostylev): Add integration with WebUI actor http://crosbug.com/22570
  // EXPECT_FALSE(actor_->IsContinueEnabled());
  // EXPECT_FALSE(actor_->IsConnecting());
  network_screen_->NetworkManagerChanged();

  // EXPECT_TRUE(actor_->IsContinueEnabled());
  EmulateContinueButtonExit(network_screen_);
  EXPECT_EQ(
      1, fake_session_manager_client_->emit_login_prompt_ready_call_count());
}

IN_PROC_BROWSER_TEST_F(NetworkScreenTest, Timeout) {
  EXPECT_CALL(*mock_network_state_helper_, IsConnecting())
      .WillOnce((Return(true)));
  // EXPECT_FALSE(actor_->IsContinueEnabled());
  network_screen_->NetworkManagerChanged();

  EXPECT_CALL(*mock_network_state_helper_, IsConnected())
      .Times(2)
      .WillRepeatedly(Return(false));
  // TODO(nkostylev): Add integration with WebUI actor http://crosbug.com/22570
  // EXPECT_FALSE(actor_->IsContinueEnabled());
  // EXPECT_FALSE(actor_->IsConnecting());
  network_screen_->OnConnectionTimeout();

  // Close infobubble with error message - it makes the test stable.
  // EXPECT_FALSE(actor_->IsContinueEnabled());
  // EXPECT_FALSE(actor_->IsConnecting());
  // actor_->ClearErrors();
  EXPECT_EQ(
      1, fake_session_manager_client_->emit_login_prompt_ready_call_count());
}

}  // namespace chromeos
