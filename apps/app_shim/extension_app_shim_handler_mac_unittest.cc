// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/app_shim/extension_app_shim_handler_mac.h"

#include "apps/app_shim/app_shim_host_mac.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/browser/notification_service.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace apps {

using extensions::Extension;
typedef ShellWindowRegistry::ShellWindowList ShellWindowList;

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

class MockDelegate : public ExtensionAppShimHandler::Delegate {
 public:
  virtual ~MockDelegate() {}

  MOCK_METHOD1(ProfileExistsForPath, bool(const base::FilePath&));
  MOCK_METHOD1(ProfileForPath, Profile*(const base::FilePath&));
  MOCK_METHOD2(LoadProfileAsync,
               void(const base::FilePath&,
                    base::Callback<void(Profile*)>));

  MOCK_METHOD2(GetWindows, ShellWindowList(Profile*, const std::string&));

  MOCK_METHOD2(GetAppExtension, const Extension*(Profile*, const std::string&));
  MOCK_METHOD2(LaunchApp, void(Profile*, const Extension*));
  MOCK_METHOD2(LaunchShim, void(Profile*, const Extension*));

  MOCK_METHOD0(MaybeTerminate, void());

  void CaptureLoadProfileCallback(
      const base::FilePath& path,
      base::Callback<void(Profile*)> callback) {
    callbacks_[path] = callback;
  }

  bool RunLoadProfileCallback(
      const base::FilePath& path,
      Profile* profile) {
    callbacks_[path].Run(profile);
    return callbacks_.erase(path);
  }

 private:
  std::map<base::FilePath,
           base::Callback<void(Profile*)> > callbacks_;
};

class TestingExtensionAppShimHandler : public ExtensionAppShimHandler {
 public:
  TestingExtensionAppShimHandler(Delegate* delegate) {
    set_delegate(delegate);
  }
  virtual ~TestingExtensionAppShimHandler() {}

  MOCK_METHOD2(OnShimFocus, void(Host* host, AppShimFocusType));

  void RealOnShimFocus(Host* host, AppShimFocusType focus_type) {
    ExtensionAppShimHandler::OnShimFocus(host, focus_type);
  }

  AppShimHandler::Host* FindHost(Profile* profile,
                                 const std::string& app_id) {
    HostMap::const_iterator it = hosts().find(make_pair(profile, app_id));
    return it == hosts().end() ? NULL : it->second;
  }

  content::NotificationRegistrar& GetRegistrar() { return registrar(); }

 private:
  DISALLOW_COPY_AND_ASSIGN(TestingExtensionAppShimHandler);
};

