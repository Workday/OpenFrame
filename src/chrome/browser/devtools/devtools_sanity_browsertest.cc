// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/cancelable_callback.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/memory/ref_counted.h"
#include "base/path_service.h"
#include "base/prefs/pref_service.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/test_timeouts.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/devtools/browser_list_tabcontents_provider.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/extensions/unpacked_installer.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/test_switches.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/child_process_data.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/devtools_client_host.h"
#include "content/public/browser/devtools_http_handler.h"
#include "content/public/browser/devtools_manager.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/worker_service.h"
#include "content/public/browser/worker_service_observer.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test_utils.h"
#include "extensions/common/switches.h"
#include "net/socket/tcp_listen_socket.h"
#include "net/test/spawned_test_server/spawned_test_server.h"

using content::BrowserThread;
using content::DevToolsManager;
using content::DevToolsAgentHost;
using content::NavigationController;
using content::RenderViewHost;
using content::WebContents;
using content::WorkerService;
using content::WorkerServiceObserver;

namespace {

// Used to block until a dev tools client window's browser is closed.
class BrowserClosedObserver : public content::NotificationObserver {
 public:
  explicit BrowserClosedObserver(Browser* browser) {
    registrar_.Add(this, chrome::NOTIFICATION_BROWSER_CLOSED,
                   content::Source<Browser>(browser));
    content::RunMessageLoop();
  }

  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE {
    base::MessageLoopForUI::current()->Quit();
  }

 private:
  content::NotificationRegistrar registrar_;
  DISALLOW_COPY_AND_ASSIGN(BrowserClosedObserver);
};

// The delay waited in some cases where we don't have a notifications for an
// action we take.
const int kActionDelayMs = 500;

const char kDebuggerTestPage[] = "files/devtools/debugger_test_page.html";
const char kPauseWhenLoadingDevTools[] =
    "files/devtools/pause_when_loading_devtools.html";
const char kPauseWhenScriptIsRunning[] =
    "files/devtools/pause_when_script_is_running.html";
const char kPageWithContentScript[] =
    "files/devtools/page_with_content_script.html";
const char kNavigateBackTestPage[] =
    "files/devtools/navigate_back.html";
const char kChunkedTestPage[] = "chunked";
const char kSlowTestPage[] =
    "chunked?waitBeforeHeaders=100&waitBetweenChunks=100&chunksNumber=2";
const char kSharedWorkerTestPage[] =
    "files/workers/workers_ui_shared_worker.html";
const char kReloadSharedWorkerTestPage[] =
    "files/workers/debug_shared_worker_initialization.html";

void RunTestFunction(DevToolsWindow* window, const char* test_name) {
  std::string result;

  // At first check that JavaScript part of the front-end is loaded by
  // checking that global variable uiTests exists(it's created after all js
  // files have been loaded) and has runTest method.
  ASSERT_TRUE(
      content::ExecuteScriptAndExtractString(
          window->GetRenderViewHost(),
          "window.domAutomationController.send("
          "    '' + (window.uiTests && (typeof uiTests.runTest)));",
          &result));

  if (result == "function") {
    ASSERT_TRUE(
        content::ExecuteScriptAndExtractString(
            window->GetRenderViewHost(),
            base::StringPrintf("uiTests.runTest('%s')", test_name),
            &result));
    EXPECT_EQ("[OK]", result);
  } else {
    FAIL() << "DevTools front-end is broken.";
  }
}

class DevToolsSanityTest : public InProcessBrowserTest {
 public:
  DevToolsSanityTest()
      : window_(NULL),
        inspected_rvh_(NULL) {}

 protected:
  void RunTest(const std::string& test_name, const std::string& test_page) {
    OpenDevToolsWindow(test_page);
    RunTestFunction(window_, test_name.c_str());
    CloseDevToolsWindow();
  }

  void OpenDevToolsWindow(const std::string& test_page) {
    ASSERT_TRUE(test_server()->Start());
    GURL url = test_server()->GetURL(test_page);
    ui_test_utils::NavigateToURL(browser(), url);

    content::WindowedNotificationObserver observer(
        content::NOTIFICATION_LOAD_STOP,
        content::NotificationService::AllSources());
    inspected_rvh_ = GetInspectedTab()->GetRenderViewHost();
    window_ = DevToolsWindow::OpenDevToolsWindow(inspected_rvh_);
    observer.Wait();
  }

