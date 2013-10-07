// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <list>
#include <set>

#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_browser_main.h"
#include "chrome/browser/chrome_browser_main_extra_parts.h"
#include "chrome/browser/chrome_content_browser_client.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/api/web_navigation/web_navigation_api.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/renderer_host/chrome_resource_dispatcher_host_delegate.h"
#include "chrome/browser/tab_contents/render_view_context_menu.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/resource_controller.h"
#include "content/public/browser/resource_dispatcher_host.h"
#include "content/public/browser/resource_throttle.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/context_menu_params.h"
#include "content/public/common/url_constants.h"
#include "content/public/test/browser_test_utils.h"
#include "extensions/common/switches.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "third_party/WebKit/public/web/WebContextMenuData.h"
#include "third_party/WebKit/public/web/WebInputEvent.h"
#include "webkit/common/resource_type.h"

using content::WebContents;

namespace extensions {

namespace {

// An UI-less RenderViewContextMenu.
class TestRenderViewContextMenu : public RenderViewContextMenu {
 public:
  TestRenderViewContextMenu(WebContents* web_contents,
                            const content::ContextMenuParams& params)
      : RenderViewContextMenu(web_contents, params) {
  }
  virtual ~TestRenderViewContextMenu() {}

 private:
  virtual void PlatformInit() OVERRIDE {}
  virtual void PlatformCancel() OVERRIDE {}
  virtual bool GetAcceleratorForCommandId(int, ui::Accelerator*) OVERRIDE {
    return false;
  }

  DISALLOW_COPY_AND_ASSIGN(TestRenderViewContextMenu);
};


// This class can defer requests for arbitrary URLs.
class TestNavigationListener
    : public base::RefCountedThreadSafe<TestNavigationListener> {
 public:
  TestNavigationListener() {}

  // Add |url| to the set of URLs we should delay.
  void DelayRequestsForURL(const GURL& url) {
    if (!content::BrowserThread::CurrentlyOn(content::BrowserThread::IO)) {
      content::BrowserThread::PostTask(
          content::BrowserThread::IO,
          FROM_HERE,
          base::Bind(&TestNavigationListener::DelayRequestsForURL, this, url));
      return;
    }
    urls_to_delay_.insert(url);
  }

  // Resume all deferred requests.
  void ResumeAll() {
    if (!content::BrowserThread::CurrentlyOn(content::BrowserThread::IO)) {
      content::BrowserThread::PostTask(
          content::BrowserThread::IO,
          FROM_HERE,
          base::Bind(&TestNavigationListener::ResumeAll, this));
      return;
    }
    WeakThrottleList::const_iterator it;
    for (it = throttles_.begin(); it != throttles_.end(); ++it) {
      if (it->get())
        (*it)->Resume();
    }
    throttles_.clear();
  }

  // Constructs a ResourceThrottle if the request for |url| should be held.
  //
  // Needs to be invoked on the IO thread.
  content::ResourceThrottle* CreateResourceThrottle(
      const GURL& url,
      ResourceType::Type resource_type) {
    DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::IO));
    if (urls_to_delay_.find(url) == urls_to_delay_.end())
      return NULL;

    Throttle* throttle = new Throttle();
    throttles_.push_back(throttle->AsWeakPtr());
    return throttle;
  }

 private:
  friend class base::RefCountedThreadSafe<TestNavigationListener>;

  virtual ~TestNavigationListener() {}

  // Stores a throttle per URL request that we have delayed.
  class Throttle : public content::ResourceThrottle,
                   public base::SupportsWeakPtr<Throttle> {
   public:
    void Resume() {
      controller()->Resume();
    }

    // content::ResourceThrottle implementation.
    virtual void WillStartRequest(bool* defer) OVERRIDE {
      *defer = true;
    }
  };
  typedef base::WeakPtr<Throttle> WeakThrottle;
  typedef std::list<WeakThrottle> WeakThrottleList;
  WeakThrottleList throttles_;

  // The set of URLs to be delayed.
  std::set<GURL> urls_to_delay_;

  DISALLOW_COPY_AND_ASSIGN(TestNavigationListener);
};

