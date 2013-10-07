// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/shell.h"
#include "base/command_line.h"
#include "chrome/browser/chrome_browser_main.h"
#include "chrome/browser/chrome_browser_main_extra_parts.h"
#include "chrome/browser/chrome_content_browser_client.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/chromeos/cros/cros_in_process_browser_test.h"
#include "chrome/browser/chromeos/login/login_display_host_impl.h"
#include "chrome/browser/chromeos/login/login_wizard.h"
#include "chrome/browser/chromeos/login/wizard_controller.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "chromeos/chromeos_switches.h"
#include "chromeos/cryptohome/mock_cryptohome_library.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_service.h"
#include "content/public/test/test_utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Return;

namespace {

class LoginTestBase : public chromeos::CrosInProcessBrowserTest {
 public:
  LoginTestBase() {}

 protected:
  virtual void SetUpInProcessBrowserTestFixture() OVERRIDE {
    CrosInProcessBrowserTest::SetUpInProcessBrowserTestFixture();

    mock_cryptohome_library_.reset(new chromeos::MockCryptohomeLibrary());
    EXPECT_CALL(*(mock_cryptohome_library_.get()), GetSystemSalt())
        .WillRepeatedly(Return(std::string("stub_system_salt")));
    EXPECT_CALL(*(mock_cryptohome_library_.get()), InstallAttributesIsReady())
        .WillRepeatedly(Return(false));
  }

  scoped_ptr<chromeos::MockCryptohomeLibrary> mock_cryptohome_library_;

 private:
  DISALLOW_COPY_AND_ASSIGN(LoginTestBase);
};

class LoginUserTest : public LoginTestBase {
 protected:
  virtual void SetUpInProcessBrowserTestFixture() OVERRIDE {
    LoginTestBase::SetUpInProcessBrowserTestFixture();
  }

  virtual void SetUpCommandLine(CommandLine* command_line) OVERRIDE {
    command_line->AppendSwitchASCII(
        chromeos::switches::kLoginUser, "TestUser@gmail.com");
    command_line->AppendSwitchASCII(chromeos::switches::kLoginProfile, "user");
  }
};

class LoginGuestTest : public LoginTestBase {
 protected:
  virtual void SetUpCommandLine(CommandLine* command_line) OVERRIDE {
    command_line->AppendSwitch(chromeos::switches::kGuestSession);
    command_line->AppendSwitch(::switches::kIncognito);
    command_line->AppendSwitchASCII(chromeos::switches::kLoginProfile, "user");
  }
};

class LoginCursorTest : public LoginTestBase {
 protected:
  virtual void SetUpCommandLine(CommandLine* command_line) OVERRIDE {
    command_line->AppendSwitch(chromeos::switches::kLoginManager);
  }
};

// Used to add an observer to NotificationService after it's created.
class TestBrowserMainExtraParts
    : public ChromeBrowserMainExtraParts,
      public content::NotificationObserver {
 public:
  TestBrowserMainExtraParts() {}
  virtual ~TestBrowserMainExtraParts() {}

  // ChromeBrowserMainExtraParts implementation.
  virtual void PreEarlyInitialization() OVERRIDE {
    registrar_.Add(this, chrome::NOTIFICATION_LOGIN_OR_LOCK_WEBUI_VISIBLE,
                   content::NotificationService::AllSources());
  }

  void set_quit_task(const base::Closure& quit_task) { quit_task_ = quit_task; }

 private:
  // Overridden from content::NotificationObserver:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE {
    quit_task_.Run();
  }

  content::NotificationRegistrar registrar_;
  base::Closure quit_task_;

  DISALLOW_COPY_AND_ASSIGN(TestBrowserMainExtraParts);
};

class TestContentBrowserClient : public chrome::ChromeContentBrowserClient {
 public:
  TestContentBrowserClient() {}
  virtual ~TestContentBrowserClient() {}