const char kTestAppIdA[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const char kTestAppIdB[] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

class FakeHost : public apps::AppShimHandler::Host {
 public:
  FakeHost(const base::FilePath& profile_path,
           const std::string& app_id,
           TestingExtensionAppShimHandler* handler)
      : profile_path_(profile_path),
        app_id_(app_id),
        handler_(handler),
        close_count_(0) {}

  MOCK_METHOD1(OnAppLaunchComplete, void(AppShimLaunchResult));

  virtual void OnAppClosed() OVERRIDE {
    handler_->OnShimClose(this);
    ++close_count_;
  }
  virtual base::FilePath GetProfilePath() const OVERRIDE {
    return profile_path_;
  }
  virtual std::string GetAppId() const OVERRIDE { return app_id_; }

  int close_count() { return close_count_; }

 private:
  base::FilePath profile_path_;
  std::string app_id_;
  TestingExtensionAppShimHandler* handler_;
  int close_count_;

  DISALLOW_COPY_AND_ASSIGN(FakeHost);
};

class ExtensionAppShimHandlerTest : public testing::Test {
 protected:
  ExtensionAppShimHandlerTest()
      : delegate_(new MockDelegate),
        handler_(new TestingExtensionAppShimHandler(delegate_)),
        profile_path_a_("Profile A"),
        profile_path_b_("Profile B"),
        host_aa_(profile_path_a_, kTestAppIdA, handler_.get()),
        host_ab_(profile_path_a_, kTestAppIdB, handler_.get()),
        host_bb_(profile_path_b_, kTestAppIdB, handler_.get()),
        host_aa_duplicate_(profile_path_a_, kTestAppIdA, handler_.get()) {
    base::FilePath extension_path("/fake/path");
    base::DictionaryValue manifest;
    manifest.SetString("name", "Fake Name");
    manifest.SetString("version", "1");
    std::string error;
    extension_a_ = Extension::Create(
        extension_path, extensions::Manifest::INTERNAL, manifest,
        Extension::NO_FLAGS, kTestAppIdA, &error);
    EXPECT_TRUE(extension_a_.get()) << error;

    extension_b_ = Extension::Create(
        extension_path, extensions::Manifest::INTERNAL, manifest,
        Extension::NO_FLAGS, kTestAppIdB, &error);
    EXPECT_TRUE(extension_b_.get()) << error;

    EXPECT_CALL(*delegate_, ProfileExistsForPath(profile_path_a_))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*delegate_, ProfileForPath(profile_path_a_))
        .WillRepeatedly(Return(&profile_a_));
    EXPECT_CALL(*delegate_, ProfileExistsForPath(profile_path_b_))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*delegate_, ProfileForPath(profile_path_b_))
        .WillRepeatedly(Return(&profile_b_));

    // In most tests, we don't care about the result of GetWindows, it just
    // needs to be non-empty.
    ShellWindowList shell_window_list;
    shell_window_list.push_back(static_cast<ShellWindow*>(NULL));
    EXPECT_CALL(*delegate_, GetWindows(_, _))
        .WillRepeatedly(Return(shell_window_list));

    EXPECT_CALL(*delegate_, GetAppExtension(_, kTestAppIdA))
        .WillRepeatedly(Return(extension_a_.get()));
    EXPECT_CALL(*delegate_, GetAppExtension(_, kTestAppIdB))
        .WillRepeatedly(Return(extension_b_.get()));
    EXPECT_CALL(*delegate_, LaunchApp(_,_))
        .WillRepeatedly(Return());
  }

  MockDelegate* delegate_;
  scoped_ptr<TestingExtensionAppShimHandler> handler_;
  base::FilePath profile_path_a_;
  base::FilePath profile_path_b_;
  TestingProfile profile_a_;
  TestingProfile profile_b_;
  FakeHost host_aa_;
  FakeHost host_ab_;
  FakeHost host_bb_;
  FakeHost host_aa_duplicate_;
  scoped_refptr<Extension> extension_a_;
  scoped_refptr<Extension> extension_b_;

 private:
  DISALLOW_COPY_AND_ASSIGN(ExtensionAppShimHandlerTest);
};

TEST_F(ExtensionAppShimHandlerTest, LaunchFailure) {
  // Bad profile path.
  EXPECT_CALL(*delegate_, ProfileExistsForPath(profile_path_a_))
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(host_aa_, OnAppLaunchComplete(APP_SHIM_LAUNCH_PROFILE_NOT_FOUND));
  handler_->OnShimLaunch(&host_aa_, APP_SHIM_LAUNCH_NORMAL);

  // App not found.
  EXPECT_CALL(*delegate_, GetAppExtension(&profile_a_, kTestAppIdA))
      .WillOnce(Return(static_cast<const Extension*>(NULL)))
      .WillRepeatedly(Return(extension_a_.get()));
  EXPECT_CALL(host_aa_, OnAppLaunchComplete(APP_SHIM_LAUNCH_APP_NOT_FOUND));
  handler_->OnShimLaunch(&host_aa_, APP_SHIM_LAUNCH_NORMAL);
}