// Waits for a WC to be created. Once it starts loading |delay_url| (after at
// least the first navigation has committed), it delays the load, executes
// |script| in the last committed RVH and resumes the load when |until_url|
// commits. This class expects |script| to trigger the load of |until_url|.
class DelayLoadStartAndExecuteJavascript
    : public content::NotificationObserver,
      public content::WebContentsObserver {
 public:
  DelayLoadStartAndExecuteJavascript(
      TestNavigationListener* test_navigation_listener,
      const GURL& delay_url,
      const std::string& script,
      const GURL& until_url)
      : content::WebContentsObserver(),
        test_navigation_listener_(test_navigation_listener),
        delay_url_(delay_url),
        until_url_(until_url),
        script_(script),
        script_was_executed_(false),
        rvh_(NULL) {
    registrar_.Add(this,
                   chrome::NOTIFICATION_TAB_ADDED,
                   content::NotificationService::AllSources());
    test_navigation_listener_->DelayRequestsForURL(delay_url_);
  }
  virtual ~DelayLoadStartAndExecuteJavascript() {}

  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE {
    if (type != chrome::NOTIFICATION_TAB_ADDED) {
      NOTREACHED();
      return;
    }
    content::WebContentsObserver::Observe(
        content::Details<content::WebContents>(details).ptr());
    registrar_.RemoveAll();
  }

  virtual void DidStartProvisionalLoadForFrame(
      int64 frame_id,
      int64 parent_frame_id,
      bool is_main_frame,
      const GURL& validated_url,
      bool is_error_page,
      bool is_iframe_srcdoc,
      content::RenderViewHost* render_view_host) OVERRIDE {
    if (validated_url != delay_url_ || !rvh_)
      return;

    rvh_->ExecuteJavascriptInWebFrame(string16(), UTF8ToUTF16(script_));
    script_was_executed_ = true;
  }

  virtual void DidCommitProvisionalLoadForFrame(
      int64 frame_id,
      bool is_main_frame,
      const GURL& url,
      content::PageTransition transition_type,
      content::RenderViewHost* render_view_host) OVERRIDE {
    if (script_was_executed_ && url == until_url_) {
      content::WebContentsObserver::Observe(NULL);
      test_navigation_listener_->ResumeAll();
    }
    rvh_ = render_view_host;
  }

 private:
  content::NotificationRegistrar registrar_;

  scoped_refptr<TestNavigationListener> test_navigation_listener_;

  GURL delay_url_;
  GURL until_url_;
  std::string script_;
  bool script_was_executed_;
  content::RenderViewHost* rvh_;

  DISALLOW_COPY_AND_ASSIGN(DelayLoadStartAndExecuteJavascript);
};

// A ResourceDispatcherHostDelegate that adds a TestNavigationObserver.
class TestResourceDispatcherHostDelegate
    : public ChromeResourceDispatcherHostDelegate {
 public:
  TestResourceDispatcherHostDelegate(
      prerender::PrerenderTracker* prerender_tracker,
      TestNavigationListener* test_navigation_listener)
      : ChromeResourceDispatcherHostDelegate(prerender_tracker),
        test_navigation_listener_(test_navigation_listener) {
  }
  virtual ~TestResourceDispatcherHostDelegate() {}

  virtual void RequestBeginning(
      net::URLRequest* request,
      content::ResourceContext* resource_context,
      appcache::AppCacheService* appcache_service,
      ResourceType::Type resource_type,
      int child_id,
      int route_id,
      bool is_continuation_of_transferred_request,
      ScopedVector<content::ResourceThrottle>* throttles) OVERRIDE {
    ChromeResourceDispatcherHostDelegate::RequestBeginning(
        request,
        resource_context,
        appcache_service,
        resource_type,
        child_id,
        route_id,
        is_continuation_of_transferred_request,
        throttles);
    content::ResourceThrottle* throttle =
        test_navigation_listener_->CreateResourceThrottle(request->url(),
                                                          resource_type);
    if (throttle)
      throttles->push_back(throttle);
  }

 private:
  scoped_refptr<TestNavigationListener> test_navigation_listener_;

  DISALLOW_COPY_AND_ASSIGN(TestResourceDispatcherHostDelegate);
};

