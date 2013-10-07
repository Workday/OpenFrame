// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/base/in_process_browser_test.h"

#include "base/auto_reset.h"
#include "base/basictypes.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/file_util.h"
#include "base/files/file_path.h"
#include "base/lazy_instance.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/test_file_util.h"
#include "base/threading/non_thread_safe.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/google/google_util.h"
#include "chrome/browser/io_thread.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/net/net_error_tab_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_list_observer.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/host_desktop.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/logging_chrome.h"
#include "chrome/common/url_constants.h"
#include "chrome/renderer/chrome_content_renderer_client.h"
#include "chrome/test/base/chrome_test_suite.h"
#include "chrome/test/base/test_launcher_utils.h"
#include "chrome/test/base/test_switches.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_browser_thread.h"
#include "content/public/test/test_launcher.h"
#include "content/public/test/test_navigation_observer.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/spawned_test_server/spawned_test_server.h"

#if defined(OS_MACOSX)
#include "base/mac/scoped_nsautorelease_pool.h"
#endif

#if defined(OS_WIN) && defined(USE_AURA)
#include "base/win/scoped_com_initializer.h"
#include "base/win/windows_version.h"
#include "ui/base/win/atl_module.h"
#include "win8/test/metro_registration_helper.h"
#include "win8/test/test_registrar_constants.h"
#endif

#if defined(ENABLE_CAPTIVE_PORTAL_DETECTION)
#include "chrome/browser/captive_portal/captive_portal_service.h"
#endif

#if !defined(OS_ANDROID) && !defined(OS_IOS)
#include "chrome/browser/storage_monitor/test_storage_monitor.h"
#endif

namespace {

// Passed as value of kTestType.
const char kBrowserTestType[] = "browser";

// Used when running in single-process mode.
base::LazyInstance<chrome::ChromeContentRendererClient>::Leaky
    g_chrome_content_renderer_client = LAZY_INSTANCE_INITIALIZER;

// A BrowserListObserver that makes sure that all browsers created are on the
// |allowed_desktop_|.
class SingleDesktopTestObserver : public chrome::BrowserListObserver,
                                  public base::NonThreadSafe {
 public:
  explicit SingleDesktopTestObserver(chrome::HostDesktopType allowed_desktop);
  virtual ~SingleDesktopTestObserver();

  // chrome::BrowserListObserver:
  virtual void OnBrowserAdded(Browser* browser) OVERRIDE;

 private:
  chrome::HostDesktopType allowed_desktop_;

  DISALLOW_COPY_AND_ASSIGN(SingleDesktopTestObserver);
};

SingleDesktopTestObserver::SingleDesktopTestObserver(
    chrome::HostDesktopType allowed_desktop)
        : allowed_desktop_(allowed_desktop) {
  BrowserList::AddObserver(this);
}

SingleDesktopTestObserver::~SingleDesktopTestObserver() {
  BrowserList::RemoveObserver(this);
}

void SingleDesktopTestObserver::OnBrowserAdded(Browser* browser) {
  CHECK(CalledOnValidThread());
  CHECK_EQ(browser->host_desktop_type(), allowed_desktop_);
}

}  // namespace

InProcessBrowserTest::InProcessBrowserTest()
    : browser_(NULL),
      exit_when_last_browser_closes_(true),
      multi_desktop_test_(false)
#if defined(OS_MACOSX)
      , autorelease_pool_(NULL)
#endif  // OS_MACOSX
    {
#if defined(OS_MACOSX)
  // TODO(phajdan.jr): Make browser_tests self-contained on Mac, remove this.
  // Before we run the browser, we have to hack the path to the exe to match
  // what it would be if Chrome was running, because it is used to fork renderer
  // processes, on Linux at least (failure to do so will cause a browser_test to
  // be run instead of a renderer).
  base::FilePath chrome_path;
  CHECK(PathService::Get(base::FILE_EXE, &chrome_path));
  chrome_path = chrome_path.DirName();
  chrome_path = chrome_path.Append(chrome::kBrowserProcessExecutablePath);
  CHECK(PathService::Override(base::FILE_EXE, chrome_path));
#endif  // defined(OS_MACOSX)
  CreateTestServer(base::FilePath(FILE_PATH_LITERAL("chrome/test/data")));
  base::FilePath src_dir;
  CHECK(PathService::Get(base::DIR_SOURCE_ROOT, &src_dir));
  embedded_test_server()->ServeFilesFromDirectory(
      src_dir.AppendASCII("chrome/test/data"));
}

InProcessBrowserTest::~InProcessBrowserTest() {
}

