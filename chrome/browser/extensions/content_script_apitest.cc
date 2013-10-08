// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/api/permissions/permissions_api.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "url/gurl.h"

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, ContentScriptAllFrames) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(RunExtensionTest("content_scripts/all_frames")) << message_;
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, ContentScriptAboutBlankIframes) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(
      RunExtensionTest("content_scripts/about_blank_iframes")) << message_;
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, ContentScriptExtensionIframe) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(RunExtensionTest("content_scripts/extension_iframe")) << message_;
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, ContentScriptExtensionProcess) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(
      RunExtensionTest("content_scripts/extension_process")) << message_;
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, ContentScriptFragmentNavigation) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  const char* extension_name = "content_scripts/fragment";
  ASSERT_TRUE(RunExtensionTest(extension_name)) << message_;
}

// Times out on Linux: http://crbug.com/163097
#if defined(OS_LINUX)
#define MAYBE_ContentScriptIsolatedWorlds DISABLED_ContentScriptIsolatedWorlds
#else
#define MAYBE_ContentScriptIsolatedWorlds ContentScriptIsolatedWorlds
#endif
IN_PROC_BROWSER_TEST_F(ExtensionApiTest, MAYBE_ContentScriptIsolatedWorlds) {
  // This extension runs various bits of script and tests that they all run in
  // the same isolated world.
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(RunExtensionTest("content_scripts/isolated_world1")) << message_;

  // Now load a different extension, inject into same page, verify worlds aren't
  // shared.
  ASSERT_TRUE(RunExtensionTest("content_scripts/isolated_world2")) << message_;
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, ContentScriptIgnoreHostPermissions) {
  host_resolver()->AddRule("a.com", "127.0.0.1");
  host_resolver()->AddRule("b.com", "127.0.0.1");
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(RunExtensionTest(
      "content_scripts/dont_match_host_permissions")) << message_;
}

// crbug.com/39249 -- content scripts js should not run on view source.
IN_PROC_BROWSER_TEST_F(ExtensionApiTest, ContentScriptViewSource) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(RunExtensionTest("content_scripts/view_source")) << message_;
}

// crbug.com/126257 -- content scripts should not get injected into other
// extensions.
IN_PROC_BROWSER_TEST_F(ExtensionApiTest, ContentScriptOtherExtensions) {
  host_resolver()->AddRule("a.com", "127.0.0.1");
  ASSERT_TRUE(StartEmbeddedTestServer());
  // First, load extension that sets up content script.
  ASSERT_TRUE(RunExtensionTest("content_scripts/other_extensions/injector"))
      << message_;
  // Then load targeted extension to make sure its content isn't changed.
  ASSERT_TRUE(RunExtensionTest("content_scripts/other_extensions/victim"))
      << message_;
}

// crbug.com/120762
IN_PROC_BROWSER_TEST_F(
    ExtensionApiTest,
    DISABLED_ContentScriptStylesInjectedIntoExistingRenderers) {
  ASSERT_TRUE(StartEmbeddedTestServer());

  content::WindowedNotificationObserver signal(
      chrome::NOTIFICATION_USER_SCRIPTS_UPDATED,
      content::Source<Profile>(browser()->profile()));

  // Start with a renderer already open at a URL.
  GURL url(test_server()->GetURL("file/extensions/test_file.html"));
  ui_test_utils::NavigateToURL(browser(), url);

  LoadExtension(
      test_data_dir_.AppendASCII("content_scripts/existing_renderers"));

  signal.Wait();

  // And check that its styles were affected by the styles that just got loaded.
  bool styles_injected;
  ASSERT_TRUE(content::ExecuteScriptAndExtractBool(
      browser()->tab_strip_model()->GetActiveWebContents(),
      "window.domAutomationController.send("
      "    document.defaultView.getComputedStyle(document.body, null)."
      "        getPropertyValue('background-color') == 'rgb(255, 0, 0)')",
      &styles_injected));
  ASSERT_TRUE(styles_injected);
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest,
                       ContentScriptCSSLocalization) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(RunExtensionTest("content_scripts/css_l10n")) << message_;
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, ContentScriptExtensionAPIs) {
  ASSERT_TRUE(StartEmbeddedTestServer());

  const extensions::Extension* extension = LoadExtension(
      test_data_dir_.AppendASCII("content_scripts/extension_api"));

  ResultCatcher catcher;
  ui_test_utils::NavigateToURL(
      browser(),
      embedded_test_server()->GetURL(
          "/extensions/api_test/content_scripts/extension_api/functions.html"));
  EXPECT_TRUE(catcher.GetNextResult());

  // Navigate to a page that will cause a content script to run that starts
  // listening for an extension event.
  ui_test_utils::NavigateToURL(
      browser(),
      embedded_test_server()->GetURL(
          "/extensions/api_test/content_scripts/extension_api/events.html"));

  // Navigate to an extension page that will fire the event events.js is
  // listening for.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), extension->GetResourceURL("fire_event.html"),
      NEW_FOREGROUND_TAB, ui_test_utils::BROWSER_TEST_NONE);
  EXPECT_TRUE(catcher.GetNextResult());
}

// Flaky on Windows. http://crbug.com/248418
#if defined(OS_WIN)
#define MAYBE_ContentScriptPermissionsApi DISABLED_ContentScriptPermissionsApi
#else
#define MAYBE_ContentScriptPermissionsApi ContentScriptPermissionsApi
#endif
IN_PROC_BROWSER_TEST_F(ExtensionApiTest, MAYBE_ContentScriptPermissionsApi) {
  extensions::PermissionsRequestFunction::SetIgnoreUserGestureForTests(true);
  extensions::PermissionsRequestFunction::SetAutoConfirmForTests(true);
  host_resolver()->AddRule("*.com", "127.0.0.1");
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(RunExtensionTest("content_scripts/permissions")) << message_;
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, ContentScriptBypassPageCSP) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(RunExtensionTest("content_scripts/bypass_page_csp")) << message_;
}