// Used to manage the lifetime of the TestResourceDispatcherHostDelegate which
// needs to be deleted before the threads are stopped.
class TestBrowserMainExtraParts : public ChromeBrowserMainExtraParts {
 public:
  explicit TestBrowserMainExtraParts(
      TestNavigationListener* test_navigation_listener)
      : test_navigation_listener_(test_navigation_listener) {
  }
  virtual ~TestBrowserMainExtraParts() {}

  TestResourceDispatcherHostDelegate* resource_dispatcher_host_delegate() {
    if (!resource_dispatcher_host_delegate_.get()) {
      resource_dispatcher_host_delegate_.reset(
          new TestResourceDispatcherHostDelegate(
              g_browser_process->prerender_tracker(),
              test_navigation_listener_.get()));
    }
    return resource_dispatcher_host_delegate_.get();
  }

  // ChromeBrowserMainExtraParts implementation.
  virtual void PostMainMessageLoopRun() OVERRIDE {
    resource_dispatcher_host_delegate_.reset();
  }

 private:
  scoped_refptr<TestNavigationListener> test_navigation_listener_;
  scoped_ptr<TestResourceDispatcherHostDelegate>
      resource_dispatcher_host_delegate_;

  DISALLOW_COPY_AND_ASSIGN(TestBrowserMainExtraParts);
};

// A ContentBrowserClient that doesn't forward the RDH created signal.
class TestContentBrowserClient : public chrome::ChromeContentBrowserClient {
 public:
  explicit TestContentBrowserClient(
      TestNavigationListener* test_navigation_listener)
      : test_navigation_listener_(test_navigation_listener) {
  }
  virtual ~TestContentBrowserClient() {}

  virtual void ResourceDispatcherHostCreated() OVERRIDE {
    // Don't invoke ChromeContentBrowserClient::ResourceDispatcherHostCreated.
    // It would notify BrowserProcessImpl which would create a
    // ChromeResourceDispatcherHostDelegate and other objects. Not creating
    // other objects might turn out to be a problem in the future.
    content::ResourceDispatcherHost::Get()->SetDelegate(
        browser_main_extra_parts_->resource_dispatcher_host_delegate());
  }

  virtual content::BrowserMainParts* CreateBrowserMainParts(
      const content::MainFunctionParams& parameters) OVERRIDE {
    ChromeBrowserMainParts* main_parts = static_cast<ChromeBrowserMainParts*>(
        ChromeContentBrowserClient::CreateBrowserMainParts(parameters));

    browser_main_extra_parts_ =
        new TestBrowserMainExtraParts(test_navigation_listener_.get());
    main_parts->AddParts(browser_main_extra_parts_);
    return main_parts;
  }

 private:
  scoped_refptr<TestNavigationListener> test_navigation_listener_;
  TestBrowserMainExtraParts* browser_main_extra_parts_;

  DISALLOW_COPY_AND_ASSIGN(TestContentBrowserClient);
};

}  // namespace

class WebNavigationApiTest : public ExtensionApiTest {
 public:
  WebNavigationApiTest() {}
  virtual ~WebNavigationApiTest() {}

  virtual void SetUpInProcessBrowserTestFixture() OVERRIDE {
    ExtensionApiTest::SetUpInProcessBrowserTestFixture();

    test_navigation_listener_ = new TestNavigationListener();
    content_browser_client_.reset(
        new TestContentBrowserClient(test_navigation_listener_.get()));
    original_content_browser_client_ = content::SetBrowserClientForTesting(
        content_browser_client_.get());

    FrameNavigationState::set_allow_extension_scheme(true);

    CommandLine::ForCurrentProcess()->AppendSwitch(
        switches::kAllowLegacyExtensionManifests);

    host_resolver()->AddRule("*", "127.0.0.1");
  }

  virtual void TearDownInProcessBrowserTestFixture() OVERRIDE {
    ExtensionApiTest::TearDownInProcessBrowserTestFixture();
    content::SetBrowserClientForTesting(original_content_browser_client_);
  }

  TestNavigationListener* test_navigation_listener() {
    return test_navigation_listener_.get();
  }

 private:
  scoped_refptr<TestNavigationListener> test_navigation_listener_;
  scoped_ptr<TestContentBrowserClient> content_browser_client_;
  content::ContentBrowserClient* original_content_browser_client_;

  DISALLOW_COPY_AND_ASSIGN(WebNavigationApiTest);
};

