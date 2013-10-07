// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/background/background_contents_service.h"
#include "chrome/browser/background/background_contents_service_factory.h"
#include "chrome/browser/background/background_mode_manager.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension.h"
#include "content/public/browser/notification_service.h"
#include "content/public/test/test_notification_tracker.h"
#include "content/public/test/test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"

#if defined(OS_MACOSX)
#include "base/mac/scoped_nsautorelease_pool.h"
#endif

using extensions::Extension;

class AppBackgroundPageApiTest : public ExtensionApiTest {
 public:
  virtual void SetUpCommandLine(CommandLine* command_line) OVERRIDE {
    ExtensionApiTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch(switches::kDisablePopupBlocking);
    command_line->AppendSwitch(switches::kAllowHTTPBackgroundPage);
  }

  bool CreateApp(const std::string& app_manifest,
                 base::FilePath* app_dir) {
    if (!app_dir_.CreateUniqueTempDir()) {
      LOG(ERROR) << "Unable to create a temporary directory.";
      return false;
    }
    base::FilePath manifest_path = app_dir_.path().AppendASCII("manifest.json");
    int bytes_written = file_util::WriteFile(manifest_path,
                                             app_manifest.data(),
                                             app_manifest.size());
    if (bytes_written != static_cast<int>(app_manifest.size())) {
      LOG(ERROR) << "Unable to write complete manifest to file. Return code="
                 << bytes_written;
      return false;
    }
    *app_dir = app_dir_.path();
    return true;
  }

  bool WaitForBackgroundMode(bool expected_background_mode) {
#if defined(OS_CHROMEOS)
    // BackgroundMode is not supported on chromeos, so we should test the
    // behavior of BackgroundContents, but not the background mode state itself.
    return true;
#else
    BackgroundModeManager* manager =
        g_browser_process->background_mode_manager();
    // If background mode is disabled on this platform (e.g. cros), then skip
    // this check.
    if (!manager || !manager->IsBackgroundModePrefEnabled()) {
      DLOG(WARNING) << "Skipping check - background mode disabled";
      return true;
    }
    if (manager->IsBackgroundModeActiveForTest() == expected_background_mode)
      return true;

    // We are not currently in the expected state - wait for the state to
    // change.
    content::WindowedNotificationObserver watcher(
        chrome::NOTIFICATION_BACKGROUND_MODE_CHANGED,
        content::NotificationService::AllSources());
    watcher.Wait();
    return manager->IsBackgroundModeActiveForTest() == expected_background_mode;
#endif
  }

  void CloseBrowser(Browser* browser) {
    content::WindowedNotificationObserver observer(
        chrome::NOTIFICATION_BROWSER_CLOSED,
        content::NotificationService::AllSources());
    browser->window()->Close();
#if defined(OS_MACOSX)
    // BrowserWindowController depends on the auto release pool being recycled
    // in the message loop to delete itself, which frees the Browser object
    // which fires this event.
    AutoreleasePool()->Recycle();
#endif
    observer.Wait();
  }

  void UnloadExtensionViaTask(const std::string& id) {
    base::MessageLoop::current()->PostTask(
        FROM_HERE,
        base::Bind(&AppBackgroundPageApiTest::UnloadExtension, this, id));
  }

 private:
  base::ScopedTempDir app_dir_;
};

// Disable on Mac only.  http://crbug.com/95139
#if defined(OS_MACOSX)
#define MAYBE_Basic DISABLED_Basic
#else
#define MAYBE_Basic Basic
#endif

IN_PROC_BROWSER_TEST_F(AppBackgroundPageApiTest, MAYBE_Basic) {
  host_resolver()->AddRule("a.com", "127.0.0.1");
  ASSERT_TRUE(StartEmbeddedTestServer());

  std::string app_manifest = base::StringPrintf(
      "{"
      "  \"name\": \"App\","
      "  \"version\": \"0.1\","
      "  \"manifest_version\": 2,"
      "  \"app\": {"
      "    \"urls\": ["
      "      \"http://a.com/\""
      "    ],"
      "    \"launch\": {"
      "      \"web_url\": \"http://a.com:%d/\""
      "    }"
      "  },"
      "  \"permissions\": [\"background\"]"
      "}",
      embedded_test_server()->port());

  base::FilePath app_dir;
  ASSERT_TRUE(CreateApp(app_manifest, &app_dir));
  ASSERT_TRUE(LoadExtension(app_dir));
  // Background mode should not be active until a background page is created.
  ASSERT_TRUE(WaitForBackgroundMode(false));
  ASSERT_TRUE(RunExtensionTest("app_background_page/basic")) << message_;
  // The test closes the background contents, so we should fall back to no
  // background mode at the end.
  ASSERT_TRUE(WaitForBackgroundMode(false));
}

