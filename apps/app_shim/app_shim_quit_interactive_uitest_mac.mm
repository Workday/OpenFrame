// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests behavior when quitting apps with app shims.

#import <Cocoa/Cocoa.h>

#include "apps/app_shim/app_shim_host_manager_mac.h"
#include "apps/app_shim/extension_app_shim_handler_mac.h"
#include "apps/switches.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browser_shutdown.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_test_message_listener.h"
#include "chrome/browser/extensions/platform_app_browsertest_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "content/public/test/test_utils.h"
#include "ui/base/test/cocoa_test_event_utils.h"

using extensions::PlatformAppBrowserTest;

namespace apps {

namespace {

class FakeHost : public apps::AppShimHandler::Host {
 public:
  FakeHost(const base::FilePath& profile_path,
           const std::string& app_id,
           ExtensionAppShimHandler* handler)
      : profile_path_(profile_path),
        app_id_(app_id),
        handler_(handler) {}

  virtual void OnAppLaunchComplete(AppShimLaunchResult result) OVERRIDE {}
  virtual void OnAppClosed() OVERRIDE {
    handler_->OnShimClose(this);
  }
  virtual base::FilePath GetProfilePath() const OVERRIDE {
    return profile_path_;
  }
  virtual std::string GetAppId() const OVERRIDE { return app_id_; }

 private:
  base::FilePath profile_path_;
  std::string app_id_;
  ExtensionAppShimHandler* handler_;

  DISALLOW_COPY_AND_ASSIGN(FakeHost);
};

// Starts an app without a browser window using --load_and_launch_app and
// --silent_launch.
class AppShimQuitTest : public PlatformAppBrowserTest {
 protected:
  AppShimQuitTest() {}

  void SetUpAppShim() {
    ASSERT_EQ(0u, [[NSApp windows] count]);
    ExtensionTestMessageListener launched_listener("Launched", false);
    ASSERT_TRUE(launched_listener.WaitUntilSatisfied());
    ASSERT_EQ(1u, [[NSApp windows] count]);

    handler_ = g_browser_process->platform_part()->app_shim_host_manager()->
        extension_app_shim_handler();

    // Attach a host for the app.
    extension_id_ =
        GetExtensionByPath(extension_service()->extensions(), app_path_)->id();
    host_.reset(new FakeHost(profile()->GetPath().BaseName(),
                             extension_id_,
                             handler_));
    handler_->OnShimLaunch(host_.get(), APP_SHIM_LAUNCH_REGISTER_ONLY);
    EXPECT_EQ(host_.get(), handler_->FindHost(profile(), extension_id_));

    // Focus the app window.
    NSWindow* window = [[NSApp windows] objectAtIndex:0];
    EXPECT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(window));
    content::RunAllPendingInMessageLoop();
  }

  virtual void SetUpCommandLine(CommandLine* command_line) OVERRIDE {
    PlatformAppBrowserTest::SetUpCommandLine(command_line);
    // Simulate an app shim initiated launch, i.e. launch app but not browser.
    app_path_ = test_data_dir_
        .AppendASCII("platform_apps")
        .AppendASCII("minimal");
    command_line->AppendSwitchNative(apps::kLoadAndLaunchApp,
                                     app_path_.value());
    command_line->AppendSwitch(switches::kSilentLaunch);
  }

  base::FilePath app_path_;
  ExtensionAppShimHandler* handler_;
  std::string extension_id_;
  scoped_ptr<FakeHost> host_;

  DISALLOW_COPY_AND_ASSIGN(AppShimQuitTest);
};

}  // namespace

// Test that closing an app with Cmd+Q when no browsers have ever been open
// terminates Chrome.
IN_PROC_BROWSER_TEST_F(AppShimQuitTest, QuitWithKeyEvent) {
  SetUpAppShim();

  // Simulate a Cmd+Q event.
  NSWindow* window = [[NSApp windows] objectAtIndex:0];
  NSEvent* event = cocoa_test_event_utils::KeyEventWithKeyCode(
      0, 'q', NSKeyDown, NSCommandKeyMask);
  [window postEvent:event
            atStart:NO];

  // This will time out if the event above does not terminate Chrome.
  content::RunMessageLoop();

  EXPECT_FALSE(handler_->FindHost(profile(), extension_id_));
  EXPECT_TRUE(browser_shutdown::IsTryingToQuit());
}

}  // namespace apps