void InProcessBrowserTest::SetUp() {
  // Undo TestingBrowserProcess creation in ChromeTestSuite.
  // TODO(phajdan.jr): Extract a smaller test suite so we don't need this.
  DCHECK(g_browser_process);
  BrowserProcess* old_browser_process = g_browser_process;
  // g_browser_process must be NULL during its own destruction.
  g_browser_process = NULL;
  delete old_browser_process;

  CommandLine* command_line = CommandLine::ForCurrentProcess();
  // Allow subclasses to change the command line before running any tests.
  SetUpCommandLine(command_line);
  // Add command line arguments that are used by all InProcessBrowserTests.
  PrepareTestCommandLine(command_line);

  // Create a temporary user data directory if required.
  ASSERT_TRUE(CreateUserDataDirectory())
      << "Could not create user data directory.";

  // Allow subclasses the opportunity to make changes to the default user data
  // dir before running any tests.
  ASSERT_TRUE(SetUpUserDataDirectory())
      << "Could not set up user data directory.";

  // Single-process mode is not set in BrowserMain, so process it explicitly,
  // and set up renderer.
  if (command_line->HasSwitch(switches::kSingleProcess)) {
    content::SetRendererClientForTesting(
        &g_chrome_content_renderer_client.Get());
  }

#if defined(OS_CHROMEOS)
  // Make sure that the log directory exists.
  base::FilePath log_dir = logging::GetSessionLogFile(*command_line).DirName();
  file_util::CreateDirectory(log_dir);
#endif  // defined(OS_CHROMEOS)

  host_resolver_ = new net::RuleBasedHostResolverProc(NULL);

  // See http://en.wikipedia.org/wiki/Web_Proxy_Autodiscovery_Protocol
  // We don't want the test code to use it.
  host_resolver_->AddSimulatedFailure("wpad");

  net::ScopedDefaultHostResolverProc scoped_host_resolver_proc(
      host_resolver_.get());

#if defined(OS_MACOSX)
  // On Mac, without the following autorelease pool, code which is directly
  // executed (as opposed to executed inside a message loop) would autorelease
  // objects into a higher-level pool. This pool is not recycled in-sync with
  // the message loops' pools and causes problems with code relying on
  // deallocation via an autorelease pool (such as browser window closure and
  // browser shutdown). To avoid this, the following pool is recycled after each
  // time code is directly executed.
  autorelease_pool_ = new base::mac::ScopedNSAutoreleasePool;
#endif

#if defined(ENABLE_CAPTIVE_PORTAL_DETECTION)
  captive_portal::CaptivePortalService::set_state_for_testing(
      captive_portal::CaptivePortalService::DISABLED_FOR_TESTING);
#endif

  chrome_browser_net::NetErrorTabHelper::set_state_for_testing(
      chrome_browser_net::NetErrorTabHelper::TESTING_FORCE_DISABLED);

  google_util::SetMockLinkDoctorBaseURLForTesting();

#if defined(OS_WIN) && defined(USE_AURA)
  if (base::win::GetVersion() >= base::win::VERSION_WIN8 &&
      CommandLine::ForCurrentProcess()->HasSwitch(switches::kAshBrowserTests)) {
    com_initializer_.reset(new base::win::ScopedCOMInitializer());
    ui::win::CreateATLModuleIfNeeded();
    ASSERT_TRUE(win8::MakeTestDefaultBrowserSynchronously());
  }
#endif

  BrowserTestBase::SetUp();
}

void InProcessBrowserTest::PrepareTestCommandLine(CommandLine* command_line) {
  // Propagate commandline settings from test_launcher_utils.
  test_launcher_utils::PrepareBrowserCommandLineForTests(command_line);

  // This is a Browser test.
  command_line->AppendSwitchASCII(switches::kTestType, kBrowserTestType);

#if defined(OS_WIN) && defined(USE_AURA)
  if (command_line->HasSwitch(switches::kAshBrowserTests)) {
    command_line->AppendSwitchNative(switches::kViewerLaunchViaAppId,
                                     win8::test::kDefaultTestAppUserModelId);
    // Ash already launches with a single browser opened, add kSilentLaunch to
    // make sure StartupBrowserCreator doesn't attempt to launch a browser on
    // the native desktop on startup.
    command_line->AppendSwitch(switches::kSilentLaunch);
  }
#endif

#if defined(OS_MACOSX)
  // Explicitly set the path of the binary used for child processes, otherwise
  // they'll try to use browser_tests which doesn't contain ChromeMain.
  base::FilePath subprocess_path;
  PathService::Get(base::FILE_EXE, &subprocess_path);
  // Recreate the real environment, run the helper within the app bundle.
  subprocess_path = subprocess_path.DirName().DirName();
  DCHECK_EQ(subprocess_path.BaseName().value(), "Contents");
  subprocess_path =
      subprocess_path.Append("Versions").Append(chrome::kChromeVersion);
  subprocess_path =
      subprocess_path.Append(chrome::kHelperProcessExecutablePath);
  command_line->AppendSwitchPath(switches::kBrowserSubprocessPath,
                                 subprocess_path);
#endif

  // TODO(pkotwicz): Investigate if we can remove this switch.
  if (exit_when_last_browser_closes_)
    command_line->AppendSwitch(switches::kDisableZeroBrowsersOpenForTests);

  if (command_line->GetArgs().empty())
    command_line->AppendArg(content::kAboutBlankURL);
}