// Fails often on Windows dbg bots. http://crbug.com/177163
#if defined(OS_WIN)
#define MAYBE_Api DISABLED_Api
#else
#define MAYBE_Api Api
#endif  // defined(OS_WIN)
IN_PROC_BROWSER_TEST_F(WebNavigationApiTest, MAYBE_Api) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(
      RunExtensionSubtest("webnavigation", "test_api.html")) << message_;
}

// Fails often on Windows dbg bots. http://crbug.com/177163
#if defined(OS_WIN)
#define MAYBE_GetFrame DISABLED_GetFrame
#else
#define MAYBE_GetFrame GetFrame
#endif  // defined(OS_WIN)
IN_PROC_BROWSER_TEST_F(WebNavigationApiTest, MAYBE_GetFrame) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(
      RunExtensionSubtest("webnavigation", "test_getFrame.html")) << message_;
}

IN_PROC_BROWSER_TEST_F(WebNavigationApiTest, ClientRedirect) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(
      RunExtensionSubtest("webnavigation", "test_clientRedirect.html"))
          << message_;
}

IN_PROC_BROWSER_TEST_F(WebNavigationApiTest, ServerRedirect) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(
      RunExtensionSubtest("webnavigation", "test_serverRedirect.html"))
          << message_;
}

// http://crbug.com/235171 and http://crbug.com/177163
#if (defined(OS_WIN) && !defined(NDEBUG)) || \
    (defined(OS_CHROMEOS) || (defined(OS_LINUX) && defined(USE_AURA)))
#define MAYBE_ServerRedirectSingleProcess DISABLED_ServerRedirectSingleProcess
#else
#define MAYBE_ServerRedirectSingleProcess ServerRedirectSingleProcess
#endif
IN_PROC_BROWSER_TEST_F(WebNavigationApiTest,
                       MAYBE_ServerRedirectSingleProcess) {
  ASSERT_TRUE(StartEmbeddedTestServer());

  // Set max renderers to 1 to force running out of processes.
  content::RenderProcessHost::SetMaxRendererProcessCount(1);

  // Wait for the extension to set itself up and return control to us.
  ASSERT_TRUE(RunExtensionSubtest(
      "webnavigation", "test_serverRedirectSingleProcess.html"))
          << message_;

  WebContents* tab = browser()->tab_strip_model()->GetActiveWebContents();
  content::WaitForLoadStop(tab);

  ResultCatcher catcher;
  GURL url(base::StringPrintf(
      "http://www.a.com:%d/"
          "extensions/api_test/webnavigation/serverRedirect/a.html",
      embedded_test_server()->port()));

  ui_test_utils::NavigateToURL(browser(), url);

  url = GURL(base::StringPrintf(
      "http://www.b.com:%d/server-redirect?http://www.b.com:%d/",
      embedded_test_server()->port(),
      embedded_test_server()->port()));

  ui_test_utils::NavigateToURL(browser(), url);

  ASSERT_TRUE(catcher.GetNextResult()) << catcher.message();
}

IN_PROC_BROWSER_TEST_F(WebNavigationApiTest, ForwardBack) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(
      RunExtensionSubtest("webnavigation", "test_forwardBack.html"))
          << message_;
}

IN_PROC_BROWSER_TEST_F(WebNavigationApiTest, IFrame) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(
      RunExtensionSubtest("webnavigation", "test_iframe.html")) << message_;
}

IN_PROC_BROWSER_TEST_F(WebNavigationApiTest, SrcDoc) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(
      RunExtensionSubtest("webnavigation", "test_srcdoc.html")) << message_;
}

IN_PROC_BROWSER_TEST_F(WebNavigationApiTest, OpenTab) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(
      RunExtensionSubtest("webnavigation", "test_openTab.html")) << message_;
}

IN_PROC_BROWSER_TEST_F(WebNavigationApiTest, ReferenceFragment) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(
      RunExtensionSubtest("webnavigation", "test_referenceFragment.html"))
          << message_;
}

IN_PROC_BROWSER_TEST_F(WebNavigationApiTest, SimpleLoad) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(
      RunExtensionSubtest("webnavigation", "test_simpleLoad.html")) << message_;
}