  WebContents* GetInspectedTab() {
    return browser()->tab_strip_model()->GetWebContentsAt(0);
  }

  void CloseDevToolsWindow() {
    DevToolsManager* devtools_manager = DevToolsManager::GetInstance();
    // CloseAllClientHosts may destroy window_ so store the browser first.
    Browser* browser = window_->browser();
    devtools_manager->CloseAllClientHosts();
    // Wait only when DevToolsWindow has a browser. For docked DevTools, this
    // is NULL and we skip the wait.
    if (browser)
      BrowserClosedObserver close_observer(browser);
  }

  DevToolsWindow* window_;
  RenderViewHost* inspected_rvh_;
};

void TimeoutCallback(const std::string& timeout_message) {
  FAIL() << timeout_message;
  base::MessageLoop::current()->Quit();
}

// Base class for DevTools tests that test devtools functionality for
// extensions and content scripts.
class DevToolsExtensionTest : public DevToolsSanityTest,
                              public content::NotificationObserver {
 public:
  DevToolsExtensionTest() : DevToolsSanityTest() {
    PathService::Get(chrome::DIR_TEST_DATA, &test_extensions_dir_);
    test_extensions_dir_ = test_extensions_dir_.AppendASCII("devtools");
    test_extensions_dir_ = test_extensions_dir_.AppendASCII("extensions");
  }

 protected:
  // Load an extension from test\data\devtools\extensions\<extension_name>
  void LoadExtension(const char* extension_name) {
    base::FilePath path = test_extensions_dir_.AppendASCII(extension_name);
    ASSERT_TRUE(LoadExtensionFromPath(path)) << "Failed to load extension.";
  }

 private:
  bool LoadExtensionFromPath(const base::FilePath& path) {
    ExtensionService* service = extensions::ExtensionSystem::Get(
        browser()->profile())->extension_service();
    size_t num_before = service->extensions()->size();
    {
      content::NotificationRegistrar registrar;
      registrar.Add(this, chrome::NOTIFICATION_EXTENSION_LOADED,
                    content::NotificationService::AllSources());
      base::CancelableClosure timeout(
          base::Bind(&TimeoutCallback, "Extension load timed out."));
      base::MessageLoop::current()->PostDelayedTask(
          FROM_HERE, timeout.callback(), TestTimeouts::action_timeout());
      extensions::UnpackedInstaller::Create(service)->Load(path);
      content::RunMessageLoop();
      timeout.Cancel();
    }
    size_t num_after = service->extensions()->size();
    if (num_after != (num_before + 1))
      return false;

    return WaitForExtensionViewsToLoad();
  }

  bool WaitForExtensionViewsToLoad() {
    // Wait for all the extension render views that exist to finish loading.
    // NOTE: This assumes that the extension views list is not changing while
    // this method is running.

    content::NotificationRegistrar registrar;
    registrar.Add(this, chrome::NOTIFICATION_EXTENSION_HOST_DID_STOP_LOADING,
                  content::NotificationService::AllSources());
    base::CancelableClosure timeout(
        base::Bind(&TimeoutCallback, "Extension host load timed out."));
    base::MessageLoop::current()->PostDelayedTask(
        FROM_HERE, timeout.callback(), TestTimeouts::action_timeout());

    ExtensionProcessManager* manager =
        extensions::ExtensionSystem::Get(browser()->profile())->
            process_manager();
    ExtensionProcessManager::ViewSet all_views = manager->GetAllViews();
    for (ExtensionProcessManager::ViewSet::const_iterator iter =
             all_views.begin();
         iter != all_views.end();) {
      if (!(*iter)->IsLoading())
        ++iter;
      else
        content::RunMessageLoop();
    }

    timeout.Cancel();
    return true;
  }

  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE {
    switch (type) {
      case chrome::NOTIFICATION_EXTENSION_LOADED:
      case chrome::NOTIFICATION_EXTENSION_HOST_DID_STOP_LOADING:
        base::MessageLoopForUI::current()->Quit();
        break;
      default:
        NOTREACHED();
        break;
    }
  }

  base::FilePath test_extensions_dir_;
};

class DevToolsExperimentalExtensionTest : public DevToolsExtensionTest {
 public:
  virtual void SetUpCommandLine(CommandLine* command_line) OVERRIDE {
    command_line->AppendSwitch(
        extensions::switches::kEnableExperimentalExtensionApis);
  }
};

class WorkerDevToolsSanityTest : public InProcessBrowserTest {
 public:
  WorkerDevToolsSanityTest() : window_(NULL) {}