// Crashy, http://crbug.com/69215.
IN_PROC_BROWSER_TEST_F(AppBackgroundPageApiTest, DISABLED_LacksPermission) {
  host_resolver()->AddRule("a.com", "127.0.0.1");
  ASSERT_TRUE(StartEmbeddedTestServer());

  std::string app_manifest = base::StringPrintf(
      "{"
      "  \"name\": \"App\","
      "  \"version\": \"0.1\","
      "  \"manifest_version\": 2,"
      "  \"app\": {"
      "    \"urls\": ["
      "      \"http://a.com/\""
      "    ],"
      "    \"launch\": {"
      "      \"web_url\": \"http://a.com:%d/\""
      "    }"
      "  }"
      "}",
      embedded_test_server()->port());

  base::FilePath app_dir;
  ASSERT_TRUE(CreateApp(app_manifest, &app_dir));
  ASSERT_TRUE(LoadExtension(app_dir));
  ASSERT_TRUE(RunExtensionTest("app_background_page/lacks_permission"))
      << message_;
  ASSERT_TRUE(WaitForBackgroundMode(false));
}

IN_PROC_BROWSER_TEST_F(AppBackgroundPageApiTest, ManifestBackgroundPage) {
  host_resolver()->AddRule("a.com", "127.0.0.1");
  ASSERT_TRUE(StartEmbeddedTestServer());

  std::string app_manifest = base::StringPrintf(
      "{"
      "  \"name\": \"App\","
      "  \"version\": \"0.1\","
      "  \"manifest_version\": 2,"
      "  \"app\": {"
      "    \"urls\": ["
      "      \"http://a.com/\""
      "    ],"
      "    \"launch\": {"
      "      \"web_url\": \"http://a.com:%d/\""
      "    }"
      "  },"
      "  \"permissions\": [\"background\"],"
      "  \"background\": {"
      "    \"page\": \"http://a.com:%d/test.html\""
      "  }"
      "}",
      embedded_test_server()->port(),
      embedded_test_server()->port());

  base::FilePath app_dir;
  ASSERT_TRUE(CreateApp(app_manifest, &app_dir));
  // Background mode should not be active now because no background app was
  // loaded.
  ASSERT_TRUE(LoadExtension(app_dir));
  // Background mode be active now because a background page was created when
  // the app was loaded.
  ASSERT_TRUE(WaitForBackgroundMode(true));

  const Extension* extension = GetSingleLoadedExtension();
  ASSERT_TRUE(
      BackgroundContentsServiceFactory::GetForProfile(browser()->profile())->
          GetAppBackgroundContents(ASCIIToUTF16(extension->id())));
  UnloadExtension(extension->id());
}

IN_PROC_BROWSER_TEST_F(AppBackgroundPageApiTest, NoJsBackgroundPage) {
  // Make sure that no BackgroundContentses get deleted (a signal that repeated
  // window.open calls recreate instances, instead of being no-ops).
  content::TestNotificationTracker background_deleted_tracker;
  background_deleted_tracker.ListenFor(
      chrome::NOTIFICATION_BACKGROUND_CONTENTS_DELETED,
      content::Source<Profile>(browser()->profile()));

  host_resolver()->AddRule("a.com", "127.0.0.1");
  ASSERT_TRUE(StartEmbeddedTestServer());

  std::string app_manifest = base::StringPrintf(
      "{"
      "  \"name\": \"App\","
      "  \"version\": \"0.1\","
      "  \"manifest_version\": 2,"
      "  \"app\": {"
      "    \"urls\": ["
      "      \"http://a.com/\""
      "    ],"
      "    \"launch\": {"
      "      \"web_url\": \"http://a.com:%d/test.html\""
      "    }"
      "  },"
      "  \"permissions\": [\"background\"],"
      "  \"background\": {"
      "    \"allow_js_access\": false"
      "  }"
      "}",
      embedded_test_server()->port());

  base::FilePath app_dir;
  ASSERT_TRUE(CreateApp(app_manifest, &app_dir));
  ASSERT_TRUE(LoadExtension(app_dir));

  // There isn't a background page loaded initially.
  const Extension* extension = GetSingleLoadedExtension();
  ASSERT_FALSE(
      BackgroundContentsServiceFactory::GetForProfile(browser()->profile())->
          GetAppBackgroundContents(ASCIIToUTF16(extension->id())));
  // The test makes sure that window.open returns null.
  ASSERT_TRUE(RunExtensionTest("app_background_page/no_js")) << message_;
  // And after it runs there should be a background page.
  ASSERT_TRUE(
      BackgroundContentsServiceFactory::GetForProfile(browser()->profile())->
          GetAppBackgroundContents(ASCIIToUTF16(extension->id())));

  EXPECT_EQ(0u, background_deleted_tracker.size());
  UnloadExtension(extension->id());
}

