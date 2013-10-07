// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/command_line.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_icon_set.h"
#include "chrome/common/extensions/manifest_handlers/app_launch_info.h"
#include "chrome/common/extensions/manifest_handlers/icons_handler.h"
#include "chrome/common/extensions/permissions/permission_set.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/test_switches.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test_utils.h"

namespace extensions {

class ExtensionFromWebAppTest
    : public InProcessBrowserTest, public content::NotificationObserver {
 protected:
  ExtensionFromWebAppTest() : installed_extension_(NULL) {
  }

  std::string expected_extension_id_;
  const Extension* installed_extension_;

 private:
  // content::NotificationObserver
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE {
    if (type == chrome::NOTIFICATION_EXTENSION_INSTALLED) {
      const Extension* extension =
          content::Details<const InstalledExtensionInfo>(details)->extension;
      if (extension->id() == expected_extension_id_) {
        installed_extension_ = extension;
        base::MessageLoopForUI::current()->Quit();
      }
    }
  }
};

#if defined(OS_CHROMEOS)
// The Apps tab is not included in the new tab page in Chrome OS.
#define MAYBE_Basic DISABLED_Basic
#else
#define MAYBE_Basic Basic
#endif

IN_PROC_BROWSER_TEST_F(ExtensionFromWebAppTest, MAYBE_Basic) {
#if defined(OS_WIN) && defined(USE_ASH)
  // Disable this test in Metro+Ash for now (http://crbug.com/262796).
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kAshBrowserTests))
    return;
#endif

  browser()->profile()->GetExtensionService()->set_show_extensions_prompts(
      false);

  content::NotificationRegistrar registrar;
  registrar.Add(this, chrome::NOTIFICATION_EXTENSION_INSTALLED,
                content::NotificationService::AllSources());

  expected_extension_id_ = "ffnmbohohhobhkjpfbefbjifapgcmpaa";
  ui_test_utils::NavigateToURL(
      browser(),
      GURL("chrome://newtab"));
  EXPECT_TRUE(content::ExecuteScript(
      browser()->tab_strip_model()->GetActiveWebContents(),
      "chrome.send('generateAppForLink', "
      "['http://www.example.com', 'Test application', 0])"));

  if (!installed_extension_)
    content::RunMessageLoop();

  EXPECT_TRUE(installed_extension_);
  EXPECT_TRUE(installed_extension_->is_hosted_app());
  EXPECT_EQ("Test application", installed_extension_->name());
  EXPECT_EQ("", installed_extension_->description());
  EXPECT_EQ(GURL("http://www.example.com/"),
            AppLaunchInfo::GetLaunchWebURL(installed_extension_));
  EXPECT_EQ(extension_misc::LAUNCH_TAB,
            AppLaunchInfo::GetLaunchContainer(installed_extension_));
  EXPECT_EQ(0u, installed_extension_->GetActivePermissions()->apis().size());
  EXPECT_EQ(0u, IconsInfo::GetIcons(installed_extension_).map().size());
}

}  // namespace extensions
