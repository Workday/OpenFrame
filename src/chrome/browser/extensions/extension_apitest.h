// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_APITEST_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_APITEST_H_

#include <deque>
#include <string>

#include "base/compiler_specific.h"
#include "base/values.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "content/public/browser/notification_registrar.h"

namespace base {
class FilePath;
}

namespace extensions {
class Extension;
}

// The general flow of these API tests should work like this:
// (1) Setup initial browser state (e.g. create some bookmarks for the
//     bookmark test)
// (2) Call ASSERT_TRUE(RunExtensionTest(name));
// (3) In your extension code, run your test and call chrome.test.pass or
//     chrome.test.fail
// (4) Verify expected browser state.
// TODO(erikkay): There should also be a way to drive events in these tests.
class ExtensionApiTest : public ExtensionBrowserTest {
 public:
  // Flags used to configure how the tests are run.
  // TODO(aa): Many of these are dupes of ExtensionBrowserTest::Flags. Combine
  // somehow?
  enum Flags {
    kFlagNone = 0,

    // Allow the extension to run in incognito mode.
    kFlagEnableIncognito = 1 << 0,

    // Launch the test page in an incognito window.
    kFlagUseIncognito = 1 << 1,

    // Allow file access for the extension.
    kFlagEnableFileAccess = 1 << 2,

    // Loads the extension with location COMPONENT.
    kFlagLoadAsComponent = 1 << 3,

    // Launch the extension as a platform app.
    kFlagLaunchPlatformApp = 1 << 4,

    // Don't fail when the loaded manifest has warnings.
    kFlagIgnoreManifestWarnings = 1 << 5,

    // Allow manifest versions older that Extension::kModernManifestVersion.
    // Used to test old manifest features.
    kFlagAllowOldManifestVersions = 1 << 6,
  };

  ExtensionApiTest();
  virtual ~ExtensionApiTest();

 protected:
  // Helper class that observes tests failing or passing. Observation starts
  // when the class is constructed. Get the next result by calling
  // GetNextResult() and message() if GetNextResult() return false. If there
  // are no results, this method will pump the UI message loop until one is
  // received.
  class ResultCatcher : public content::NotificationObserver {
   public:
    ResultCatcher();
    virtual ~ResultCatcher();

    // Pumps the UI loop until a notification is received that an API test
    // succeeded or failed. Returns true if the test succeeded, false otherwise.
    bool GetNextResult();

    void RestrictToProfile(Profile* profile) { profile_restriction_ = profile; }

    const std::string& message() { return message_; }

   private:
    virtual void Observe(int type,
                         const content::NotificationSource& source,
                         const content::NotificationDetails& details) OVERRIDE;

    content::NotificationRegistrar registrar_;

    // A sequential list of pass/fail notifications from the test extension(s).
    std::deque<bool> results_;

    // If it failed, what was the error message?
    std::deque<std::string> messages_;
    std::string message_;

    // If non-NULL, we will listen to events from this profile only.
    Profile* profile_restriction_;

    // True if we're in a nested message loop waiting for results from
    // the extension.
    bool waiting_;
  };

  virtual void SetUpInProcessBrowserTestFixture() OVERRIDE;
  virtual void TearDownInProcessBrowserTestFixture() OVERRIDE;

  // Load |extension_name| and wait for pass / fail notification.
  // |extension_name| is a directory in "test/data/extensions/api_test".
  bool RunExtensionTest(const char* extension_name);

  // Same as RunExtensionTest, but enables the extension for incognito mode.
  bool RunExtensionTestIncognito(const char* extension_name);

  // Same as RunExtensionTest, but ignores any warnings in the manifest.
  bool RunExtensionTestIgnoreManifestWarnings(const char* extension_name);

  // Same as RunExtensionTest, allow old manifest ersions.
  bool RunExtensionTestAllowOldManifestVersion(const char* extension_name);

  // Same as RunExtensionTest, but loads extension as component.
  bool RunComponentExtensionTest(const char* extension_name);

  // Same as RunExtensionTest, but disables file access.
  bool RunExtensionTestNoFileAccess(const char* extension_name);

  // Same as RunExtensionTestIncognito, but disables file access.
  bool RunExtensionTestIncognitoNoFileAccess(const char* extension_name);

  // If not empty, Load |extension_name|, load |page_url| and wait for pass /
  // fail notification from the extension API on the page. Note that if
  // |page_url| is not a valid url, it will be treated as a resource within
  // the extension. |extension_name| is a directory in
  // "test/data/extensions/api_test".
  bool RunExtensionSubtest(const char* extension_name,
                           const std::string& page_url);

  // Same as RunExtensionSubtest, except run with the specific |flags|
  // (as defined in the Flags enum).
  bool RunExtensionSubtest(const char* extension_name,
                           const std::string& page_url,
                           int flags);

  // Load |page_url| and wait for pass / fail notification from the extension
  // API on the page.
  bool RunPageTest(const std::string& page_url);
  bool RunPageTest(const std::string& page_url, int flags);

  // Similar to RunExtensionTest, except used for running tests in platform app
  // shell windows.
  bool RunPlatformAppTest(const char* extension_name);
  // Similar to RunPlatformAppTest, except sets an additional string argument
  // |customArg| to the test config object.
  bool RunPlatformAppTestWithArg(
      const char* extension_name, const char* custom_arg);

  // Start the test server, and store details of its state.  Those details
  // will be available to javascript tests using chrome.test.getConfig().
  bool StartEmbeddedTestServer();

  // Start the test WebSocket server, and store details of its state. Those
  // details will be available to javascript tests using
  // chrome.test.getConfig().
  bool StartWebSocketServer(const base::FilePath& root_directory);

  // Test that exactly one extension loaded.  If so, return a pointer to
  // the extension.  If not, return NULL and set message_.
  const extensions::Extension* GetSingleLoadedExtension();

  // All extensions tested by ExtensionApiTest are in the "api_test" dir.
  virtual void SetUpCommandLine(CommandLine* command_line) OVERRIDE;

  // If it failed, what was the error message?
  std::string message_;

 private:
  bool RunExtensionTestImpl(const char* extension_name,
                            const std::string& test_page,
                            const char* custom_arg,
                            int flags);

  // Hold details of the test, set in C++, which can be accessed by
  // javascript using chrome.test.getConfig().
  scoped_ptr<DictionaryValue> test_config_;

  // Hold the test WebSocket server.
  scoped_ptr<net::SpawnedTestServer> websocket_server_;
};

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_APITEST_H_