bool InProcessBrowserTest::CreateUserDataDirectory() {
  CommandLine* command_line = CommandLine::ForCurrentProcess();
  base::FilePath user_data_dir =
      command_line->GetSwitchValuePath(switches::kUserDataDir);
  if (user_data_dir.empty()) {
    if (temp_user_data_dir_.CreateUniqueTempDir() &&
        temp_user_data_dir_.IsValid()) {
      user_data_dir = temp_user_data_dir_.path();
    } else {
      LOG(ERROR) << "Could not create temporary user data directory \""
                 << temp_user_data_dir_.path().value() << "\".";
      return false;
    }
  }
  return test_launcher_utils::OverrideUserDataDir(user_data_dir);
}

void InProcessBrowserTest::TearDown() {
  DCHECK(!g_browser_process);
#if defined(OS_WIN) && defined(USE_AURA)
  com_initializer_.reset();
#endif
  BrowserTestBase::TearDown();
}

void InProcessBrowserTest::AddTabAtIndexToBrowser(
    Browser* browser,
    int index,
    const GURL& url,
    content::PageTransition transition) {
  chrome::NavigateParams params(browser, url, transition);
  params.tabstrip_index = index;
  params.disposition = NEW_FOREGROUND_TAB;
  chrome::Navigate(&params);

  content::WaitForLoadStop(params.target_contents);
}

void InProcessBrowserTest::AddTabAtIndex(
    int index,
    const GURL& url,
    content::PageTransition transition) {
  AddTabAtIndexToBrowser(browser(), index, url, transition);
}

bool InProcessBrowserTest::SetUpUserDataDirectory() {
  return true;
}

// Creates a browser with a single tab (about:blank), waits for the tab to
// finish loading and shows the browser.
Browser* InProcessBrowserTest::CreateBrowser(Profile* profile) {
  Browser* browser = new Browser(
      Browser::CreateParams(profile, chrome::GetActiveDesktop()));
  AddBlankTabAndShow(browser);
  return browser;
}

Browser* InProcessBrowserTest::CreateIncognitoBrowser() {
  // Create a new browser with using the incognito profile.
  Browser* incognito = new Browser(
      Browser::CreateParams(browser()->profile()->GetOffTheRecordProfile(),
                            chrome::GetActiveDesktop()));
  AddBlankTabAndShow(incognito);
  return incognito;
}

Browser* InProcessBrowserTest::CreateBrowserForPopup(Profile* profile) {
  Browser* browser =
      new Browser(Browser::CreateParams(Browser::TYPE_POPUP, profile,
                  chrome::GetActiveDesktop()));
  AddBlankTabAndShow(browser);
  return browser;
}

Browser* InProcessBrowserTest::CreateBrowserForApp(
    const std::string& app_name,
    Profile* profile) {
  Browser* browser = new Browser(
      Browser::CreateParams::CreateForApp(
          Browser::TYPE_POPUP, app_name, gfx::Rect(), profile,
          chrome::GetActiveDesktop()));
  AddBlankTabAndShow(browser);
  return browser;
}

void InProcessBrowserTest::AddBlankTabAndShow(Browser* browser) {
  content::WindowedNotificationObserver observer(
      content::NOTIFICATION_LOAD_STOP,
      content::NotificationService::AllSources());
  chrome::AddSelectedTabWithURL(browser, GURL(content::kAboutBlankURL),
                                content::PAGE_TRANSITION_AUTO_TOPLEVEL);
  observer.Wait();

  browser->window()->Show();
}