TEST_F(ExtensionAppShimHandlerTest, LaunchAndCloseShim) {
  const AppShimLaunchType normal_launch = APP_SHIM_LAUNCH_NORMAL;

  // Normal startup.
  handler_->OnShimLaunch(&host_aa_, normal_launch);
  EXPECT_EQ(&host_aa_, handler_->FindHost(&profile_a_, kTestAppIdA));

  handler_->OnShimLaunch(&host_ab_, normal_launch);
  EXPECT_EQ(&host_ab_, handler_->FindHost(&profile_a_, kTestAppIdB));

  handler_->OnShimLaunch(&host_bb_, normal_launch);
  EXPECT_EQ(&host_bb_, handler_->FindHost(&profile_b_, kTestAppIdB));

  // Activation when there is a registered shim finishes launch with success and
  // focuses the app.
  EXPECT_CALL(host_aa_, OnAppLaunchComplete(APP_SHIM_LAUNCH_SUCCESS));
  EXPECT_CALL(*handler_, OnShimFocus(&host_aa_, APP_SHIM_FOCUS_NORMAL));
  handler_->OnAppActivated(&profile_a_, kTestAppIdA);

  // Starting and closing a second host just focuses the app.
  EXPECT_CALL(*handler_, OnShimFocus(&host_aa_duplicate_,
                                     APP_SHIM_FOCUS_REOPEN));
  EXPECT_CALL(host_aa_duplicate_,
              OnAppLaunchComplete(APP_SHIM_LAUNCH_DUPLICATE_HOST));
  handler_->OnShimLaunch(&host_aa_duplicate_, normal_launch);
  EXPECT_EQ(&host_aa_, handler_->FindHost(&profile_a_, kTestAppIdA));
  handler_->OnShimClose(&host_aa_duplicate_);
  EXPECT_EQ(&host_aa_, handler_->FindHost(&profile_a_, kTestAppIdA));

  // Normal close.
  handler_->OnShimClose(&host_aa_);
  EXPECT_FALSE(handler_->FindHost(&profile_a_, kTestAppIdA));

  // Closing the second host afterward does nothing.
  handler_->OnShimClose(&host_aa_duplicate_);
  EXPECT_FALSE(handler_->FindHost(&profile_a_, kTestAppIdA));
}

TEST_F(ExtensionAppShimHandlerTest, AppLifetime) {
  // When the app activates, if there is no shim, start one.
  EXPECT_CALL(*delegate_, LaunchShim(&profile_a_, extension_a_.get()));
  handler_->OnAppActivated(&profile_a_, kTestAppIdA);

  // Normal shim launch adds an entry in the map.
  // App should not be launched here, but return success to the shim.
  EXPECT_CALL(*delegate_, LaunchApp(&profile_a_, extension_a_.get()))
      .Times(0);
  EXPECT_CALL(host_aa_, OnAppLaunchComplete(APP_SHIM_LAUNCH_SUCCESS));
  handler_->OnShimLaunch(&host_aa_, APP_SHIM_LAUNCH_REGISTER_ONLY);
  EXPECT_EQ(&host_aa_, handler_->FindHost(&profile_a_, kTestAppIdA));

  // Closing all windows does not quit the shim.
  handler_->OnAppDeactivated(&profile_a_, kTestAppIdA);
  EXPECT_EQ(0, host_aa_.close_count());

  // Return no shell windows for OnShimFocus and OnShimQuit.
  ShellWindowList shell_window_list;
  EXPECT_CALL(*delegate_, GetWindows(&profile_a_, kTestAppIdA))
      .WillRepeatedly(Return(shell_window_list));

  // Non-reopen focus does nothing.
  EXPECT_CALL(*handler_, OnShimFocus(&host_aa_, APP_SHIM_FOCUS_NORMAL))
      .WillOnce(Invoke(handler_.get(),
                       &TestingExtensionAppShimHandler::RealOnShimFocus));
  EXPECT_CALL(*delegate_, LaunchApp(&profile_a_, extension_a_.get()))
      .Times(0);
  handler_->OnShimFocus(&host_aa_, APP_SHIM_FOCUS_NORMAL);

  // Reopen focus launches the app.
  EXPECT_CALL(*handler_, OnShimFocus(&host_aa_, APP_SHIM_FOCUS_REOPEN))
      .WillOnce(Invoke(handler_.get(),
                       &TestingExtensionAppShimHandler::RealOnShimFocus));
  EXPECT_CALL(*delegate_, LaunchApp(&profile_a_, extension_a_.get()));
  handler_->OnShimFocus(&host_aa_, APP_SHIM_FOCUS_REOPEN);

  // Quit closes the shim.
  EXPECT_CALL(*delegate_, MaybeTerminate())
      .WillOnce(Return());
  handler_->OnShimQuit(&host_aa_);
  EXPECT_EQ(1, host_aa_.close_count());
}