// Fails often on Windows dbg bots. http://crbug.com/177163
#if defined(OS_WIN)
#define MAYBE_Failures DISABLED_Failures
#else
#define MAYBE_Failures Failures
#endif  // defined(OS_WIN)
IN_PROC_BROWSER_TEST_F(WebNavigationApiTest, MAYBE_Failures) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(
      RunExtensionSubtest("webnavigation", "test_failures.html")) << message_;
}

IN_PROC_BROWSER_TEST_F(WebNavigationApiTest, FilteredTest) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(
      RunExtensionSubtest("webnavigation", "test_filtered.html")) << message_;
}

// Fails often on Windows dbg bots. http://crbug.com/177163
#if defined(OS_WIN)
#define MAYBE_UserAction DISABLED_UserAction
#else
#define MAYBE_UserAction UserAction
#endif  // defined(OS_WIN)
IN_PROC_BROWSER_TEST_F(WebNavigationApiTest, MAYBE_UserAction) {
  ASSERT_TRUE(StartEmbeddedTestServer());

  // Wait for the extension to set itself up and return control to us.
  ASSERT_TRUE(
      RunExtensionSubtest("webnavigation", "test_userAction.html")) << message_;

  WebContents* tab = browser()->tab_strip_model()->GetActiveWebContents();
  content::WaitForLoadStop(tab);

  ResultCatcher catcher;

  ExtensionService* service = extensions::ExtensionSystem::Get(
      browser()->profile())->extension_service();
  const extensions::Extension* extension =
      service->GetExtensionById(last_loaded_extension_id_, false);
  GURL url = extension->GetResourceURL("userAction/a.html");

  ui_test_utils::NavigateToURL(browser(), url);

  // This corresponds to "Open link in new tab".
  content::ContextMenuParams params;
  params.is_editable = false;
  params.media_type = WebKit::WebContextMenuData::MediaTypeNone;
  params.page_url = url;
  params.frame_id = WebNavigationTabObserver::Get(tab)->
      frame_navigation_state().GetMainFrameID().frame_num;
  params.link_url = extension->GetResourceURL("userAction/b.html");

  TestRenderViewContextMenu menu(tab, params);
  menu.Init();
  menu.ExecuteCommand(IDC_CONTENT_CONTEXT_OPENLINKNEWTAB, 0);

  ASSERT_TRUE(catcher.GetNextResult()) << catcher.message();
}

// http://crbug.com/177163
#if defined(OS_WIN) && !defined(NDEBUG)
#define MAYBE_RequestOpenTab DISABLED_RequestOpenTab
#else
#define MAYBE_RequestOpenTab RequestOpenTab
#endif
IN_PROC_BROWSER_TEST_F(WebNavigationApiTest, MAYBE_RequestOpenTab) {
  ASSERT_TRUE(StartEmbeddedTestServer());

  // Wait for the extension to set itself up and return control to us.
  ASSERT_TRUE(RunExtensionSubtest("webnavigation", "test_requestOpenTab.html"))
      << message_;

  WebContents* tab = browser()->tab_strip_model()->GetActiveWebContents();
  content::WaitForLoadStop(tab);

  ResultCatcher catcher;

  ExtensionService* service = extensions::ExtensionSystem::Get(
      browser()->profile())->extension_service();
  const extensions::Extension* extension =
      service->GetExtensionById(last_loaded_extension_id_, false);
  GURL url = extension->GetResourceURL("requestOpenTab/a.html");

  ui_test_utils::NavigateToURL(browser(), url);

  // There's a link on a.html. Middle-click on it to open it in a new tab.
  WebKit::WebMouseEvent mouse_event;
  mouse_event.type = WebKit::WebInputEvent::MouseDown;
  mouse_event.button = WebKit::WebMouseEvent::ButtonMiddle;
  mouse_event.x = 7;
  mouse_event.y = 7;
  mouse_event.clickCount = 1;
  tab->GetRenderViewHost()->ForwardMouseEvent(mouse_event);
  mouse_event.type = WebKit::WebInputEvent::MouseUp;
  tab->GetRenderViewHost()->ForwardMouseEvent(mouse_event);

  ASSERT_TRUE(catcher.GetNextResult()) << catcher.message();
}