IN_PROC_BROWSER_TEST_F(AppBackgroundPageApiTest, NoJsManifestBackgroundPage) {
  host_resolver()->AddRule("a.com", "127.0.0.1");
  ASSERT_TRUE(StartEmbeddedTestServer());

  std::string app_manifest = base::StringPrintf(
      "{"
      "  \"name\": \"App\","
      "  \"version\": \"0.1\","
      "  \"manifest_version\": 2,"
      "  \"app\": {"
      "    \"urls\": ["
      "      \"http://a.com/\""
      "    ],"
      "    \"launch\": {"
      "      \"web_url\": \"http://a.com:%d/\""
      "    }"
      "  },"
      "  \"permissions\": [\"background\"],"
      "  \"background\": {"
      "    \"page\": \"http://a.com:%d/bg.html\","
      "    \"allow_js_access\": false"
      "  }"
      "}",
      embedded_test_server()->port(),
      embedded_test_server()->port());

  base::FilePath app_dir;
  ASSERT_TRUE(CreateApp(app_manifest, &app_dir));
  ASSERT_TRUE(LoadExtension(app_dir));

  // The background page should load, but window.open should return null.
  const Extension* extension = GetSingleLoadedExtension();
  ASSERT_TRUE(
      BackgroundContentsServiceFactory::GetForProfile(browser()->profile())->
          GetAppBackgroundContents(ASCIIToUTF16(extension->id())));
  ASSERT_TRUE(RunExtensionTest("app_background_page/no_js_manifest")) <<
      message_;
  UnloadExtension(extension->id());
}

IN_PROC_BROWSER_TEST_F(AppBackgroundPageApiTest, OpenTwoBackgroundPages) {
  host_resolver()->AddRule("a.com", "127.0.0.1");
  ASSERT_TRUE(StartEmbeddedTestServer());

  std::string app_manifest = base::StringPrintf(
      "{"
      "  \"name\": \"App\","
      "  \"version\": \"0.1\","
      "  \"manifest_version\": 2,"
      "  \"app\": {"
      "    \"urls\": ["
      "      \"http://a.com/\""
      "    ],"
      "    \"launch\": {"
      "      \"web_url\": \"http://a.com:%d/\""
      "    }"
      "  },"
      "  \"permissions\": [\"background\"]"
      "}",
      embedded_test_server()->port());

  base::FilePath app_dir;
  ASSERT_TRUE(CreateApp(app_manifest, &app_dir));
  ASSERT_TRUE(LoadExtension(app_dir));
  const Extension* extension = GetSingleLoadedExtension();
  ASSERT_TRUE(RunExtensionTest("app_background_page/two_pages")) << message_;
  UnloadExtension(extension->id());
}

IN_PROC_BROWSER_TEST_F(AppBackgroundPageApiTest, OpenTwoPagesWithManifest) {
  host_resolver()->AddRule("a.com", "127.0.0.1");
  ASSERT_TRUE(StartEmbeddedTestServer());

  std::string app_manifest = base::StringPrintf(
      "{"
      "  \"name\": \"App\","
      "  \"version\": \"0.1\","
      "  \"manifest_version\": 2,"
      "  \"app\": {"
      "    \"urls\": ["
      "      \"http://a.com/\""
      "    ],"
      "    \"launch\": {"
      "      \"web_url\": \"http://a.com:%d/\""
      "    }"
      "  },"
      "  \"background\": {"
      "    \"page\": \"http://a.com:%d/bg.html\""
      "  },"
      "  \"permissions\": [\"background\"]"
      "}",
      embedded_test_server()->port(),
      embedded_test_server()->port());

  base::FilePath app_dir;
  ASSERT_TRUE(CreateApp(app_manifest, &app_dir));
  ASSERT_TRUE(LoadExtension(app_dir));
  const Extension* extension = GetSingleLoadedExtension();
  ASSERT_TRUE(RunExtensionTest("app_background_page/two_with_manifest")) <<
      message_;
  UnloadExtension(extension->id());
}

