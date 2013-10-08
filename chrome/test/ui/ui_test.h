// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_TEST_UI_UI_TEST_H_
#define CHROME_TEST_UI_UI_TEST_H_

// This file provides a common base for running UI unit tests, which operate
// the entire browser application in a separate process for holistic
// functional testing.
//
// Tests should #include this file, subclass UITest, and use the TEST_F macro
// to declare individual test cases.  This provides a running browser window
// during the test, accessible through the window_ member variable.  The window
// will close when the test ends, regardless of whether the test passed.
//
// Tests which need to launch the browser with a particular set of command-line
// arguments should set the value of launch_arguments_ in their constructors.

#include <string>

#include "base/command_line.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/process/process.h"
#include "base/time/time.h"
#include "chrome/test/automation/proxy_launcher.h"
#include "testing/platform_test.h"
#include "url/gurl.h"

class AutomationProxy;
class BrowserProxy;
class GURL;
class TabProxy;

namespace base {
class DictionaryValue;
class FilePath;
}

// Base class for UI Tests. This implements the core of the functions.
// This base class decouples all automation functionality from testing
// infrastructure, for use without gtest.
// If using gtest, you probably want to inherit from UITest (declared below)
// rather than UITestBase.
class UITestBase {
 public:
  // ********* Utility functions *********

  // Launches the browser only.
  void LaunchBrowser();

  // Launches the browser and IPC testing connection in server mode.
  void LaunchBrowserAndServer();

  // Launches the IPC testing connection in client mode,
  // which then attempts to connect to a browser.
  void ConnectToRunningBrowser();

  // Only for pyauto.
  base::TimeDelta action_timeout();
  int action_timeout_ms();
  void set_action_timeout(base::TimeDelta timeout);
  void set_action_timeout_ms(int timeout);

  // Overridable so that derived classes can provide their own ProxyLauncher.
  virtual ProxyLauncher* CreateProxyLauncher();

  // Closes the browser and IPC testing server.
  void CloseBrowserAndServer();

  // Launches the browser with the given command line.
  // TODO(phajdan.jr): Make LaunchBrowser private.
  void LaunchBrowser(const CommandLine& cmdline, bool clear_profile);

  // Exits out browser instance.
  void QuitBrowser();

  // Tells the browser to navigate to the given URL in the active tab
  // of the first app window.
  // This method doesn't return until the navigation is complete.
  void NavigateToURL(const GURL& url);

  // Navigate to the given URL in the active tab of the given app window.
  void NavigateToURL(const GURL& url, int window_index);

  // Same as above, except in the given tab and window.
  void NavigateToURL(const GURL& url, int window_index, int tab_index);

  // Tells the browser to navigate to the given URL in the active tab
  // of the first app window.
  // This method doesn't return until the |number_of_navigations| navigations
  // complete.
  void NavigateToURLBlockUntilNavigationsComplete(const GURL& url,
                                                  int number_of_navigations);

  // Same as above, except in the given tab and window.
  void NavigateToURLBlockUntilNavigationsComplete(const GURL& url,
      int number_of_navigations, int tab_index, int window_index);

  // Returns the URL of the currently active tab. Only looks in the first
  // window, for backward compatibility. If there is no active tab, or some
  // other error, the returned URL will be empty.
  GURL GetActiveTabURL() { return GetActiveTabURL(0); }

  // Like above, but looks at the window at the given index.
  GURL GetActiveTabURL(int window_index);

  // Returns the title of the currently active tab. Only looks in the first
  // window, for backward compatibility.
  std::wstring GetActiveTabTitle() { return GetActiveTabTitle(0); }

  // Like above, but looks at the window at the given index.
  std::wstring GetActiveTabTitle(int window_index);

  // Returns the tabstrip index of the currently active tab in the window at
  // the given index, or -1 on error. Only looks in the first window, for
  // backward compatibility.
  int GetActiveTabIndex() { return GetActiveTabIndex(0); }

  // Like above, but looks at the window at the given index.
  int GetActiveTabIndex(int window_index);

  // Returns the number of tabs in the first window.  If no windows exist,
  // causes a test failure and returns 0.
  int GetTabCount();

  // Same as GetTabCount(), except with the window at the given index.
  int GetTabCount(int window_index);

  // Polls up to kWaitForActionMaxMsec ms to attain a specific tab count. Will
  // assert that the tab count is valid at the end of the wait.
  void WaitUntilTabCount(int tab_count);

  // Closes the specified browser.  Returns true if the browser was closed.
  // This call is blocking.  |application_closed| is set to true if this was
  // the last browser window (and therefore as a result of it closing the
  // browser process terminated).  Note that in that case this method returns
  // after the browser process has terminated.
  bool CloseBrowser(BrowserProxy* browser, bool* application_closed) const;