// Fails often on Windows dbg bots. http://crbug.com/177163
#if defined(OS_WIN)
#define MAYBE_TargetBlank DISABLED_TargetBlank
#else
#define MAYBE_TargetBlank TargetBlank
#endif  // defined(OS_WIN)
IN_PROC_BROWSER_TEST_F(WebNavigationApiTest, MAYBE_TargetBlank) {
  ASSERT_TRUE(StartEmbeddedTestServer());

  // Wait for the extension to set itself up and return control to us.
  ASSERT_TRUE(RunExtensionSubtest("webnavigation", "test_targetBlank.html"))
      << message_;

  WebContents* tab = browser()->tab_strip_model()->GetActiveWebContents();
  content::WaitForLoadStop(tab);

  ResultCatcher catcher;

  GURL url = embedded_test_server()->GetURL(
      "/extensions/api_test/webnavigation/targetBlank/a.html");

  chrome::NavigateParams params(browser(), url, content::PAGE_TRANSITION_LINK);
  ui_test_utils::NavigateToURL(&params);

  // There's a link with target=_blank on a.html. Click on it to open it in a
  // new tab.
  WebKit::WebMouseEvent mouse_event;
  mouse_event.type = WebKit::WebInputEvent::MouseDown;
  mouse_event.button = WebKit::WebMouseEvent::ButtonLeft;
  mouse_event.x = 7;
  mouse_event.y = 7;
  mouse_event.clickCount = 1;
  tab->GetRenderViewHost()->ForwardMouseEvent(mouse_event);
  mouse_event.type = WebKit::WebInputEvent::MouseUp;
  tab->GetRenderViewHost()->ForwardMouseEvent(mouse_event);

  ASSERT_TRUE(catcher.GetNextResult()) << catcher.message();
}

// http://crbug.com/177163
#if defined(OS_WIN) && !defined(NDEBUG)
#define MAYBE_TargetBlankIncognito DISABLED_TargetBlankIncognito
#else
#define MAYBE_TargetBlankIncognito TargetBlankIncognito
#endif
IN_PROC_BROWSER_TEST_F(WebNavigationApiTest, MAYBE_TargetBlankIncognito) {
  ASSERT_TRUE(StartEmbeddedTestServer());

  // Wait for the extension to set itself up and return control to us.
  ASSERT_TRUE(RunExtensionSubtest(
      "webnavigation", "test_targetBlank.html",
      ExtensionApiTest::kFlagEnableIncognito)) << message_;

  ResultCatcher catcher;

  GURL url = embedded_test_server()->GetURL(
      "/extensions/api_test/webnavigation/targetBlank/a.html");

  Browser* otr_browser = ui_test_utils::OpenURLOffTheRecord(
      browser()->profile(), url);
  WebContents* tab = otr_browser->tab_strip_model()->GetActiveWebContents();

  // There's a link with target=_blank on a.html. Click on it to open it in a
  // new tab.
  WebKit::WebMouseEvent mouse_event;
  mouse_event.type = WebKit::WebInputEvent::MouseDown;
  mouse_event.button = WebKit::WebMouseEvent::ButtonLeft;
  mouse_event.x = 7;
  mouse_event.y = 7;
  mouse_event.clickCount = 1;
  tab->GetRenderViewHost()->ForwardMouseEvent(mouse_event);
  mouse_event.type = WebKit::WebInputEvent::MouseUp;
  tab->GetRenderViewHost()->ForwardMouseEvent(mouse_event);

  ASSERT_TRUE(catcher.GetNextResult()) << catcher.message();
}

IN_PROC_BROWSER_TEST_F(WebNavigationApiTest, History) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(
      RunExtensionSubtest("webnavigation", "test_history.html"))
          << message_;
}

IN_PROC_BROWSER_TEST_F(WebNavigationApiTest, CrossProcess) {
  ASSERT_TRUE(StartEmbeddedTestServer());

  LoadExtension(test_data_dir_.AppendASCII("webnavigation").AppendASCII("app"));
  LoadExtension(test_data_dir_.AppendASCII("webnavigation"));

  ExtensionService* service = extensions::ExtensionSystem::Get(
      browser()->profile())->extension_service();
  const extensions::Extension* extension =
      service->GetExtensionById(last_loaded_extension_id_, false);

  // See crossProcess/d.html.
  DelayLoadStartAndExecuteJavascript call_script(
      test_navigation_listener(),
      embedded_test_server()->GetURL("/test1"),
      "navigate2()",
      extension->GetResourceURL("crossProcess/empty.html"));

  ASSERT_TRUE(RunPageTest(
      extension->GetResourceURL("test_crossProcess.html").spec()))
          << message_;
}