 protected:
  class WorkerData : public base::RefCountedThreadSafe<WorkerData> {
   public:
    WorkerData() : worker_process_id(0), worker_route_id(0) {}
    int worker_process_id;
    int worker_route_id;

   private:
    friend class base::RefCountedThreadSafe<WorkerData>;
    ~WorkerData() {}
  };

  class WorkerCreationObserver : public WorkerServiceObserver {
   public:
    explicit WorkerCreationObserver(WorkerData* worker_data)
        : worker_data_(worker_data) {
    }

   private:
    virtual ~WorkerCreationObserver() {}

    virtual void WorkerCreated (
        const GURL& url,
        const string16& name,
        int process_id,
        int route_id) OVERRIDE {
      worker_data_->worker_process_id = process_id;
      worker_data_->worker_route_id = route_id;
      WorkerService::GetInstance()->RemoveObserver(this);
      BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
          base::MessageLoop::QuitClosure());
      delete this;
    }
    scoped_refptr<WorkerData> worker_data_;
  };

  class WorkerTerminationObserver : public WorkerServiceObserver {
   public:
    explicit WorkerTerminationObserver(WorkerData* worker_data)
        : worker_data_(worker_data) {
    }

   private:
    virtual ~WorkerTerminationObserver() {}

    virtual void WorkerDestroyed(int process_id, int route_id) OVERRIDE {
      ASSERT_EQ(worker_data_->worker_process_id, process_id);
      ASSERT_EQ(worker_data_->worker_route_id, route_id);
      WorkerService::GetInstance()->RemoveObserver(this);
      BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
          base::MessageLoop::QuitClosure());
      delete this;
    }
    scoped_refptr<WorkerData> worker_data_;
  };

  void RunTest(const char* test_name, const char* test_page) {
    ASSERT_TRUE(test_server()->Start());
    GURL url = test_server()->GetURL(test_page);
    ui_test_utils::NavigateToURL(browser(), url);

    scoped_refptr<WorkerData> worker_data = WaitForFirstSharedWorker();
    OpenDevToolsWindowForSharedWorker(worker_data.get());
    RunTestFunction(window_, test_name);
    CloseDevToolsWindow();
  }

  static void TerminateWorkerOnIOThread(scoped_refptr<WorkerData> worker_data) {
    if (WorkerService::GetInstance()->TerminateWorker(
            worker_data->worker_process_id, worker_data->worker_route_id)) {
      WorkerService::GetInstance()->AddObserver(
          new WorkerTerminationObserver(worker_data.get()));
      return;
    }
    FAIL() << "Failed to terminate worker.\n";
  }

  static void TerminateWorker(scoped_refptr<WorkerData> worker_data) {
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        base::Bind(&TerminateWorkerOnIOThread, worker_data));
    content::RunMessageLoop();
  }

  static void WaitForFirstSharedWorkerOnIOThread(
      scoped_refptr<WorkerData> worker_data) {
    std::vector<WorkerService::WorkerInfo> worker_info =
        WorkerService::GetInstance()->GetWorkers();
    if (!worker_info.empty()) {
      worker_data->worker_process_id = worker_info[0].process_id;
      worker_data->worker_route_id = worker_info[0].route_id;
      BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
          base::MessageLoop::QuitClosure());
      return;
    }

    WorkerService::GetInstance()->AddObserver(
        new WorkerCreationObserver(worker_data.get()));
  }

  static scoped_refptr<WorkerData> WaitForFirstSharedWorker() {
    scoped_refptr<WorkerData> worker_data(new WorkerData());
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        base::Bind(&WaitForFirstSharedWorkerOnIOThread, worker_data));
    content::RunMessageLoop();
    return worker_data;
  }

  void OpenDevToolsWindowForSharedWorker(WorkerData* worker_data) {
    Profile* profile = browser()->profile();
    window_ = DevToolsWindow::CreateDevToolsWindowForWorker(profile);
    window_->Show(DEVTOOLS_TOGGLE_ACTION_SHOW);
    scoped_refptr<DevToolsAgentHost> agent_host(
        DevToolsAgentHost::GetForWorker(
            worker_data->worker_process_id,
            worker_data->worker_route_id));
    DevToolsManager::GetInstance()->RegisterDevToolsClientHostFor(
        agent_host.get(), window_->GetDevToolsClientHostForTest());
    RenderViewHost* client_rvh = window_->GetRenderViewHost();
    WebContents* client_contents = WebContents::FromRenderViewHost(client_rvh);
    if (client_contents->IsLoading()) {
      content::WindowedNotificationObserver observer(
          content::NOTIFICATION_LOAD_STOP,
          content::Source<NavigationController>(
              &client_contents->GetController()));
      observer.Wait();
    }
  }

  void CloseDevToolsWindow() {
    Browser* browser = window_->browser();
    browser->tab_strip_model()->CloseAllTabs();
    BrowserClosedObserver close_observer(browser);
  }

  DevToolsWindow* window_;
};