  // Gets the executable file path of the Chrome browser process.
  const base::FilePath::CharType* GetExecutablePath();

  // Return the user data directory being used by the browser instance in
  // UITest::SetUp().
  base::FilePath user_data_dir() const {
    return launcher_->user_data_dir();
  }

  // Called by some tests that wish to have a base profile to start from. This
  // "user data directory" (containing one or more profiles) will be recursively
  // copied into the user data directory for the test and the files will be
  // evicted from the OS cache. To start with a blank profile, supply an empty
  // string (the default).
  const base::FilePath& template_user_data() const { return template_user_data_; }
  void set_template_user_data(const base::FilePath& template_user_data) {
    template_user_data_ = template_user_data;
  }

  // Get the handle of browser process connected to the automation. This
  // function only returns a reference to the handle so the caller does not
  // own the handle returned.
  base::ProcessHandle process() const { return launcher_->process(); }

  // Return the process id of the browser process (-1 on error).
  base::ProcessId browser_process_id() const { return launcher_->process_id(); }

  // Return the time when the browser was run.
  base::TimeTicks browser_launch_time() const {
    return launcher_->browser_launch_time();
  }

  // Return how long the shutdown took.
  base::TimeDelta browser_quit_time() const {
    return launcher_->browser_quit_time();
  }

  // Fetch the state which determines whether the profile will be cleared on
  // next startup.
  bool get_clear_profile() const {
    return clear_profile_;
  }
  // Sets clear_profile_. Should be called before launching browser to have
  // any effect.
  void set_clear_profile(bool clear_profile) {
    clear_profile_ = clear_profile;
  }

  // homepage_ accessor.
  std::string homepage() {
    return homepage_;
  }

  // Sets homepage_. Should be called before launching browser to have
  // any effect.
  void set_homepage(const std::string& homepage) {
    homepage_ = homepage;
  }

  void set_test_name(const std::string& name) {
    test_name_ = name;
  }

  // Sets the shutdown type, which defaults to WINDOW_CLOSE.
  void set_shutdown_type(ProxyLauncher::ShutdownType value) {
    launcher_->set_shutdown_type(value);
  }

  // Get the number of crash dumps we've logged since the test started.
  int GetCrashCount() const;

  // Returns empty string if there were no unexpected Chrome asserts or crashes,
  // a string describing the failures otherwise. As a side effect, it will fail
  // with EXPECT_EQ macros if this code runs within a gtest harness.
  std::string CheckErrorsAndCrashes() const;

  // Use Chromium binaries from the given directory.
  void SetBrowserDirectory(const base::FilePath& dir);

  // Appends a command-line switch (no associated value) to be passed to the
  // browser when launched.
  void AppendBrowserLaunchSwitch(const char* name);

  // Appends a command-line switch with associated value to be passed to the
  // browser when launched.
  void AppendBrowserLaunchSwitch(const char* name, const char* value);

  // Pass-through to AutomationProxy::BeginTracing.
  bool BeginTracing(const std::string& category_patterns);

  // Pass-through to AutomationProxy::EndTracing.
  std::string EndTracing();

 protected:
  // String to display when a test fails because the crash service isn't
  // running.
  static const wchar_t kFailedNoCrashService[];

  UITestBase();
  explicit UITestBase(base::MessageLoop::Type msg_loop_type);

  virtual ~UITestBase();

  // Starts the browser using the arguments in launch_arguments_, and
  // sets up member variables.
  virtual void SetUp();

  // Closes the browser window.
  virtual void TearDown();

  virtual AutomationProxy* automation() const;

  ProxyLauncher::LaunchState DefaultLaunchState();

  // Extra command-line switches that need to be passed to the browser are
  // added in this function. Add new command-line switches here.
  virtual void SetLaunchSwitches();

  // Called by the ProxyLauncher just before the browser is launched, allowing
  // setup of the profile for the runtime environment..
  virtual void SetUpProfile();

  // Returns the proxy for the currently active tab, or NULL if there is no
  // tab or there was some kind of error. Only looks at the first window, for
  // backward compatibility. The returned pointer MUST be deleted by the
  // caller if non-NULL.
  scoped_refptr<TabProxy> GetActiveTab();

  // Like above, but looks at the window at the given index.
  scoped_refptr<TabProxy> GetActiveTab(int window_index);

  // ********* Member variables *********

  // Path to the browser executable.
  base::FilePath browser_directory_;

  // Path to the unit test data.
  base::FilePath test_data_directory_;

  // Command to launch the browser
  CommandLine launch_arguments_;

  // The number of errors expected during the run (generally 0).
  size_t expected_errors_;

  // The number of crashes expected during the run (generally 0).
  int expected_crashes_;

  // Homepage used for testing.
  std::string homepage_;

