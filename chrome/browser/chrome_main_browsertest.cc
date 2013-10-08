// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/ui/ui_test.h"

#include "base/command_line.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/test_switches.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/web_contents.h"
#include "net/base/net_util.h"

// These tests don't apply to the Mac version; see GetCommandLineForRelaunch
// for details.
#if !defined(OS_MACOSX)

class ChromeMainTest : public InProcessBrowserTest {
 public:
  ChromeMainTest() {}

  void Relaunch(const CommandLine& new_command_line) {
    base::LaunchProcess(new_command_line, base::LaunchOptions(), NULL);
  }
};

// Make sure that the second invocation creates a new window.
IN_PROC_BROWSER_TEST_F(ChromeMainTest, SecondLaunch) {
#if defined(OS_WIN) && defined(USE_ASH)
  // Disable this test in Metro+Ash for now (http://crbug.com/262796).
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kAshBrowserTests))
    return;
#endif

  ui_test_utils::BrowserAddedObserver observer;
  Relaunch(GetCommandLineForRelaunch());
  observer.WaitForSingleNewBrowser();
  ASSERT_EQ(2u, chrome::GetBrowserCount(browser()->profile(),
                                        browser()->host_desktop_type()));
}

IN_PROC_BROWSER_TEST_F(ChromeMainTest, ReuseBrowserInstanceWhenOpeningFile) {
#if defined(OS_WIN) && defined(USE_ASH)
  // Disable this test in Metro+Ash for now (http://crbug.com/262796).
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kAshBrowserTests))
    return;
#endif

  base::FilePath test_file_path = ui_test_utils::GetTestFilePath(
      base::FilePath(), base::FilePath().AppendASCII("empty.html"));
  CommandLine new_command_line(GetCommandLineForRelaunch());
  new_command_line.AppendArgPath(test_file_path);
  content::WindowedNotificationObserver observer(
        chrome::NOTIFICATION_TAB_ADDED,
        content::NotificationService::AllSources());
  Relaunch(new_command_line);
  observer.Wait();

  GURL url = net::FilePathToFileURL(test_file_path);
  content::WebContents* tab =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_EQ(url, tab->GetController().GetActiveEntry()->GetVirtualURL());
}

// ChromeMainTest.SecondLaunchWithIncognitoUrl is flaky on Win and Linux.
// http://crbug.com/130395
#if defined(OS_WIN) || defined(OS_LINUX)
#define MAYBE_SecondLaunchWithIncognitoUrl DISABLED_SecondLaunchWithIncognitoUrl
#else
#define MAYBE_SecondLaunchWithIncognitoUrl SecondLaunchWithIncognitoUrl
#endif

IN_PROC_BROWSER_TEST_F(ChromeMainTest, MAYBE_SecondLaunchWithIncognitoUrl) {
  // We should start with one normal window.
  ASSERT_EQ(1u, chrome::GetTabbedBrowserCount(browser()->profile(),
                                              browser()->host_desktop_type()));

  // Run with --incognito switch and an URL specified.
  base::FilePath test_file_path = ui_test_utils::GetTestFilePath(
      base::FilePath(), base::FilePath().AppendASCII("empty.html"));
  CommandLine new_command_line(GetCommandLineForRelaunch());
  new_command_line.AppendSwitch(switches::kIncognito);
  new_command_line.AppendArgPath(test_file_path);

  Relaunch(new_command_line);

  // There should be one normal and one incognito window now.
  ui_test_utils::BrowserAddedObserver observer;
  Relaunch(new_command_line);
  observer.WaitForSingleNewBrowser();
  ASSERT_EQ(2u, chrome::GetTotalBrowserCount());

  ASSERT_EQ(1u, chrome::GetTabbedBrowserCount(browser()->profile(),
                                              browser()->host_desktop_type()));
}

IN_PROC_BROWSER_TEST_F(ChromeMainTest, SecondLaunchFromIncognitoWithNormalUrl) {
#if defined(OS_WIN) && defined(USE_ASH)
  // Disable this test in Metro+Ash for now (http://crbug.com/262796).
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kAshBrowserTests))
    return;
#endif

  // We should start with one normal window.
  ASSERT_EQ(1u, chrome::GetTabbedBrowserCount(browser()->profile(),
                                              browser()->host_desktop_type()));

  // Create an incognito window.
  chrome::NewIncognitoWindow(browser());

  ASSERT_EQ(2u, chrome::GetTotalBrowserCount());
  ASSERT_EQ(1u, chrome::GetTabbedBrowserCount(browser()->profile(),
                                              browser()->host_desktop_type()));

  // Close the first window.
  Profile* profile = browser()->profile();
  chrome::HostDesktopType host_desktop_type = browser()->host_desktop_type();
  content::WindowedNotificationObserver observer(
        chrome::NOTIFICATION_BROWSER_CLOSED,
        content::NotificationService::AllSources());
  chrome::CloseWindow(browser());
  observer.Wait();

  // There should only be the incognito window open now.
  ASSERT_EQ(1u, chrome::GetTotalBrowserCount());
  ASSERT_EQ(0u, chrome::GetTabbedBrowserCount(profile, host_desktop_type));

  // Run with just an URL specified, no --incognito switch.
  base::FilePath test_file_path = ui_test_utils::GetTestFilePath(
      base::FilePath(), base::FilePath().AppendASCII("empty.html"));
  CommandLine new_command_line(GetCommandLineForRelaunch());
  new_command_line.AppendArgPath(test_file_path);
  content::WindowedNotificationObserver tab_observer(
        chrome::NOTIFICATION_TAB_ADDED,
        content::NotificationService::AllSources());
  Relaunch(new_command_line);
  tab_observer.Wait();

  // There should be one normal and one incognito window now.
  ASSERT_EQ(2u, chrome::GetTotalBrowserCount());
  ASSERT_EQ(1u, chrome::GetTabbedBrowserCount(profile, host_desktop_type));
}

#endif  // !OS_MACOSX
