// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/fake_login_utils.h"

#include "base/command_line.h"
#include "base/path_service.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chromeos/login/login_display_host.h"
#include "chrome/browser/chromeos/login/mock_authenticator.h"
#include "chrome/browser/first_run/first_run.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/startup/startup_browser_creator.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/test/base/testing_profile.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace chromeos {

FakeLoginUtils::FakeLoginUtils() : should_launch_browser_(false) {}

FakeLoginUtils::~FakeLoginUtils() {}

void FakeLoginUtils::DoBrowserLaunch(Profile* profile,
                                     LoginDisplayHost* login_host) {
  if (should_launch_browser_) {
    StartupBrowserCreator browser_creator;
    chrome::startup::IsFirstRun first_run =
        first_run::IsChromeFirstRun() ? chrome::startup::IS_FIRST_RUN
                                      : chrome::startup::IS_NOT_FIRST_RUN;
    ASSERT_TRUE(
        browser_creator.LaunchBrowser(*CommandLine::ForCurrentProcess(),
                                      profile,
                                      base::FilePath(),
                                      chrome::startup::IS_PROCESS_STARTUP,
                                      first_run,
                                      NULL));
  }
  if (login_host)
    login_host->Finalize();
}

void FakeLoginUtils::PrepareProfile(const UserContext& user_context,
                                    const std::string& display_email,
                                    bool using_oauth,
                                    bool has_cookies,
                                    bool has_active_session,
                                    LoginUtils::Delegate* delegate) {
  UserManager::Get()->UserLoggedIn(
      user_context.username, user_context.username_hash, false);
  Profile* profile;
  if (should_launch_browser_) {
    profile = CreateProfile(user_context.username);
  } else {
    profile = new TestingProfile();
    g_browser_process->profile_manager()->
        RegisterTestingProfile(profile, false, false);
  }
  if (delegate)
    delegate->OnProfilePrepared(profile);
}

void FakeLoginUtils::DelegateDeleted(LoginUtils::Delegate* delegate) {
  NOTREACHED() << "Method not implemented.";
}

void FakeLoginUtils::CompleteOffTheRecordLogin(const GURL& start_url) {
  NOTREACHED() << "Method not implemented.";
}

void FakeLoginUtils::SetFirstLoginPrefs(PrefService* prefs) {
  NOTREACHED() << "Method not implemented.";
}

scoped_refptr<Authenticator> FakeLoginUtils::CreateAuthenticator(
    LoginStatusConsumer* consumer) {
  authenticator_ =
      new MockAuthenticator(consumer, expected_username_, expected_password_);
  return authenticator_;
}

void FakeLoginUtils::RestoreAuthenticationSession(Profile* profile) {
  NOTREACHED() << "Method not implemented.";
}

void FakeLoginUtils::StopBackgroundFetchers() {
  NOTREACHED() << "Method not implemented.";
}

void FakeLoginUtils::InitRlzDelayed(Profile* user_profile) {
  NOTREACHED() << "Method not implemented.";
}

Profile* FakeLoginUtils::CreateProfile(const std::string& username_hash) {
  base::FilePath path;
  PathService::Get(chrome::DIR_USER_DATA, &path);
  path = path.AppendASCII(chrome::kProfileDirPrefix + username_hash);
  Profile* profile = g_browser_process->profile_manager()->GetProfile(path);
  return profile;
}

void FakeLoginUtils::SetExpectedCredentials(const std::string& username,
                                            const std::string& password) {
  expected_username_ = username;
  expected_password_ = password;
  if (authenticator_.get()) {
    static_cast<MockAuthenticator*>(authenticator_.get())->
        SetExpectedCredentials(username, password);
  }
}

}  //  namespace chromeos