// Tests scripts panel showing.
IN_PROC_BROWSER_TEST_F(DevToolsSanityTest, TestShowScriptsTab) {
  RunTest("testShowScriptsTab", kDebuggerTestPage);
}

// Tests that scripts tab is populated with inspected scripts even if it
// hadn't been shown by the moment inspected paged refreshed.
// @see http://crbug.com/26312
IN_PROC_BROWSER_TEST_F(
    DevToolsSanityTest,
    TestScriptsTabIsPopulatedOnInspectedPageRefresh) {
  // Clear inspector settings to ensure that Elements will be
  // current panel when DevTools window is open.
  content::BrowserContext* browser_context =
      GetInspectedTab()->GetBrowserContext();
  Profile::FromBrowserContext(browser_context)->GetPrefs()->
      ClearPref(prefs::kWebKitInspectorSettings);

  RunTest("testScriptsTabIsPopulatedOnInspectedPageRefresh",
          kDebuggerTestPage);
}

// Tests that chrome.devtools extension is correctly exposed.
IN_PROC_BROWSER_TEST_F(DevToolsExtensionTest,
                       TestDevToolsExtensionAPI) {
  LoadExtension("devtools_extension");
  RunTest("waitForTestResultsInConsole", std::string());
}

// Disabled on Windows due to flakiness. http://crbug.com/183649
#if defined(OS_WIN)
#define MAYBE_TestDevToolsExtensionMessaging DISABLED_TestDevToolsExtensionMessaging
#else
#define MAYBE_TestDevToolsExtensionMessaging TestDevToolsExtensionMessaging
#endif

// Tests that chrome.devtools extension can communicate with background page
// using extension messaging.
IN_PROC_BROWSER_TEST_F(DevToolsExtensionTest,
                       MAYBE_TestDevToolsExtensionMessaging) {
  LoadExtension("devtools_messaging");
  RunTest("waitForTestResultsInConsole", std::string());
}

// Tests that chrome.experimental.devtools extension is correctly exposed
// when the extension has experimental permission.
IN_PROC_BROWSER_TEST_F(DevToolsExperimentalExtensionTest,
                       TestDevToolsExperimentalExtensionAPI) {
  LoadExtension("devtools_experimental");
  RunTest("waitForTestResultsInConsole", std::string());
}

// Tests that a content script is in the scripts list.
// http://crbug.com/114104
IN_PROC_BROWSER_TEST_F(DevToolsExtensionTest,
                       TestContentScriptIsPresent) {
  LoadExtension("simple_content_script");
  RunTest("testContentScriptIsPresent", kPageWithContentScript);
}

// Tests that scripts are not duplicated after Scripts Panel switch.
IN_PROC_BROWSER_TEST_F(DevToolsSanityTest,
                       TestNoScriptDuplicatesOnPanelSwitch) {
  RunTest("testNoScriptDuplicatesOnPanelSwitch", kDebuggerTestPage);
}

// Tests that debugger works correctly if pause event occurs when DevTools
// frontend is being loaded.
IN_PROC_BROWSER_TEST_F(DevToolsSanityTest,
                       TestPauseWhenLoadingDevTools) {
  RunTest("testPauseWhenLoadingDevTools", kPauseWhenLoadingDevTools);
}