  // Name of currently running automated test passed to Chrome process.
  std::string test_name_;

  // Wait for initial loads to complete in SetUp() before running test body.
  bool wait_for_initial_loads_;

  // This can be set to true to have the test run the dom automation case.
  bool dom_automation_enabled_;

  // This can be set to true to enable the stats collection controller JS
  // bindings.
  bool stats_collection_controller_enabled_;

  // See set_template_user_data().
  base::FilePath template_user_data_;

  // Determines if the window is shown or hidden. Defaults to hidden.
  bool show_window_;

  // If true the profile is cleared before launching. Default is true.
  bool clear_profile_;

  // Should we supply the testing channel id
  // on the command line? Default is true.
  bool include_testing_id_;

  // Enable file cookies, default is true.
  bool enable_file_cookies_;

  // Launches browser and AutomationProxy.
  scoped_ptr<ProxyLauncher> launcher_;

  // PID file for websocket server.
  base::FilePath websocket_pid_file_;

 private:
  // Time the test was started (so we can check for new crash dumps)
  base::Time test_start_time_;
};

class UITest : public UITestBase, public PlatformTest {
 protected:
  UITest() {}
  explicit UITest(base::MessageLoop::Type msg_loop_type)
      : UITestBase(), PlatformTest(), message_loop_(msg_loop_type) {}

  virtual void SetUp() OVERRIDE;
  virtual void TearDown() OVERRIDE;

  virtual ProxyLauncher* CreateProxyLauncher() OVERRIDE;

  // Count the number of active browser processes launched by this test.
  // The count includes browser sub-processes.
  bool GetBrowserProcessCount(int* count) WARN_UNUSED_RESULT;

  // Returns a copy of local state preferences. The caller is responsible for
  // deleting the returned object. Returns NULL if there is an error.
  base::DictionaryValue* GetLocalState();

  // Returns a copy of the default profile preferences. The caller is
  // responsible for deleting the returned object. Returns NULL if there is an
  // error.
  base::DictionaryValue* GetDefaultProfilePreferences();

  // Waits for the test case to finish.
  // ASSERTS if there are test failures.
  void WaitForFinish(const std::string &name,
                     const std::string &id, const GURL &url,
                     const std::string& test_complete_cookie,
                     const std::string& expected_cookie_value,
                     const base::TimeDelta wait_time);

  // Polls the tab for a JavaScript condition and returns once one of the
  // following conditions hold true:
  // - The JavaScript condition evaluates to true (return true).
  // - The browser process died (return false).
  // - The timeout value has been exceeded (return false).
  //
  // The JavaScript expression is executed in the context of the frame that
  // matches the provided xpath.
  bool WaitUntilJavaScriptCondition(TabProxy* tab,
                                    const std::wstring& frame_xpath,
                                    const std::wstring& jscript,
                                    base::TimeDelta timeout);

  // Polls the tab for the cookie_name cookie and returns once one of the
  // following conditions hold true:
  // - The cookie is of expected_value.
  // - The browser process died.
  // - The timeout value has been exceeded.
  bool WaitUntilCookieValue(TabProxy* tab, const GURL& url,
                            const char* cookie_name,
                            base::TimeDelta timeout,
                            const char* expected_value);

  // Polls the tab for the cookie_name cookie and returns once one of the
  // following conditions hold true:
  // - The cookie is set to any value.
  // - The browser process died.
  // - The timeout value has been exceeded.
  std::string WaitUntilCookieNonEmpty(TabProxy* tab,
                                      const GURL& url,
                                      const char* cookie_name,
                                      base::TimeDelta timeout);

  // Waits until the Find window has become fully visible (if |wait_for_open| is
  // true) or fully hidden (if |wait_for_open| is false). This function can time
  // out (return false) if the window doesn't appear within a specific time.
  bool WaitForFindWindowVisibilityChange(BrowserProxy* browser,
                                         bool wait_for_open);

  // Terminates the browser, simulates end of session.
  void TerminateBrowser();

  // Tells the browser to navigate to the given URL in the active tab
  // of the first app window.
  // Does not wait for the navigation to complete to return.
  // To avoid intermittent test failures, use NavigateToURL instead, if
  // possible.
  void NavigateToURLAsync(const GURL& url);

 private:
  base::MessageLoop message_loop_;  // Enables PostTask to main thread.
};

// These exist only to support the gTest assertion macros, and
// shouldn't be used in normal program code.
#ifdef UNIT_TEST
std::ostream& operator<<(std::ostream& out, const std::wstring& wstr);

template<typename T>
std::ostream& operator<<(std::ostream& out, const ::scoped_ptr<T>& ptr) {
  return out << ptr.get();
}
#endif  // UNIT_TEST

#endif  // CHROME_TEST_UI_UI_TEST_H_
