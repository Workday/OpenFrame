// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "base/strings/string_util.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_sorting.h"
#include "chrome/browser/infobars/confirm_infobar_delegate.h"
#include "chrome/browser/infobars/infobar_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/webui/ntp/new_tab_ui.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/base/test_switches.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test_utils.h"
#include "extensions/common/id_util.h"

using content::WebContents;
using extensions::Extension;

class ExtensionInstallUIBrowserTest : public ExtensionBrowserTest {
 public:
  // Checks that a theme info bar is currently visible and issues an undo to
  // revert to the previous theme.
  void VerifyThemeInfoBarAndUndoInstall() {
    WebContents* web_contents =
        browser()->tab_strip_model()->GetActiveWebContents();
    ASSERT_TRUE(web_contents);
    InfoBarService* infobar_service =
        InfoBarService::FromWebContents(web_contents);
    ASSERT_EQ(1U, infobar_service->infobar_count());
    ConfirmInfoBarDelegate* delegate =
        infobar_service->infobar_at(0)->AsConfirmInfoBarDelegate();
    ASSERT_TRUE(delegate);
    delegate->Cancel();
    ASSERT_EQ(0U, infobar_service->infobar_count());
  }

  // Install the given theme from the data dir and verify expected name.
  void InstallThemeAndVerify(const char* theme_name,
                             const std::string& expected_name) {
    const base::FilePath theme_path = test_data_dir_.AppendASCII(theme_name);
    ASSERT_TRUE(InstallExtensionWithUIAutoConfirm(theme_path, 1, browser()));
    const Extension* theme = GetTheme();
    ASSERT_TRUE(theme);
    ASSERT_EQ(theme->name(), expected_name);
  }

  const Extension* GetTheme() const {
    return ThemeServiceFactory::GetThemeForProfile(browser()->profile());
  }
};

#if defined(OS_LINUX)
// Fails consistently on bot chromium.chromiumos \ Linux.
// See: http://crbug.com/120647.
#define MAYBE_TestThemeInstallUndoResetsToDefault DISABLED_TestThemeInstallUndoResetsToDefault
#else
#define MAYBE_TestThemeInstallUndoResetsToDefault TestThemeInstallUndoResetsToDefault
#endif

IN_PROC_BROWSER_TEST_F(ExtensionInstallUIBrowserTest,
                       MAYBE_TestThemeInstallUndoResetsToDefault) {
#if defined(OS_WIN) && defined(USE_ASH)
  // Disable this test in Metro+Ash for now (http://crbug.com/262796).
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kAshBrowserTests))
    return;
#endif

  // Install theme once and undo to verify we go back to default theme.
  base::FilePath theme_crx = PackExtension(test_data_dir_.AppendASCII("theme"));
  ASSERT_TRUE(InstallExtensionWithUIAutoConfirm(theme_crx, 1, browser()));
  const Extension* theme = GetTheme();
  ASSERT_TRUE(theme);
  std::string theme_id = theme->id();
  VerifyThemeInfoBarAndUndoInstall();
  ASSERT_EQ(NULL, GetTheme());

  // Set the same theme twice and undo to verify we go back to default theme.
  ASSERT_TRUE(InstallExtensionWithUIAutoConfirm(theme_crx, 1, browser()));
  theme = GetTheme();
  ASSERT_TRUE(theme);
  ASSERT_EQ(theme_id, theme->id());
  ASSERT_TRUE(InstallExtensionWithUIAutoConfirm(theme_crx, 0, browser()));
  theme = GetTheme();
  ASSERT_TRUE(theme);
  ASSERT_EQ(theme_id, theme->id());
  VerifyThemeInfoBarAndUndoInstall();
  ASSERT_EQ(NULL, GetTheme());
}

IN_PROC_BROWSER_TEST_F(ExtensionInstallUIBrowserTest,
                       TestThemeInstallUndoResetsToPreviousTheme) {
#if defined(OS_WIN) && defined(USE_ASH)
  // Disable this test in Metro+Ash for now (http://crbug.com/262796).
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kAshBrowserTests))
    return;
#endif

  // Install first theme.
  InstallThemeAndVerify("theme", "camo theme");
  const Extension* theme = GetTheme();
  std::string theme_id = theme->id();

  // Then install second theme.
  InstallThemeAndVerify("theme2", "snowflake theme");
  const Extension* theme2 = GetTheme();
  EXPECT_FALSE(theme_id == theme2->id());

  // Undo second theme will revert to first theme.
  VerifyThemeInfoBarAndUndoInstall();
  EXPECT_EQ(theme, GetTheme());
}

IN_PROC_BROWSER_TEST_F(ExtensionInstallUIBrowserTest,
                       TestThemeReset) {
  InstallThemeAndVerify("theme", "camo theme");

  // Reset to default theme.
  ThemeServiceFactory::GetForProfile(browser()->profile())->UseDefaultTheme();
  ASSERT_FALSE(GetTheme());
}