IN_PROC_BROWSER_TEST_F(WebNavigationApiTest, CrossProcessFragment) {
  ASSERT_TRUE(StartEmbeddedTestServer());

  LoadExtension(test_data_dir_.AppendASCII("webnavigation"));

  ExtensionService* service = extensions::ExtensionSystem::Get(
      browser()->profile())->extension_service();
  const extensions::Extension* extension =
      service->GetExtensionById(last_loaded_extension_id_, false);

  // See crossProcess/f.html.
  DelayLoadStartAndExecuteJavascript call_script3(
      test_navigation_listener(),
      embedded_test_server()->GetURL("/test3"),
      "updateFragment()",
      extension->GetResourceURL(base::StringPrintf(
          "crossProcess/f.html?%d#foo",
          embedded_test_server()->port())));

  // See crossProcess/g.html.
  DelayLoadStartAndExecuteJavascript call_script4(
      test_navigation_listener(),
      embedded_test_server()->GetURL("/test4"),
      "updateFragment()",
      extension->GetResourceURL(base::StringPrintf(
          "crossProcess/g.html?%d#foo",
          embedded_test_server()->port())));

  ASSERT_TRUE(RunPageTest(
      extension->GetResourceURL("test_crossProcessFragment.html").spec()))
          << message_;
}

IN_PROC_BROWSER_TEST_F(WebNavigationApiTest, CrossProcessHistory) {
  ASSERT_TRUE(StartEmbeddedTestServer());

  LoadExtension(test_data_dir_.AppendASCII("webnavigation"));

  ExtensionService* service = extensions::ExtensionSystem::Get(
      browser()->profile())->extension_service();
  const extensions::Extension* extension =
      service->GetExtensionById(last_loaded_extension_id_, false);

  // See crossProcess/e.html.
  DelayLoadStartAndExecuteJavascript call_script2(
      test_navigation_listener(),
      embedded_test_server()->GetURL("/test2"),
      "updateHistory()",
      extension->GetResourceURL("crossProcess/empty.html"));

  // See crossProcess/h.html.
  DelayLoadStartAndExecuteJavascript call_script5(
      test_navigation_listener(),
      embedded_test_server()->GetURL("/test5"),
      "updateHistory()",
      extension->GetResourceURL("crossProcess/empty.html"));

  // See crossProcess/i.html.
  DelayLoadStartAndExecuteJavascript call_script6(
      test_navigation_listener(),
      embedded_test_server()->GetURL("/test6"),
      "updateHistory()",
      extension->GetResourceURL("crossProcess/empty.html"));

  ASSERT_TRUE(RunPageTest(
      extension->GetResourceURL("test_crossProcessHistory.html").spec()))
          << message_;
}

// http://crbug.com/177163
#if defined(OS_WIN) && !defined(NDEBUG)
#define MAYBE_Crash DISABLED_Crash
#else
#define MAYBE_Crash Crash
#endif
IN_PROC_BROWSER_TEST_F(WebNavigationApiTest, MAYBE_Crash) {
  ASSERT_TRUE(StartEmbeddedTestServer());

  // Wait for the extension to set itself up and return control to us.
  ASSERT_TRUE(RunExtensionSubtest("webnavigation", "test_crash.html"))
      << message_;

  WebContents* tab = browser()->tab_strip_model()->GetActiveWebContents();
  content::WaitForLoadStop(tab);

  ResultCatcher catcher;

  GURL url(base::StringPrintf(
      "http://www.a.com:%d/"
          "extensions/api_test/webnavigation/crash/a.html",
      embedded_test_server()->port()));
  ui_test_utils::NavigateToURL(browser(), url);

  ui_test_utils::NavigateToURL(browser(), GURL(content::kChromeUICrashURL));

  url = GURL(base::StringPrintf(
      "http://www.a.com:%d/"
          "extensions/api_test/webnavigation/crash/b.html",
      embedded_test_server()->port()));
  ui_test_utils::NavigateToURL(browser(), url);

  ASSERT_TRUE(catcher.GetNextResult()) << catcher.message();
}

}  // namespace extensions