TEST_F(ExtensionAppShimHandlerTest, MaybeTerminate) {
  const AppShimLaunchType register_only = APP_SHIM_LAUNCH_REGISTER_ONLY;

  // Launch shims, adding entries in the map.
  EXPECT_CALL(host_aa_, OnAppLaunchComplete(APP_SHIM_LAUNCH_SUCCESS));
  handler_->OnShimLaunch(&host_aa_, register_only);
  EXPECT_EQ(&host_aa_, handler_->FindHost(&profile_a_, kTestAppIdA));

  EXPECT_CALL(host_ab_, OnAppLaunchComplete(APP_SHIM_LAUNCH_SUCCESS));
  handler_->OnShimLaunch(&host_ab_, register_only);
  EXPECT_EQ(&host_ab_, handler_->FindHost(&profile_a_, kTestAppIdB));

  // The following quits should not terminate.
  ShellWindowList shell_window_list;
  EXPECT_CALL(*delegate_, GetWindows(_, _))
      .WillRepeatedly(Return(shell_window_list));
  EXPECT_CALL(*delegate_, MaybeTerminate())
      .Times(0);

  // Quit when there are other shims.
  handler_->OnShimQuit(&host_aa_);

  // Quit after a browser window has opened.
  handler_->Observe(chrome::NOTIFICATION_BROWSER_OPENED,
                    content::NotificationService::AllSources(),
                    content::NotificationService::NoDetails());
  handler_->OnShimQuit(&host_ab_);
}

TEST_F(ExtensionAppShimHandlerTest, RegisterOnly) {
  // For an APP_SHIM_LAUNCH_REGISTER_ONLY, don't launch the app.
  EXPECT_CALL(*delegate_, LaunchApp(_, _))
      .Times(0);
  EXPECT_CALL(host_aa_, OnAppLaunchComplete(APP_SHIM_LAUNCH_SUCCESS));
  handler_->OnShimLaunch(&host_aa_, APP_SHIM_LAUNCH_REGISTER_ONLY);
  EXPECT_TRUE(handler_->FindHost(&profile_a_, kTestAppIdA));

  // Close the shim, removing the entry in the map.
  handler_->OnShimClose(&host_aa_);
  EXPECT_FALSE(handler_->FindHost(&profile_a_, kTestAppIdA));
}

TEST_F(ExtensionAppShimHandlerTest, LoadProfile) {
  // If the profile is not loaded when an OnShimLaunch arrives, return false
  // and load the profile asynchronously. Launch the app when the profile is
  // ready.
  EXPECT_CALL(*delegate_, ProfileForPath(profile_path_a_))
      .WillOnce(Return(static_cast<Profile*>(NULL)))
      .WillRepeatedly(Return(&profile_a_));
  EXPECT_CALL(*delegate_, LoadProfileAsync(profile_path_a_, _))
      .WillOnce(Invoke(delegate_, &MockDelegate::CaptureLoadProfileCallback));
  handler_->OnShimLaunch(&host_aa_, APP_SHIM_LAUNCH_NORMAL);
  EXPECT_FALSE(handler_->FindHost(&profile_a_, kTestAppIdA));
  delegate_->RunLoadProfileCallback(profile_path_a_, &profile_a_);
  EXPECT_TRUE(handler_->FindHost(&profile_a_, kTestAppIdA));
}

}  // namespace apps
