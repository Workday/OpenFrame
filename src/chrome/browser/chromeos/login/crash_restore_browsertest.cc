// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/memory/ref_counted.h"
#include "base/run_loop.h"
#include "chrome/browser/chromeos/cros/cros_in_process_browser_test.h"
#include "chrome/browser/chromeos/login/user.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/common/chrome_switches.h"
#include "chromeos/chromeos_switches.h"
#include "chromeos/dbus/cryptohome_client.h"
#include "chromeos/dbus/fake_session_manager_client.h"
#include "chromeos/dbus/mock_dbus_thread_manager_without_gmock.h"
#include "chromeos/dbus/session_manager_client.h"
#include "content/public/test/test_utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace chromeos {

namespace {

const char kUserId1[] = "user1@example.com";
const char kUserId2[] = "user2@example.com";
const char kUserId3[] = "user3@example.com";

}  // namespace

class CrashRestoreSimpleTest : public CrosInProcessBrowserTest {
 protected:
  CrashRestoreSimpleTest() {}

  virtual ~CrashRestoreSimpleTest() {}

  virtual void SetUpCommandLine(CommandLine* command_line) OVERRIDE {
    command_line->AppendSwitch(::switches::kMultiProfiles);
    command_line->AppendSwitchASCII(switches::kLoginUser, kUserId1);
    command_line->AppendSwitchASCII(
        switches::kLoginProfile,
        CryptohomeClient::GetStubSanitizedUsername(kUserId1));
  }

  virtual void SetUpInProcessBrowserTestFixture() OVERRIDE {
    // Redirect session_manager DBus calls to FakeSessionManagerClient.
    MockDBusThreadManagerWithoutGMock* dbus_thread_manager =
        new MockDBusThreadManagerWithoutGMock();
    session_manager_client_ =
        dbus_thread_manager->fake_session_manager_client();
    DBusThreadManager::InitializeForTesting(dbus_thread_manager);
    session_manager_client_->StartSession(kUserId1);
  }

  FakeSessionManagerClient* session_manager_client_;
};

IN_PROC_BROWSER_TEST_F(CrashRestoreSimpleTest, RestoreSessionForOneUser) {
  UserManager* user_manager = UserManager::Get();
  User* user = user_manager->GetActiveUser();
  ASSERT_TRUE(user);
  EXPECT_EQ(kUserId1, user->email());
  EXPECT_EQ(CryptohomeClient::GetStubSanitizedUsername(kUserId1),
            user->username_hash());
  EXPECT_EQ(1UL, user_manager->GetLoggedInUsers().size());
}

// Observer that keeps track of user sessions restore event.
class UserSessionRestoreObserver :
    public UserManager::UserSessionStateObserver {
 public:
  UserSessionRestoreObserver()
      : running_loop_(false),
        user_sessions_restored_(UserManager::Get()->UserSessionsRestored()) {
    if (!user_sessions_restored_)
      UserManager::Get()->AddSessionStateObserver(this);
  }
  virtual ~UserSessionRestoreObserver() {}

  virtual void PendingUserSessionsRestoreFinished() OVERRIDE {
    user_sessions_restored_ = true;
    UserManager::Get()->RemoveSessionStateObserver(this);
    if (!running_loop_)
      return;

    message_loop_runner_->Quit();
    running_loop_ = false;
  }

  // Wait until the user sessions are restored. If that happened between the
  // construction of this object and this call or even before it was created
  // then it returns immediately.
  void Wait() {
    if (user_sessions_restored_)
      return;

    running_loop_ = true;
    message_loop_runner_ = new content::MessageLoopRunner();
    message_loop_runner_->Run();
  }

 private:
  bool running_loop_;
  bool user_sessions_restored_;
  scoped_refptr<content::MessageLoopRunner> message_loop_runner_;

  DISALLOW_COPY_AND_ASSIGN(UserSessionRestoreObserver);
};

class CrashRestoreComplexTest : public CrashRestoreSimpleTest {
 protected:
  CrashRestoreComplexTest() {}
  virtual ~CrashRestoreComplexTest() {}

  virtual void SetUpInProcessBrowserTestFixture() OVERRIDE {
    CrashRestoreSimpleTest::SetUpInProcessBrowserTestFixture();
    session_manager_client_->StartSession(kUserId2);
    session_manager_client_->StartSession(kUserId3);
  }
};

IN_PROC_BROWSER_TEST_F(CrashRestoreComplexTest, RestoreSessionForThreeUsers) {
  {
    UserSessionRestoreObserver restore_observer;
    restore_observer.Wait();
  }

  UserManager* user_manager = UserManager::Get();
  DCHECK(user_manager->UserSessionsRestored());

  // User that is last in the user sessions map becomes active. This behavior
  // will become better defined once each user gets a separate user desktop.
  User* user = user_manager->GetActiveUser();
  ASSERT_TRUE(user);
  EXPECT_EQ(kUserId3, user->email());
  EXPECT_EQ(CryptohomeClient::GetStubSanitizedUsername(kUserId3),
            user->username_hash());
  const UserList& users = user_manager->GetLoggedInUsers();
  ASSERT_EQ(3UL, users.size());

  // User that becomes active moves to the beginning of the list.
  EXPECT_EQ(kUserId3, users[0]->email());
  EXPECT_EQ(CryptohomeClient::GetStubSanitizedUsername(kUserId3),
            users[0]->username_hash());
  EXPECT_EQ(kUserId2, users[1]->email());
  EXPECT_EQ(CryptohomeClient::GetStubSanitizedUsername(kUserId2),
            users[1]->username_hash());
  EXPECT_EQ(kUserId1, users[2]->email());
  EXPECT_EQ(CryptohomeClient::GetStubSanitizedUsername(kUserId1),
            users[2]->username_hash());
}

}  // namespace chromeos