// Tests that pressing 'Pause' will pause script execution if the script
// is already running.
#if defined(OS_WIN)
// Timing out on windows tryservers: http://crbug.com/219515
#define MAYBE_TestPauseWhenScriptIsRunning DISABLED_TestPauseWhenScriptIsRunning
#elif defined(OS_LINUX) && !defined(OS_CHROMEOS) && defined(ARCH_CPU_ARM_FAMILY)
// Timing out on linux ARM bot: https://crbug/238453
#define MAYBE_TestPauseWhenScriptIsRunning DISABLED_TestPauseWhenScriptIsRunning
#else
#define MAYBE_TestPauseWhenScriptIsRunning TestPauseWhenScriptIsRunning
#endif
IN_PROC_BROWSER_TEST_F(DevToolsSanityTest, MAYBE_TestPauseWhenScriptIsRunning) {
  RunTest("testPauseWhenScriptIsRunning", kPauseWhenScriptIsRunning);
}

// Tests network timing.
IN_PROC_BROWSER_TEST_F(DevToolsSanityTest, TestNetworkTiming) {
  RunTest("testNetworkTiming", kSlowTestPage);
}

// Tests network size.
IN_PROC_BROWSER_TEST_F(DevToolsSanityTest, TestNetworkSize) {
  RunTest("testNetworkSize", kChunkedTestPage);
}

// Tests raw headers text.
IN_PROC_BROWSER_TEST_F(DevToolsSanityTest, TestNetworkSyncSize) {
  RunTest("testNetworkSyncSize", kChunkedTestPage);
}

// Tests raw headers text.
IN_PROC_BROWSER_TEST_F(DevToolsSanityTest, TestNetworkRawHeadersText) {
  RunTest("testNetworkRawHeadersText", kChunkedTestPage);
}

// Tests that console messages are not duplicated on navigation back.
// Disabled: http://crbug.com/260341
IN_PROC_BROWSER_TEST_F(DevToolsSanityTest, DISABLED_TestConsoleOnNavigateBack) {
  RunTest("testConsoleOnNavigateBack", kNavigateBackTestPage);
}


// Tests that external navigation from inspector page is always handled by
// DevToolsWindow and results in inspected page navigation.
IN_PROC_BROWSER_TEST_F(DevToolsSanityTest, TestDevToolsExternalNavigation) {
  OpenDevToolsWindow(kDebuggerTestPage);
  GURL url = test_server()->GetURL(kNavigateBackTestPage);
  content::WindowedNotificationObserver observer(
      content::NOTIFICATION_LOAD_STOP,
      content::NotificationService::AllSources());
  ASSERT_TRUE(content::ExecuteScript(
      window_->web_contents(),
      std::string("window.location = \"") + url.spec() + "\""));
  observer.Wait();

  ASSERT_TRUE(window_->web_contents()->GetURL().
                  SchemeIs(chrome::kChromeDevToolsScheme));
  ASSERT_EQ(GetInspectedTab()->GetURL(), url);
}

#if defined(OS_WIN)
// Flakily times out: http://crbug.com/163411
#define MAYBE_TestReattachAfterCrash DISABLED_TestReattachAfterCrash
#else
#define MAYBE_TestReattachAfterCrash TestReattachAfterCrash
#endif
// Tests that inspector will reattach to inspected page when it is reloaded
// after a crash. See http://crbug.com/101952
IN_PROC_BROWSER_TEST_F(DevToolsSanityTest, MAYBE_TestReattachAfterCrash) {
  OpenDevToolsWindow(kDebuggerTestPage);

  content::CrashTab(GetInspectedTab());
  content::WindowedNotificationObserver observer(
      content::NOTIFICATION_LOAD_STOP,
      content::Source<NavigationController>(
          &browser()->tab_strip_model()->GetActiveWebContents()->
              GetController()));
  chrome::Reload(browser(), CURRENT_TAB);
  observer.Wait();

  RunTestFunction(window_, "testReattachAfterCrash");
  CloseDevToolsWindow();
}