// Times out occasionally -- see crbug.com/108493
IN_PROC_BROWSER_TEST_F(AppBackgroundPageApiTest, DISABLED_OpenPopupFromBGPage) {
  host_resolver()->AddRule("a.com", "127.0.0.1");
  ASSERT_TRUE(StartEmbeddedTestServer());

  std::string app_manifest = base::StringPrintf(
      "{"
      "  \"name\": \"App\","
      "  \"version\": \"0.1\","
      "  \"manifest_version\": 2,"
      "  \"app\": {"
      "    \"urls\": ["
      "      \"http://a.com/\""
      "    ],"
      "    \"launch\": {"
      "      \"web_url\": \"http://a.com:%d/\""
      "    }"
      "  },"
      "  \"background\": { \"page\": \"http://a.com:%d/extensions/api_test/"
      "app_background_page/bg_open/bg_open_bg.html\" },"
      "  \"permissions\": [\"background\"]"
      "}",
      embedded_test_server()->port(),
      embedded_test_server()->port());

  base::FilePath app_dir;
  ASSERT_TRUE(CreateApp(app_manifest, &app_dir));
  ASSERT_TRUE(LoadExtension(app_dir));
  ASSERT_TRUE(RunExtensionTest("app_background_page/bg_open")) << message_;
}

IN_PROC_BROWSER_TEST_F(AppBackgroundPageApiTest, DISABLED_OpenThenClose) {
  host_resolver()->AddRule("a.com", "127.0.0.1");
  ASSERT_TRUE(StartEmbeddedTestServer());

  std::string app_manifest = base::StringPrintf(
      "{"
      "  \"name\": \"App\","
      "  \"version\": \"0.1\","
      "  \"manifest_version\": 2,"
      "  \"app\": {"
      "    \"urls\": ["
      "      \"http://a.com/\""
      "    ],"
      "    \"launch\": {"
      "      \"web_url\": \"http://a.com:%d/\""
      "    }"
      "  },"
      "  \"permissions\": [\"background\"]"
      "}",
      embedded_test_server()->port());

  base::FilePath app_dir;
  ASSERT_TRUE(CreateApp(app_manifest, &app_dir));
  ASSERT_TRUE(LoadExtension(app_dir));
  // There isn't a background page loaded initially.
  const Extension* extension = GetSingleLoadedExtension();
  ASSERT_FALSE(
      BackgroundContentsServiceFactory::GetForProfile(browser()->profile())->
          GetAppBackgroundContents(ASCIIToUTF16(extension->id())));
  // Background mode should not be active until a background page is created.
  ASSERT_TRUE(WaitForBackgroundMode(false));
  ASSERT_TRUE(RunExtensionTest("app_background_page/basic_open")) << message_;
  // Background mode should be active now because a background page was created.
  ASSERT_TRUE(WaitForBackgroundMode(true));
  ASSERT_TRUE(
      BackgroundContentsServiceFactory::GetForProfile(browser()->profile())->
          GetAppBackgroundContents(ASCIIToUTF16(extension->id())));
  // Now close the BackgroundContents.
  ASSERT_TRUE(RunExtensionTest("app_background_page/basic_close")) << message_;
  // Background mode should no longer be active.
  ASSERT_TRUE(WaitForBackgroundMode(false));
  ASSERT_FALSE(
      BackgroundContentsServiceFactory::GetForProfile(browser()->profile())->
          GetAppBackgroundContents(ASCIIToUTF16(extension->id())));
}

IN_PROC_BROWSER_TEST_F(AppBackgroundPageApiTest, UnloadExtensionWhileHidden) {
  host_resolver()->AddRule("a.com", "127.0.0.1");
  ASSERT_TRUE(StartEmbeddedTestServer());

  std::string app_manifest = base::StringPrintf(
      "{"
      "  \"name\": \"App\","
      "  \"version\": \"0.1\","
      "  \"manifest_version\": 2,"
      "  \"app\": {"
      "    \"urls\": ["
      "      \"http://a.com/\""
      "    ],"
      "    \"launch\": {"
      "      \"web_url\": \"http://a.com:%d/\""
      "    }"
      "  },"
      "  \"permissions\": [\"background\"],"
      "  \"background\": {"
      "    \"page\": \"http://a.com:%d/test.html\""
      "  }"
      "}",
      embedded_test_server()->port(),
      embedded_test_server()->port());

  base::FilePath app_dir;
  ASSERT_TRUE(CreateApp(app_manifest, &app_dir));
  // Background mode should not be active now because no background app was
  // loaded.
  ASSERT_TRUE(LoadExtension(app_dir));
  // Background mode be active now because a background page was created when
  // the app was loaded.
  ASSERT_TRUE(WaitForBackgroundMode(true));

  const Extension* extension = GetSingleLoadedExtension();
  ASSERT_TRUE(
      BackgroundContentsServiceFactory::GetForProfile(browser()->profile())->
          GetAppBackgroundContents(ASCIIToUTF16(extension->id())));

  // Close all browsers - app should continue running.
  set_exit_when_last_browser_closes(false);
  CloseBrowser(browser());

  // Post a task to unload the extension - this should cause Chrome to exit
  // cleanly (not crash).
  UnloadExtensionViaTask(extension->id());
  content::RunAllPendingInMessageLoop();
  ASSERT_TRUE(WaitForBackgroundMode(false));
}