#if !defined(OS_MACOSX)
CommandLine InProcessBrowserTest::GetCommandLineForRelaunch() {
  CommandLine new_command_line(CommandLine::ForCurrentProcess()->GetProgram());
  CommandLine::SwitchMap switches =
      CommandLine::ForCurrentProcess()->GetSwitches();
  switches.erase(switches::kUserDataDir);
  switches.erase(content::kSingleProcessTestsFlag);
  switches.erase(switches::kSingleProcess);
  new_command_line.AppendSwitch(content::kLaunchAsBrowser);

  base::FilePath user_data_dir;
  PathService::Get(chrome::DIR_USER_DATA, &user_data_dir);
  new_command_line.AppendSwitchPath(switches::kUserDataDir, user_data_dir);

  for (CommandLine::SwitchMap::const_iterator iter = switches.begin();
        iter != switches.end(); ++iter) {
    new_command_line.AppendSwitchNative((*iter).first, (*iter).second);
  }
  return new_command_line;
}
#endif

void InProcessBrowserTest::RunTestOnMainThreadLoop() {
  // Pump startup related events.
  content::RunAllPendingInMessageLoop();

#if defined(OS_MACOSX)
  autorelease_pool_->Recycle();
#endif

  chrome::HostDesktopType active_desktop = chrome::GetActiveDesktop();
  // Self-adds/removes itself from the BrowserList observers.
  scoped_ptr<SingleDesktopTestObserver> single_desktop_test_observer;
  if (!multi_desktop_test_) {
    single_desktop_test_observer.reset(
        new SingleDesktopTestObserver(active_desktop));
  }

  const BrowserList* active_browser_list =
      BrowserList::GetInstance(active_desktop);
  if (!active_browser_list->empty()) {
    browser_ = active_browser_list->get(0);
#if defined(USE_ASH)
    // There are cases where windows get created maximized by default.
    if (browser_->window()->IsMaximized())
      browser_->window()->Restore();
#endif
    content::WaitForLoadStop(
        browser_->tab_strip_model()->GetActiveWebContents());
  }

#if !defined(OS_ANDROID) && !defined(OS_IOS)
  // Do not use the real StorageMonitor for tests, which introduces another
  // source of variability and potential slowness.
  ASSERT_TRUE(chrome::test::TestStorageMonitor::CreateForBrowserTests());
#endif

  // Pump any pending events that were created as a result of creating a
  // browser.
  content::RunAllPendingInMessageLoop();

  SetUpOnMainThread();
#if defined(OS_MACOSX)
  autorelease_pool_->Recycle();
#endif

  if (!HasFatalFailure())
    RunTestOnMainThread();
#if defined(OS_MACOSX)
  autorelease_pool_->Recycle();
#endif

  // Invoke cleanup and quit even if there are failures. This is similar to
  // gtest in that it invokes TearDown even if Setup fails.
  CleanUpOnMainThread();
#if defined(OS_MACOSX)
  autorelease_pool_->Recycle();
#endif

  // Sometimes tests leave Quit tasks in the MessageLoop (for shame), so let's
  // run all pending messages here to avoid preempting the QuitBrowsers tasks.
  // TODO(jbates) Once crbug.com/134753 is fixed, this can be removed because it
  // will not be possible to post Quit tasks.
  content::RunAllPendingInMessageLoop();

  QuitBrowsers();
  // All BrowserLists should be empty at this point.
  for (chrome::HostDesktopType t = chrome::HOST_DESKTOP_TYPE_FIRST;
       t < chrome::HOST_DESKTOP_TYPE_COUNT;
       t = static_cast<chrome::HostDesktopType>(t + 1)) {
    CHECK(BrowserList::GetInstance(t)->empty()) << t;
  }
}

void InProcessBrowserTest::QuitBrowsers() {
  if (chrome::GetTotalBrowserCount() == 0)
    return;

  // Invoke AttemptExit on a running message loop.
  // AttemptExit exits the message loop after everything has been
  // shut down properly.
  base::MessageLoopForUI::current()->PostTask(FROM_HERE,
                                              base::Bind(&chrome::AttemptExit));
  content::RunMessageLoop();

#if defined(OS_MACOSX)
  // chrome::AttemptExit() will attempt to close all browsers by deleting
  // their tab contents. The last tab contents being removed triggers closing of
  // the browser window.
  //
  // On the Mac, this eventually reaches
  // -[BrowserWindowController windowWillClose:], which will post a deferred
  // -autorelease on itself to ultimately destroy the Browser object. The line
  // below is necessary to pump these pending messages to ensure all Browsers
  // get deleted.
  content::RunAllPendingInMessageLoop();
  delete autorelease_pool_;
  autorelease_pool_ = NULL;
#endif
}