IN_PROC_BROWSER_TEST_F(DevToolsSanityTest, TestPageWithNoJavaScript) {
  OpenDevToolsWindow("about:blank");
  std::string result;
  ASSERT_TRUE(
      content::ExecuteScriptAndExtractString(
          window_->GetRenderViewHost(),
          "window.domAutomationController.send("
          "    '' + (window.uiTests && (typeof uiTests.runTest)));",
          &result));
  ASSERT_EQ("function", result) << "DevTools front-end is broken.";
  CloseDevToolsWindow();
}

#if defined(OS_MACOSX)
#define MAYBE_InspectSharedWorker DISABLED_InspectSharedWorker
#else
#define MAYBE_InspectSharedWorker InspectSharedWorker
#endif
// Flakily fails with 25s timeout: http://crbug.com/89845
IN_PROC_BROWSER_TEST_F(WorkerDevToolsSanityTest, MAYBE_InspectSharedWorker) {
#if defined(OS_WIN) && defined(USE_ASH)
  // Disable this test in Metro+Ash for now (http://crbug.com/262796).
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kAshBrowserTests))
    return;
#endif

  RunTest("testSharedWorker", kSharedWorkerTestPage);
}

// http://crbug.com/100538
#if defined(OS_MACOSX) || defined(OS_WIN)
#define MAYBE_PauseInSharedWorkerInitialization DISABLED_PauseInSharedWorkerInitialization
#else
#define MAYBE_PauseInSharedWorkerInitialization PauseInSharedWorkerInitialization
#endif

// http://crbug.com/106114 is masking
// MAYBE_PauseInSharedWorkerInitialization into
// DISABLED_PauseInSharedWorkerInitialization
IN_PROC_BROWSER_TEST_F(WorkerDevToolsSanityTest,
                       MAYBE_PauseInSharedWorkerInitialization) {
  ASSERT_TRUE(test_server()->Start());
  GURL url = test_server()->GetURL(kReloadSharedWorkerTestPage);
  ui_test_utils::NavigateToURL(browser(), url);

  scoped_refptr<WorkerData> worker_data = WaitForFirstSharedWorker();
  OpenDevToolsWindowForSharedWorker(worker_data.get());

  TerminateWorker(worker_data);

  // Reload page to restart the worker.
  ui_test_utils::NavigateToURL(browser(), url);

  // Wait until worker script is paused on the debugger statement.
  RunTestFunction(window_, "testPauseInSharedWorkerInitialization");
  CloseDevToolsWindow();
}

class DevToolsAgentHostTest : public InProcessBrowserTest {};

// Tests DevToolsAgentHost retention by its target.
IN_PROC_BROWSER_TEST_F(DevToolsAgentHostTest, TestAgentHostReleased) {
  ui_test_utils::NavigateToURL(browser(), GURL("about:blank"));
  RenderViewHost* rvh = browser()->tab_strip_model()->GetWebContentsAt(0)->
      GetRenderViewHost();
  DevToolsAgentHost* agent_raw = DevToolsAgentHost::GetOrCreateFor(rvh).get();
  const std::string agent_id = agent_raw->GetId();
  ASSERT_EQ(agent_raw, DevToolsAgentHost::GetForId(agent_id)) <<
      "DevToolsAgentHost cannot be found by id";
  browser()->tab_strip_model()->
      CloseWebContentsAt(0, TabStripModel::CLOSE_NONE);
  ASSERT_FALSE(DevToolsAgentHost::GetForId(agent_id).get())
      << "DevToolsAgentHost is not released when the tab is closed";
}

class RemoteDebuggingTest: public ExtensionApiTest {
  virtual void SetUpCommandLine(CommandLine* command_line) OVERRIDE {
    ExtensionApiTest::SetUpCommandLine(command_line);
    command_line->AppendSwitchASCII(switches::kRemoteDebuggingPort, "9222");

    // Override the extension root path.
    PathService::Get(chrome::DIR_TEST_DATA, &test_data_dir_);
    test_data_dir_ = test_data_dir_.AppendASCII("devtools");
  }
};

IN_PROC_BROWSER_TEST_F(RemoteDebuggingTest, RemoteDebugger) {
#if defined(OS_WIN) && defined(USE_ASH)
  // Disable this test in Metro+Ash for now (http://crbug.com/262796).
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kAshBrowserTests))
    return;
#endif

  ASSERT_TRUE(RunExtensionTest("target_list")) << message_;
}

}  // namespace