IN_PROC_BROWSER_TEST_F(ExtensionInstallUIBrowserTest,
                       TestInstallThemeInFullScreen) {
  EXPECT_TRUE(chrome::ExecuteCommand(browser(), IDC_FULLSCREEN));
  InstallThemeAndVerify("theme", "camo theme");
}

IN_PROC_BROWSER_TEST_F(ExtensionInstallUIBrowserTest,
                       AppInstallConfirmation) {
  int num_tabs = browser()->tab_strip_model()->count();

  base::FilePath app_dir = test_data_dir_.AppendASCII("app");
  ASSERT_TRUE(InstallExtensionWithUIAutoConfirm(app_dir, 1, browser()));

  if (NewTabUI::ShouldShowApps()) {
    EXPECT_EQ(num_tabs + 1, browser()->tab_strip_model()->count());
    WebContents* web_contents =
        browser()->tab_strip_model()->GetActiveWebContents();
    ASSERT_TRUE(web_contents);
    EXPECT_TRUE(StartsWithASCII(web_contents->GetURL().spec(),
                                "chrome://newtab/", false));
  } else {
    // TODO(xiyuan): Figure out how to test extension installed bubble?
  }
}

IN_PROC_BROWSER_TEST_F(ExtensionInstallUIBrowserTest,
                       AppInstallConfirmation_Incognito) {
  Browser* incognito_browser = CreateIncognitoBrowser();

  int num_incognito_tabs = incognito_browser->tab_strip_model()->count();
  int num_normal_tabs = browser()->tab_strip_model()->count();

  base::FilePath app_dir = test_data_dir_.AppendASCII("app");
  ASSERT_TRUE(InstallExtensionWithUIAutoConfirm(app_dir, 1,
                                                incognito_browser));

  EXPECT_EQ(num_incognito_tabs,
            incognito_browser->tab_strip_model()->count());
  if (NewTabUI::ShouldShowApps()) {
    EXPECT_EQ(num_normal_tabs + 1, browser()->tab_strip_model()->count());
    WebContents* web_contents =
        browser()->tab_strip_model()->GetActiveWebContents();
    ASSERT_TRUE(web_contents);
    EXPECT_TRUE(StartsWithASCII(web_contents->GetURL().spec(),
                                "chrome://newtab/", false));
  } else {
    // TODO(xiyuan): Figure out how to test extension installed bubble?
  }
}

class NewTabUISortingBrowserTest : public ExtensionInstallUIBrowserTest {
 public:
  NewTabUISortingBrowserTest() {}

  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE {
    if (type != chrome::NOTIFICATION_EXTENSION_LAUNCHER_REORDERED) {
      ExtensionInstallUIBrowserTest::Observe(type, source, details);
      return;
    }
    const std::string* id = content::Details<const std::string>(details).ptr();
    EXPECT_TRUE(id);
    last_reordered_extension_id_ = *id;
  }

 protected:
  std::string last_reordered_extension_id_;
  content::NotificationRegistrar registrar_;

 private:
  DISALLOW_COPY_AND_ASSIGN(NewTabUISortingBrowserTest);
};

IN_PROC_BROWSER_TEST_F(NewTabUISortingBrowserTest, ReorderDuringInstall) {
  ui_test_utils::NavigateToURL(browser(), GURL(chrome::kChromeUINewTabURL));
  ExtensionService* service = extensions::ExtensionSystem::Get(
      browser()->profile())->extension_service();
  base::FilePath app_dir = test_data_dir_.AppendASCII("app");
  const std::string app_id = extensions::id_util::GenerateIdForPath(app_dir);

  const extensions::Extension* webstore_extension =
      service->GetInstalledExtension(extension_misc::kWebStoreAppId);
  EXPECT_TRUE(webstore_extension);
  ExtensionSorting* sorting = service->extension_prefs()->extension_sorting();

  // Register for notifications in the same way as AppLauncherHandler.
  registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_LAUNCHER_REORDERED,
      content::Source<ExtensionSorting>(sorting));
  // ExtensionAppItem calls this when an app install starts.
  sorting->EnsureValidOrdinals(app_id, syncer::StringOrdinal());
  // Vefify the app is not actually installed yet.
  EXPECT_FALSE(service->GetInstalledExtension(app_id));
  // Move the test app from the end to be before the web store.
  service->OnExtensionMoved(app_id,
                            std::string(),
                            extension_misc::kWebStoreAppId);
  EXPECT_EQ(app_id, last_reordered_extension_id_);

  // Now install the app.
  const extensions::Extension* test_app = LoadExtension(app_dir);
  ASSERT_TRUE(test_app);
  EXPECT_TRUE(service->GetInstalledExtension(app_id));
  EXPECT_EQ(app_id, test_app->id());
}