  virtual content::BrowserMainParts* CreateBrowserMainParts(
      const content::MainFunctionParams& parameters) OVERRIDE {
    ChromeBrowserMainParts* main_parts = static_cast<ChromeBrowserMainParts*>(
        ChromeContentBrowserClient::CreateBrowserMainParts(parameters));

    browser_main_extra_parts_ = new TestBrowserMainExtraParts();
    main_parts->AddParts(browser_main_extra_parts_);
    return main_parts;
  }

  TestBrowserMainExtraParts* browser_main_extra_parts_;

 private:
  DISALLOW_COPY_AND_ASSIGN(TestContentBrowserClient);
};


class LoginSigninTest : public chromeos::CrosInProcessBrowserTest {
 protected:
  virtual void SetUpCommandLine(CommandLine* command_line) OVERRIDE {
    command_line->AppendSwitch(chromeos::switches::kLoginManager);
    command_line->AppendSwitch(chromeos::switches::kForceLoginManagerInTests);
  }

  virtual void SetUpInProcessBrowserTestFixture() OVERRIDE {
    content_browser_client_.reset(new TestContentBrowserClient());
    original_content_browser_client_ = content::SetBrowserClientForTesting(
        content_browser_client_.get());
  }

  virtual void TearDownInProcessBrowserTestFixture() OVERRIDE {
    content::SetBrowserClientForTesting(original_content_browser_client_);
  }

  scoped_ptr<TestContentBrowserClient> content_browser_client_;
  content::ContentBrowserClient* original_content_browser_client_;
};

// After a chrome crash, the session manager will restart chrome with
// the -login-user flag indicating that the user is already logged in.
// This profile should NOT be an OTR profile.
IN_PROC_BROWSER_TEST_F(LoginUserTest, UserPassed) {
  Profile* profile = browser()->profile();
  EXPECT_EQ("user", profile->GetPath().BaseName().value());
  EXPECT_FALSE(profile->IsOffTheRecord());
}

// Verifies the cursor is not hidden at startup when user is logged in.
IN_PROC_BROWSER_TEST_F(LoginUserTest, CursorShown) {
  EXPECT_TRUE(ash::Shell::GetInstance()->cursor_manager()->IsCursorVisible());
}

// After a guest login, we should get the OTR default profile.
IN_PROC_BROWSER_TEST_F(LoginGuestTest, GuestIsOTR) {
  Profile* profile = browser()->profile();
  EXPECT_EQ("Default", profile->GetPath().BaseName().value());
  EXPECT_TRUE(profile->IsOffTheRecord());
  // Ensure there's extension service for this profile.
  EXPECT_TRUE(extensions::ExtensionSystem::Get(profile)->extension_service());
}

// Verifies the cursor is not hidden at startup when running guest session.
IN_PROC_BROWSER_TEST_F(LoginGuestTest, CursorShown) {
  EXPECT_TRUE(ash::Shell::GetInstance()->cursor_manager()->IsCursorVisible());
}

// Verifies the cursor is hidden at startup on login screen.
IN_PROC_BROWSER_TEST_F(LoginCursorTest, CursorHidden) {
  // Login screen needs to be shown explicitly when running test.
  chromeos::ShowLoginWizard(chromeos::WizardController::kLoginScreenName);

  // Cursor should be hidden at startup
  EXPECT_FALSE(ash::Shell::GetInstance()->cursor_manager()->IsCursorVisible());

  // Cursor should be shown after cursor is moved.
  EXPECT_TRUE(ui_test_utils::SendMouseMoveSync(gfx::Point()));
  EXPECT_TRUE(ash::Shell::GetInstance()->cursor_manager()->IsCursorVisible());

  base::MessageLoop::current()->DeleteSoon(
      FROM_HERE, chromeos::LoginDisplayHostImpl::default_host());
}

// Verifies that the webui for login comes up successfully.
IN_PROC_BROWSER_TEST_F(LoginSigninTest, WebUIVisible) {
  scoped_refptr<content::MessageLoopRunner> runner =
      new content::MessageLoopRunner;
  content_browser_client_->browser_main_extra_parts_->set_quit_task(
      runner->QuitClosure());
  runner->Run();
}

}  // namespace
