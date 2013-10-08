// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <deque>
#include <vector>

#include "base/command_line.h"
#include "base/path_service.h"
#include "base/prefs/pref_service.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/test_timeouts.h"
#include "base/values.h"
#include "chrome/browser/browsing_data/browsing_data_helper.h"
#include "chrome/browser/browsing_data/browsing_data_remover.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/content_settings/host_content_settings_map.h"
#include "chrome/browser/extensions/api/web_navigation/web_navigation_api.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/favicon/favicon_tab_helper.h"
#include "chrome/browser/prerender/prerender_contents.h"
#include "chrome/browser/prerender/prerender_handle.h"
#include "chrome/browser/prerender/prerender_link_manager.h"
#include "chrome/browser/prerender/prerender_link_manager_factory.h"
#include "chrome/browser/prerender/prerender_manager.h"
#include "chrome/browser/prerender/prerender_manager_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/safe_browsing/database_manager.h"
#include "chrome/browser/safe_browsing/safe_browsing_service.h"
#include "chrome/browser/safe_browsing/safe_browsing_util.h"
#include "chrome/browser/task_manager/task_manager.h"
#include "chrome/browser/task_manager/task_manager_browsertest_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/test_switches.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/browser_message_filter.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/devtools_client_host.h"
#include "content/public/browser/devtools_manager.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "content/public/test/test_utils.h"
#include "extensions/common/switches.h"
#include "grit/generated_resources.h"
#include "net/dns/mock_host_resolver.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_getter.h"
#include "net/url_request/url_request_filter.h"
#include "net/url_request/url_request_job.h"
#include "ui/base/l10n/l10n_util.h"
#include "url/gurl.h"

using content::BrowserThread;
using content::DevToolsAgentHost;
using content::DevToolsClientHost;
using content::DevToolsManager;
using content::NavigationController;
using content::OpenURLParams;
using content::Referrer;
using content::RenderViewHost;
using content::RenderWidgetHost;
using content::WebContents;

// Prerender tests work as follows:
//
// A page with a prefetch link to the test page is loaded.  Once prerendered,
// its Javascript function DidPrerenderPass() is called, which returns true if
// the page behaves as expected when prerendered.
//
// The prerendered page is then displayed on a tab.  The Javascript function
// DidDisplayPass() is called, and returns true if the page behaved as it
// should while being displayed.

namespace prerender {

namespace {

// Constants used in the test HTML files.
static const char* kReadyTitle = "READY";
static const char* kPassTitle = "PASS";

std::string CreateClientRedirect(const std::string& dest_url) {
  const char* const kClientRedirectBase = "client-redirect?";
  return kClientRedirectBase + dest_url;
}

std::string CreateServerRedirect(const std::string& dest_url) {
  const char* const kServerRedirectBase = "server-redirect?";
  return kServerRedirectBase + dest_url;
}

// Clears the specified data using BrowsingDataRemover.
void ClearBrowsingData(Browser* browser, int remove_mask) {
  BrowsingDataRemover* remover =
      BrowsingDataRemover::CreateForUnboundedRange(browser->profile());
  remover->Remove(remove_mask, BrowsingDataHelper::UNPROTECTED_WEB);
  // BrowsingDataRemover deletes itself.
}

void CancelAllPrerenders(PrerenderManager* prerender_manager) {
  prerender_manager->CancelAllPrerenders();
}

// Returns true if and only if the final status is one in which the prerendered
// page should prerender correctly. The page still may not be used.
bool ShouldRenderPrerenderedPageCorrectly(FinalStatus status) {
  switch (status) {
    case FINAL_STATUS_USED:
    case FINAL_STATUS_WINDOW_OPENER:
    case FINAL_STATUS_APP_TERMINATING:
    case FINAL_STATUS_CACHE_OR_HISTORY_CLEARED:
    // We'll crash the renderer after it's loaded.
    case FINAL_STATUS_RENDERER_CRASHED:
    case FINAL_STATUS_CANCELLED:
    case FINAL_STATUS_DEVTOOLS_ATTACHED:
    case FINAL_STATUS_PAGE_BEING_CAPTURED:
      return true;
    default:
      return false;
  }
}

// Waits for the destruction of a RenderProcessHost's IPC channel.
// Used to make sure the PrerenderLinkManager's OnChannelClosed function has
// been called, before checking its state.
class ChannelDestructionWatcher {
 public:
  ChannelDestructionWatcher() : channel_destroyed_(false),
                                waiting_for_channel_destruction_(false) {
  }

  ~ChannelDestructionWatcher() {
  }

  void WatchChannel(content::RenderProcessHost* host) {
    host->GetChannel()->AddFilter(new DestructionMessageFilter(this));
  }

  void WaitForChannelClose() {
    ASSERT_FALSE(waiting_for_channel_destruction_);

    if (channel_destroyed_)
      return;
    waiting_for_channel_destruction_ = true;
    content::RunMessageLoop();

    EXPECT_FALSE(waiting_for_channel_destruction_);
    EXPECT_TRUE(channel_destroyed_);
  }

 private:
  // When destroyed, calls ChannelDestructionWatcher::OnChannelDestroyed.
  // Ignores all messages.
  class DestructionMessageFilter : public content::BrowserMessageFilter {
   public:
     explicit DestructionMessageFilter(ChannelDestructionWatcher* watcher)
         : watcher_(watcher) {
    }

   private:
    virtual ~DestructionMessageFilter() {
      content::BrowserThread::PostTask(
          content::BrowserThread::UI, FROM_HERE,
          base::Bind(&ChannelDestructionWatcher::OnChannelDestroyed,
                     base::Unretained(watcher_)));
    }

    virtual bool OnMessageReceived(const IPC::Message& message,
                                   bool* message_was_ok) OVERRIDE {
      return false;
    }

    ChannelDestructionWatcher* watcher_;

    DISALLOW_COPY_AND_ASSIGN(DestructionMessageFilter);
  };

  void OnChannelDestroyed() {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

    EXPECT_FALSE(channel_destroyed_);
    channel_destroyed_ = true;
    if (waiting_for_channel_destruction_) {
      waiting_for_channel_destruction_ = false;
      base::MessageLoop::current()->Quit();
    }
  }

  bool channel_destroyed_;
  bool waiting_for_channel_destruction_;

  DISALLOW_COPY_AND_ASSIGN(ChannelDestructionWatcher);
};

// PrerenderContents that stops the UI message loop on DidStopLoading().
class TestPrerenderContents : public PrerenderContents {
 public:
  TestPrerenderContents(
      PrerenderManager* prerender_manager,
      Profile* profile,
      const GURL& url,
      const content::Referrer& referrer,
      Origin origin,
      int expected_number_of_loads,
      FinalStatus expected_final_status,
      bool prerender_should_wait_for_ready_title)
      : PrerenderContents(prerender_manager, profile, url,
                          referrer, origin, PrerenderManager::kNoExperiment),
        number_of_loads_(0),
        expected_number_of_loads_(expected_number_of_loads),
        expected_final_status_(expected_final_status),
        new_render_view_host_(NULL),
        was_hidden_(false),
        was_shown_(false),
        should_be_shown_(expected_final_status == FINAL_STATUS_USED),
        quit_message_loop_on_destruction_(
            expected_final_status != FINAL_STATUS_APP_TERMINATING &&
            expected_final_status != FINAL_STATUS_MAX),
        expected_pending_prerenders_(0),
        prerender_should_wait_for_ready_title_(
            prerender_should_wait_for_ready_title) {
    if (expected_number_of_loads == 0)
      base::MessageLoopForUI::current()->Quit();
  }

  virtual ~TestPrerenderContents() {
    if (expected_final_status_ == FINAL_STATUS_MAX) {
      EXPECT_EQ(match_complete_status(), MATCH_COMPLETE_REPLACEMENT);
    } else {
      EXPECT_EQ(expected_final_status_, final_status()) <<
          " when testing URL " << prerender_url().path() <<
          " (Expected: " << NameFromFinalStatus(expected_final_status_) <<
          ", Actual: " << NameFromFinalStatus(final_status()) << ")";
    }
    // Prerendering RenderViewHosts should be hidden before the first
    // navigation, so this should be happen for every PrerenderContents for
    // which a RenderViewHost is created, regardless of whether or not it's
    // used.
    if (new_render_view_host_)
      EXPECT_TRUE(was_hidden_);

    // A used PrerenderContents will only be destroyed when we swap out
    // WebContents, at the end of a navigation caused by a call to
    // NavigateToURLImpl().
    if (final_status() == FINAL_STATUS_USED)
      EXPECT_TRUE(new_render_view_host_);

    EXPECT_EQ(should_be_shown_, was_shown_);

    // When the PrerenderContents is destroyed, quit the UI message loop.
    // This happens on navigation to used prerendered pages, and soon
    // after cancellation of unused prerendered pages.
    if (quit_message_loop_on_destruction_) {
      // The message loop may not be running if this is swapped in
      // synchronously on a Navigation.
      base::MessageLoop* loop = base::MessageLoopForUI::current();
      if (loop->is_running())
        loop->Quit();
    }
  }

  virtual void RenderProcessGone(base::TerminationStatus status) OVERRIDE {
    // On quit, it's possible to end up here when render processes are closed
    // before the PrerenderManager is destroyed.  As a result, it's possible to
    // get either FINAL_STATUS_APP_TERMINATING or FINAL_STATUS_RENDERER_CRASHED
    // on quit.
    //
    // It's also possible for this to be called after we've been notified of
    // app termination, but before we've been deleted, which is why the second
    // check is needed.
    if (expected_final_status_ == FINAL_STATUS_APP_TERMINATING &&
        final_status() != expected_final_status_) {
      expected_final_status_ = FINAL_STATUS_RENDERER_CRASHED;
    }

    PrerenderContents::RenderProcessGone(status);
  }

  virtual bool AddAliasURL(const GURL& url) OVERRIDE {
    // Prevent FINAL_STATUS_UNSUPPORTED_SCHEME when navigating to about:crash in
    // the PrerenderRendererCrash test.
    if (url.spec() != content::kChromeUICrashURL)
      return PrerenderContents::AddAliasURL(url);
    return true;
  }

  virtual void DidStopLoading(RenderViewHost* render_view_host) OVERRIDE {
    PrerenderContents::DidStopLoading(render_view_host);
    ++number_of_loads_;
    if (ShouldRenderPrerenderedPageCorrectly(expected_final_status_) &&
        number_of_loads_ == expected_number_of_loads_) {
      base::MessageLoopForUI::current()->Quit();
    }
  }

  virtual void AddPendingPrerender(
      scoped_ptr<PendingPrerenderInfo> pending_prerender_info) OVERRIDE {
    PrerenderContents::AddPendingPrerender(pending_prerender_info.Pass());
    if (expected_pending_prerenders_ > 0 &&
        pending_prerender_count() == expected_pending_prerenders_) {
      base::MessageLoop::current()->Quit();
    }
  }

  virtual WebContents* CreateWebContents(
      content::SessionStorageNamespace* session_storage_namespace) OVERRIDE {
    WebContents* web_contents = PrerenderContents::CreateWebContents(
        session_storage_namespace);
    string16 ready_title = ASCIIToUTF16(kReadyTitle);
    if (prerender_should_wait_for_ready_title_)
      ready_title_watcher_.reset(new content::TitleWatcher(
          web_contents, ready_title));
    return web_contents;
  }

  void WaitForPrerenderToHaveReadyTitleIfRequired() {
    if (ready_title_watcher_.get()) {
      string16 ready_title = ASCIIToUTF16(kReadyTitle);
      ASSERT_EQ(ready_title, ready_title_watcher_->WaitAndGetTitle());
    }
  }

  // Waits until the prerender has |expected_pending_prerenders| pending
  // prerenders.
  void WaitForPendingPrerenders(size_t expected_pending_prerenders) {
    if (pending_prerender_count() < expected_pending_prerenders) {
      expected_pending_prerenders_ = expected_pending_prerenders;
      content::RunMessageLoop();
      expected_pending_prerenders_ = 0;
    }

    EXPECT_EQ(expected_pending_prerenders, pending_prerender_count());
  }

  // For tests that open the prerender in a new background tab, the RenderView
  // will not have been made visible when the PrerenderContents is destroyed
  // even though it is used.
  void set_should_be_shown(bool value) { should_be_shown_ = value; }

  int number_of_loads() const { return number_of_loads_; }

  FinalStatus expected_final_status() const { return expected_final_status_; }

  bool quit_message_loop_on_destruction() const {
    return quit_message_loop_on_destruction_;
  }

 private:
  virtual void OnRenderViewHostCreated(
      RenderViewHost* new_render_view_host) OVERRIDE {
    // Used to make sure the RenderViewHost is hidden and, if used,
    // subsequently shown.
    notification_registrar().Add(
        this,
        content::NOTIFICATION_RENDER_WIDGET_VISIBILITY_CHANGED,
        content::Source<RenderWidgetHost>(new_render_view_host));

    new_render_view_host_ = new_render_view_host;

    PrerenderContents::OnRenderViewHostCreated(new_render_view_host);
  }

  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE {
    if (type ==
        content::NOTIFICATION_RENDER_WIDGET_VISIBILITY_CHANGED) {
      EXPECT_EQ(new_render_view_host_,
                content::Source<RenderWidgetHost>(source).ptr());
      bool is_visible = *content::Details<bool>(details).ptr();

      if (!is_visible) {
        was_hidden_ = true;
      } else if (is_visible && was_hidden_) {
        // Once hidden, a prerendered RenderViewHost should only be shown after
        // being removed from the PrerenderContents for display.
        EXPECT_FALSE(GetRenderViewHost());
        was_shown_ = true;
      }
      return;
    }
    PrerenderContents::Observe(type, source, details);
  }

  int number_of_loads_;
  int expected_number_of_loads_;
  FinalStatus expected_final_status_;

  // The RenderViewHost created for the prerender, if any.
  RenderViewHost* new_render_view_host_;
  // Set to true when the prerendering RenderWidget is hidden.
  bool was_hidden_;
  // Set to true when the prerendering RenderWidget is shown, after having been
  // hidden.
  bool was_shown_;
  // Expected final value of was_shown_.  Defaults to true for
  // FINAL_STATUS_USED, and false otherwise.
  bool should_be_shown_;

  // If true, quits message loop on destruction of |this|.
  bool quit_message_loop_on_destruction_;

  // Total number of pending prerenders we're currently waiting for.  Zero
  // indicates we currently aren't waiting for any.
  size_t expected_pending_prerenders_;

  // If true, before calling DidPrerenderPass, will wait for the title of the
  // prerendered page to turn to "READY".
  bool prerender_should_wait_for_ready_title_;
  scoped_ptr<content::TitleWatcher> ready_title_watcher_;
};

// PrerenderManager that uses TestPrerenderContents.
class WaitForLoadPrerenderContentsFactory : public PrerenderContents::Factory {
 public:
  WaitForLoadPrerenderContentsFactory(
      int expected_number_of_loads,
      const std::deque<FinalStatus>& expected_final_status_queue,
      bool prerender_should_wait_for_ready_title)
      : expected_number_of_loads_(expected_number_of_loads),
        expected_final_status_queue_(expected_final_status_queue),
        prerender_should_wait_for_ready_title_(
            prerender_should_wait_for_ready_title) {
    VLOG(1) << "Factory created with queue length " <<
               expected_final_status_queue_.size();
  }

  virtual PrerenderContents* CreatePrerenderContents(
      PrerenderManager* prerender_manager,
      Profile* profile,
      const GURL& url,
      const content::Referrer& referrer,
      Origin origin,
      uint8 experiment_id) OVERRIDE {
    FinalStatus expected_final_status = FINAL_STATUS_MAX;
    if (!expected_final_status_queue_.empty()) {
      expected_final_status = expected_final_status_queue_.front();
      expected_final_status_queue_.pop_front();
    }
    VLOG(1) << "Creating prerender contents for " << url.path() <<
               " with expected final status " << expected_final_status;
    VLOG(1) << expected_final_status_queue_.size() << " left in the queue.";
    return new TestPrerenderContents(prerender_manager,
                                     profile, url, referrer, origin,
                                     expected_number_of_loads_,
                                     expected_final_status,
                                     prerender_should_wait_for_ready_title_);
  }

 private:
  int expected_number_of_loads_;
  std::deque<FinalStatus> expected_final_status_queue_;
  bool prerender_should_wait_for_ready_title_;
};

#if defined(FULL_SAFE_BROWSING)
// A SafeBrowsingDatabaseManager implementation that returns a fixed result for
// a given URL.
class FakeSafeBrowsingDatabaseManager :  public SafeBrowsingDatabaseManager {
 public:
  explicit FakeSafeBrowsingDatabaseManager(SafeBrowsingService* service)
      : SafeBrowsingDatabaseManager(service),
        threat_type_(SB_THREAT_TYPE_SAFE) { }

  // Called on the IO thread to check if the given url is safe or not.  If we
  // can synchronously determine that the url is safe, CheckUrl returns true.
  // Otherwise it returns false, and "client" is called asynchronously with the
  // result when it is ready.
  // Returns true, indicating a SAFE result, unless the URL is the fixed URL
  // specified by the user, and the user-specified result is not SAFE
  // (in which that result will be communicated back via a call into the
  // client, and false will be returned).
  // Overrides SafeBrowsingService::CheckBrowseUrl.
  virtual bool CheckBrowseUrl(const GURL& gurl, Client* client) OVERRIDE {
    if (gurl != url_ || threat_type_ == SB_THREAT_TYPE_SAFE)
      return true;

    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        base::Bind(&FakeSafeBrowsingDatabaseManager::OnCheckBrowseURLDone,
                   this, gurl, client));
    return false;
  }

  void SetThreatTypeForUrl(const GURL& url, SBThreatType threat_type) {
    url_ = url;
    threat_type_ = threat_type;
  }

 private:
  virtual ~FakeSafeBrowsingDatabaseManager() {}

  void OnCheckBrowseURLDone(const GURL& gurl, Client* client) {
    SafeBrowsingDatabaseManager::SafeBrowsingCheck sb_check(
        std::vector<GURL>(1, gurl),
        std::vector<SBFullHash>(),
        client,
        safe_browsing_util::MALWARE);
    sb_check.url_results[0] = threat_type_;
    client->OnSafeBrowsingResult(sb_check);
  }

  GURL url_;
  SBThreatType threat_type_;
  DISALLOW_COPY_AND_ASSIGN(FakeSafeBrowsingDatabaseManager);
};

class FakeSafeBrowsingService : public SafeBrowsingService {
 public:
  FakeSafeBrowsingService() { }

  // Returned pointer has the same lifespan as the database_manager_ refcounted
  // object.
  FakeSafeBrowsingDatabaseManager* fake_database_manager() {
    return fake_database_manager_;
  }

 protected:
  virtual ~FakeSafeBrowsingService() { }

  virtual SafeBrowsingDatabaseManager* CreateDatabaseManager() OVERRIDE {
    fake_database_manager_ = new FakeSafeBrowsingDatabaseManager(this);
    return fake_database_manager_;
  }

 private:
  FakeSafeBrowsingDatabaseManager* fake_database_manager_;

  DISALLOW_COPY_AND_ASSIGN(FakeSafeBrowsingService);
};

// Factory that creates FakeSafeBrowsingService instances.
class TestSafeBrowsingServiceFactory : public SafeBrowsingServiceFactory {
 public:
  TestSafeBrowsingServiceFactory() :
      most_recent_service_(NULL) { }
  virtual ~TestSafeBrowsingServiceFactory() { }

  virtual SafeBrowsingService* CreateSafeBrowsingService() OVERRIDE {
    most_recent_service_ =  new FakeSafeBrowsingService();
    return most_recent_service_;
  }

  FakeSafeBrowsingService* most_recent_service() const {
    return most_recent_service_;
  }

 private:
  FakeSafeBrowsingService* most_recent_service_;
};
#endif

class FakeDevToolsClientHost : public DevToolsClientHost {
 public:
  FakeDevToolsClientHost() {}
  virtual ~FakeDevToolsClientHost() {}
  virtual void InspectedContentsClosing() OVERRIDE {}
  virtual void DispatchOnInspectorFrontend(const std::string& msg) OVERRIDE {}
  virtual void ReplacedWithAnotherClient() OVERRIDE {}
};

class RestorePrerenderMode {
 public:
  RestorePrerenderMode() : prev_mode_(PrerenderManager::GetMode()) {
  }

  ~RestorePrerenderMode() { PrerenderManager::SetMode(prev_mode_); }
 private:
  PrerenderManager::PrerenderManagerMode prev_mode_;
};

// URLRequestJob (and associated handler) which never starts.
class NeverStartURLRequestJob : public net::URLRequestJob {
 public:
  NeverStartURLRequestJob(net::URLRequest* request,
                          net::NetworkDelegate* network_delegate)
      : net::URLRequestJob(request, network_delegate) {
  }

  virtual void Start() OVERRIDE {}

 private:
  virtual ~NeverStartURLRequestJob() {}
};

class NeverStartProtocolHandler
    : public net::URLRequestJobFactory::ProtocolHandler {
 public:
  NeverStartProtocolHandler() {}
  virtual ~NeverStartProtocolHandler() {}

  virtual net::URLRequestJob* MaybeCreateJob(
      net::URLRequest* request,
      net::NetworkDelegate* network_delegate) const OVERRIDE {
    return new NeverStartURLRequestJob(request, network_delegate);
  }
};

void CreateNeverStartProtocolHandlerOnIO(const GURL& url) {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  scoped_ptr<net::URLRequestJobFactory::ProtocolHandler> never_respond_handler(
      new NeverStartProtocolHandler());
  net::URLRequestFilter::GetInstance()->AddUrlProtocolHandler(
      url, never_respond_handler.Pass());
}

}  // namespace

// Many of these tests are flaky. See http://crbug.com/249179
class PrerenderBrowserTest : virtual public InProcessBrowserTest {
 public:
  PrerenderBrowserTest()
      : autostart_test_server_(true),
        prerender_contents_factory_(NULL),
#if defined(FULL_SAFE_BROWSING)
        safe_browsing_factory_(new TestSafeBrowsingServiceFactory()),
#endif
        use_https_src_server_(false),
        call_javascript_(true),
        loader_path_("files/prerender/prerender_loader.html"),
        explicitly_set_browser_(NULL) {}

  virtual ~PrerenderBrowserTest() {}

  content::SessionStorageNamespace* GetSessionStorageNamespace() const {
    WebContents* web_contents =
        current_browser()->tab_strip_model()->GetActiveWebContents();
    if (!web_contents)
      return NULL;
    return web_contents->GetController().GetDefaultSessionStorageNamespace();
  }

  virtual void SetUp() OVERRIDE {
    // TODO(danakj): The GPU Video Decoder needs real GL bindings.
    // crbug.com/269087
    UseRealGLBindings();

    InProcessBrowserTest::SetUp();
  }

  virtual void SetUpInProcessBrowserTestFixture() OVERRIDE {
#if defined(FULL_SAFE_BROWSING)
    SafeBrowsingService::RegisterFactory(safe_browsing_factory_.get());
#endif
  }

  virtual void SetUpCommandLine(CommandLine* command_line) OVERRIDE {
    command_line->AppendSwitchASCII(switches::kPrerenderMode,
                                    switches::kPrerenderModeSwitchValueEnabled);
#if defined(OS_MACOSX)
    // The plugins directory isn't read by default on the Mac, so it needs to be
    // explicitly registered.
    base::FilePath app_dir;
    PathService::Get(chrome::DIR_APP, &app_dir);
    command_line->AppendSwitchPath(
        switches::kExtraPluginDir,
        app_dir.Append(FILE_PATH_LITERAL("plugins")));
#endif
    command_line->AppendSwitch(switches::kAlwaysAuthorizePlugins);
  }

  virtual void SetUpOnMainThread() OVERRIDE {
    current_browser()->profile()->GetPrefs()->SetBoolean(
        prefs::kPromptForDownload, false);
    IncreasePrerenderMemory();
    if (autostart_test_server_)
      ASSERT_TRUE(test_server()->Start());
  }

  // Overload for a single expected final status
  void PrerenderTestURL(const std::string& html_file,
                        FinalStatus expected_final_status,
                        int expected_number_of_loads) {
    PrerenderTestURL(html_file,
                     expected_final_status,
                     expected_number_of_loads,
                     false);
  }

  void PrerenderTestURL(const std::string& html_file,
                        FinalStatus expected_final_status,
                        int expected_number_of_loads,
                        bool prerender_should_wait_for_ready_title) {
    std::deque<FinalStatus> expected_final_status_queue(1,
                                                        expected_final_status);
    PrerenderTestURL(html_file,
                     expected_final_status_queue,
                     expected_number_of_loads,
                     prerender_should_wait_for_ready_title);
  }

  void PrerenderTestURL(
      const std::string& html_file,
      const std::deque<FinalStatus>& expected_final_status_queue,
      int expected_number_of_loads,
      bool prerender_should_wait_for_ready_title) {
    GURL url = test_server()->GetURL(html_file);
    PrerenderTestURLImpl(url, url,
                         expected_final_status_queue,
                         expected_number_of_loads,
                         prerender_should_wait_for_ready_title);
  }

  void PrerenderTestURL(
      const std::string& html_file,
      const std::deque<FinalStatus>& expected_final_status_queue,
      int expected_number_of_loads) {
    PrerenderTestURL(html_file, expected_final_status_queue,
                     expected_number_of_loads, false);
  }

  void PrerenderTestURL(
      const GURL& url,
      FinalStatus expected_final_status,
      int expected_number_of_loads) {
    std::deque<FinalStatus> expected_final_status_queue(1,
                                                        expected_final_status);
    PrerenderTestURLImpl(url, url,
                         expected_final_status_queue,
                         expected_number_of_loads,
                         false);
  }

  void PrerenderTestURL(
      const GURL& prerender_url,
      const GURL& destination_url,
      FinalStatus expected_final_status,
      int expected_number_of_loads) {
    std::deque<FinalStatus> expected_final_status_queue(1,
                                                        expected_final_status);
    PrerenderTestURLImpl(prerender_url, destination_url,
                         expected_final_status_queue,
                         expected_number_of_loads,
                         false);
  }

  void NavigateToDestURL() const {
    NavigateToDestURLWithDisposition(CURRENT_TAB, true);
  }

  // Opens the url in a new tab, with no opener.
  void NavigateToDestURLWithDisposition(
      WindowOpenDisposition disposition,
      bool expect_swap_to_succeed) const {
    NavigateToURLImpl(dest_url_, disposition, expect_swap_to_succeed);
  }

  void OpenDestURLViaClick() const {
    OpenDestURLWithJSImpl("Click()");
  }

  void OpenDestURLViaClickTarget() const {
    OpenDestURLWithJSImpl("ClickTarget()");
  }

  void OpenDestURLViaClickNewWindow() const {
    OpenDestURLWithJSImpl("ShiftClick()");
  }

  void OpenDestURLViaClickNewForegroundTab() const {
#if defined(OS_MACOSX)
    OpenDestURLWithJSImpl("MetaShiftClick()");
#else
    OpenDestURLWithJSImpl("CtrlShiftClick()");
#endif
  }

  void OpenDestURLViaClickNewBackgroundTab() const {
    TestPrerenderContents* prerender_contents = GetPrerenderContents();
    ASSERT_TRUE(prerender_contents != NULL);
    prerender_contents->set_should_be_shown(false);
#if defined(OS_MACOSX)
    OpenDestURLWithJSImpl("MetaClick()");
#else
    OpenDestURLWithJSImpl("CtrlClick()");
#endif
  }

  void OpenDestURLViaWindowOpen() const {
    OpenDestURLWithJSImpl("WindowOpen()");
  }

  void RemoveLinkElement(int i) const {
    current_browser()->tab_strip_model()->GetActiveWebContents()->
        GetRenderViewHost()->ExecuteJavascriptInWebFrame(
            string16(),
            ASCIIToUTF16(base::StringPrintf("RemoveLinkElement(%d)", i)));
  }

  void ClickToNextPageAfterPrerender() {
    content::WindowedNotificationObserver new_page_observer(
        content::NOTIFICATION_NAV_ENTRY_COMMITTED,
        content::NotificationService::AllSources());
    RenderViewHost* render_view_host = current_browser()->tab_strip_model()->
        GetActiveWebContents()->GetRenderViewHost();
    render_view_host->ExecuteJavascriptInWebFrame(
        string16(),
        ASCIIToUTF16("ClickOpenLink()"));
    new_page_observer.Wait();
  }

  void NavigateToNextPageAfterPrerender() const {
    ui_test_utils::NavigateToURL(
        current_browser(),
        test_server()->GetURL("files/prerender/prerender_page.html"));
  }

  void NavigateToDestUrlAndWaitForPassTitle() {
    string16 expected_title = ASCIIToUTF16(kPassTitle);
    content::TitleWatcher title_watcher(
        GetPrerenderContents()->prerender_contents(),
        expected_title);
    NavigateToDestURL();
    EXPECT_EQ(expected_title, title_watcher.WaitAndGetTitle());
  }

  // Called after the prerendered page has been navigated to and then away from.
  // Navigates back through the history to the prerendered page.
  void GoBackToPrerender() {
    content::WindowedNotificationObserver back_nav_observer(
        content::NOTIFICATION_NAV_ENTRY_COMMITTED,
        content::NotificationService::AllSources());
    chrome::GoBack(current_browser(), CURRENT_TAB);
    back_nav_observer.Wait();
    bool original_prerender_page = false;
    ASSERT_TRUE(content::ExecuteScriptAndExtractBool(
        current_browser()->tab_strip_model()->GetActiveWebContents(),
        "window.domAutomationController.send(IsOriginalPrerenderPage())",
        &original_prerender_page));
    EXPECT_TRUE(original_prerender_page);
  }

  // Goes back to the page that was active before the prerender was swapped
  // in. This must be called when the prerendered page is the current page
  // in the active tab.
  void GoBackToPageBeforePrerender() {
    WebContents* tab =
        current_browser()->tab_strip_model()->GetActiveWebContents();
    ASSERT_TRUE(tab);
    EXPECT_FALSE(tab->IsLoading());
    content::WindowedNotificationObserver back_nav_observer(
        content::NOTIFICATION_LOAD_STOP,
        content::Source<NavigationController>(&tab->GetController()));
    chrome::GoBack(current_browser(), CURRENT_TAB);
    back_nav_observer.Wait();
    bool js_result;
    ASSERT_TRUE(content::ExecuteScriptAndExtractBool(
        tab,
        "window.domAutomationController.send(DidBackToOriginalPagePass())",
        &js_result));
    EXPECT_TRUE(js_result);
  }

  void NavigateToURL(const std::string& dest_html_file) const {
    GURL dest_url = test_server()->GetURL(dest_html_file);
    NavigateToURLImpl(dest_url, CURRENT_TAB, true);
  }

  bool UrlIsInPrerenderManager(const std::string& html_file) const {
    return UrlIsInPrerenderManager(test_server()->GetURL(html_file));
  }

  bool UrlIsInPrerenderManager(const GURL& url) const {
    return GetPrerenderManager()->FindPrerenderData(
        url, GetSessionStorageNamespace()) != NULL;
  }

  void set_use_https_src(bool use_https_src_server) {
    use_https_src_server_ = use_https_src_server;
  }

  void DisableJavascriptCalls() {
    call_javascript_ = false;
  }

  TaskManagerModel* GetModel() const {
    return TaskManager::GetInstance()->model();
  }

  PrerenderManager* GetPrerenderManager() const {
    PrerenderManager* prerender_manager =
        PrerenderManagerFactory::GetForProfile(current_browser()->profile());
    return prerender_manager;
  }

  const PrerenderLinkManager* GetPrerenderLinkManager() const {
    PrerenderLinkManager* prerender_link_manager =
        PrerenderLinkManagerFactory::GetForProfile(
            current_browser()->profile());
    return prerender_link_manager;
  }

  bool DidReceivePrerenderStartEventForLinkNumber(int index) const {
    bool received_prerender_started;
    std::string expression = base::StringPrintf(
        "window.domAutomationController.send(Boolean("
            "receivedPrerenderStartEvents[%d]))", index);

    CHECK(content::ExecuteScriptAndExtractBool(
        current_browser()->tab_strip_model()->GetActiveWebContents(),
        expression,
        &received_prerender_started));
    return received_prerender_started;
  }

  bool DidReceivePrerenderLoadEventForLinkNumber(int index) const {
    bool received_prerender_loaded;
    std::string expression = base::StringPrintf(
        "window.domAutomationController.send(Boolean("
            "receivedPrerenderLoadEvents[%d]))", index);

    CHECK(content::ExecuteScriptAndExtractBool(
        current_browser()->tab_strip_model()->GetActiveWebContents(),
        expression,
        &received_prerender_loaded));
    return received_prerender_loaded;
  }

  bool DidReceivePrerenderStopEventForLinkNumber(int index) const {
    bool received_prerender_stopped;
    std::string expression = base::StringPrintf(
        "window.domAutomationController.send(Boolean("
            "receivedPrerenderStopEvents[%d]))", index);

    CHECK(content::ExecuteScriptAndExtractBool(
        current_browser()->tab_strip_model()->GetActiveWebContents(),
        expression,
        &received_prerender_stopped));
    return received_prerender_stopped;
  }

  bool HadPrerenderEventErrors() const {
    bool had_prerender_event_errors;
    CHECK(content::ExecuteScriptAndExtractBool(
        current_browser()->tab_strip_model()->GetActiveWebContents(),
        "window.domAutomationController.send(Boolean("
        "    hadPrerenderEventErrors))",
        &had_prerender_event_errors));
    return had_prerender_event_errors;
  }

  // Asserting on this can result in flaky tests.  PrerenderHandles are
  // removed from the PrerenderLinkManager when the prerender is canceled from
  // the browser, when the prerenders are cancelled from the renderer process,
  // or the channel for the renderer process is closed on the IO thread.  In the
  // last case, the code must be careful to wait for the channel to close, as it
  // is done asynchronously after swapping out the old process.  See
  // ChannelDestructionWatcher.
  bool IsEmptyPrerenderLinkManager() const {
    return GetPrerenderLinkManager()->IsEmpty();
  }

  // Returns length of |prerender_manager_|'s history, or -1 on failure.
  int GetHistoryLength() const {
    scoped_ptr<DictionaryValue> prerender_dict(
        static_cast<DictionaryValue*>(GetPrerenderManager()->GetAsValue()));
    if (!prerender_dict.get())
      return -1;
    ListValue* history_list;
    if (!prerender_dict->GetList("history", &history_list))
      return -1;
    return static_cast<int>(history_list->GetSize());
  }

#if defined(FULL_SAFE_BROWSING)
  FakeSafeBrowsingDatabaseManager* GetFakeSafeBrowsingDatabaseManager() {
    return safe_browsing_factory_->most_recent_service()->
        fake_database_manager();
  }
#endif

  TestPrerenderContents* GetPrerenderContentsFor(const GURL& url) const {
    PrerenderManager::PrerenderData* prerender_data =
        GetPrerenderManager()->FindPrerenderData(
            url, GetSessionStorageNamespace());
    return static_cast<TestPrerenderContents*>(
        prerender_data ? prerender_data->contents() : NULL);
  }

  TestPrerenderContents* GetPrerenderContents() const {
    return GetPrerenderContentsFor(dest_url_);
  }

  void set_loader_path(const std::string& path) {
    loader_path_ = path;
  }

  void set_loader_query_and_fragment(const std::string& query_and_fragment) {
    loader_query_and_fragment_ = query_and_fragment;
  }

  GURL GetCrossDomainTestUrl(const std::string& path) {
    static const std::string secondary_domain = "www.foo.com";
    host_resolver()->AddRule(secondary_domain, "127.0.0.1");
    std::string url_str(base::StringPrintf(
        "http://%s:%d/%s",
        secondary_domain.c_str(),
        test_server()->host_port_pair().port(),
        path.c_str()));
    return GURL(url_str);
  }

  void set_browser(Browser* browser) {
    explicitly_set_browser_ = browser;
  }

  Browser* current_browser() const {
    return explicitly_set_browser_ ? explicitly_set_browser_ : browser();
  }

  void IncreasePrerenderMemory() {
    // Increase the memory allowed in a prerendered page above normal settings.
    // Debug build bots occasionally run against the default limit, and tests
    // were failing because the prerender was canceled due to memory exhaustion.
    // http://crbug.com/93076
    GetPrerenderManager()->mutable_config().max_bytes = 1000 * 1024 * 1024;
  }

 protected:
  bool autostart_test_server_;

 private:
  void PrerenderTestURLImpl(
      const GURL& prerender_url,
      const GURL& destination_url,
      const std::deque<FinalStatus>& expected_final_status_queue,
      int expected_number_of_loads,
      bool prerender_should_wait_for_ready_title) {
    dest_url_ = destination_url;

    std::vector<net::SpawnedTestServer::StringPair> replacement_text;
    replacement_text.push_back(
        make_pair("REPLACE_WITH_PRERENDER_URL", prerender_url.spec()));
    replacement_text.push_back(
        make_pair("REPLACE_WITH_DESTINATION_URL", destination_url.spec()));
    std::string replacement_path;
    ASSERT_TRUE(net::SpawnedTestServer::GetFilePathWithReplacements(
        loader_path_,
        replacement_text,
        &replacement_path));

    const net::SpawnedTestServer* src_server = test_server();
    scoped_ptr<net::SpawnedTestServer> https_src_server;
    if (use_https_src_server_) {
      https_src_server.reset(
          new net::SpawnedTestServer(
              net::SpawnedTestServer::TYPE_HTTPS,
              net::SpawnedTestServer::kLocalhost,
              base::FilePath(FILE_PATH_LITERAL("chrome/test/data"))));
      ASSERT_TRUE(https_src_server->Start());
      src_server = https_src_server.get();
    }
    GURL loader_url = src_server->GetURL(replacement_path +
                                         loader_query_and_fragment_);

    PrerenderManager* prerender_manager = GetPrerenderManager();
    ASSERT_TRUE(prerender_manager);
    prerender_manager->mutable_config().rate_limit_enabled = false;
    prerender_manager->mutable_config().https_allowed = true;
    ASSERT_TRUE(prerender_contents_factory_ == NULL);
    prerender_contents_factory_ =
        new WaitForLoadPrerenderContentsFactory(
            expected_number_of_loads,
            expected_final_status_queue,
            prerender_should_wait_for_ready_title);
    prerender_manager->SetPrerenderContentsFactory(
        prerender_contents_factory_);
    FinalStatus expected_final_status = expected_final_status_queue.front();

    // We construct launch_nav_observer so that we can be certain our loader
    // page has finished loading before continuing. This prevents ambiguous
    // NOTIFICATION_LOAD_STOP events from making tests flaky.
    WebContents* web_contents =
        current_browser()->tab_strip_model()->GetActiveWebContents();
    content::WindowedNotificationObserver loader_nav_observer(
        content::NOTIFICATION_LOAD_STOP,
        content::Source<NavigationController>(
            &web_contents->GetController()));

    // ui_test_utils::NavigateToURL uses its own observer and message loop.
    // Since the test needs to wait until the prerendered page has stopped
    // loading, rather than the page directly navigated to, need to
    // handle browser navigation directly.
    current_browser()->OpenURL(OpenURLParams(
        loader_url, Referrer(), CURRENT_TAB, content::PAGE_TRANSITION_TYPED,
        false));

    content::RunMessageLoop();
    // Now that we've run the prerender until it stopped loading, we can now
    // also make sure the launcher has finished loading.
    loader_nav_observer.Wait();

    TestPrerenderContents* prerender_contents = GetPrerenderContents();

    if (ShouldRenderPrerenderedPageCorrectly(expected_final_status)) {
      ASSERT_NE(static_cast<PrerenderContents*>(NULL), prerender_contents);
      EXPECT_EQ(FINAL_STATUS_MAX, prerender_contents->final_status());

      if (call_javascript_ && expected_number_of_loads > 0) {
        // Wait for the prerendered page to change title to signal it is ready
        // if required.
        prerender_contents->WaitForPrerenderToHaveReadyTitleIfRequired();

        // Check if page behaves as expected while in prerendered state.
        bool prerender_test_result = false;
        ASSERT_TRUE(content::ExecuteScriptAndExtractBool(
            prerender_contents->GetRenderViewHostMutable(),
            "window.domAutomationController.send(DidPrerenderPass())",
            &prerender_test_result));
        EXPECT_TRUE(prerender_test_result);
      }
    } else {
      // In the failure case, we should have removed |dest_url_| from the
      // prerender_manager.  We ignore dummy PrerenderContents (as indicated
      // by not having started), and PrerenderContents that are expected to
      // be left in the manager until the test finishes.
      EXPECT_TRUE(prerender_contents == NULL ||
                  !prerender_contents->prerendering_has_started());
    }
  }

  void NavigateToURLImpl(const GURL& dest_url,
                         WindowOpenDisposition disposition,
                         bool expect_swap_to_succeed) const {
    ASSERT_NE(static_cast<PrerenderManager*>(NULL), GetPrerenderManager());
    // Make sure in navigating we have a URL to use in the PrerenderManager.
    ASSERT_NE(static_cast<PrerenderContents*>(NULL), GetPrerenderContents());

    // If opening the page in a background tab, it won't be shown when swapped
    // in.
    if (disposition == NEW_BACKGROUND_TAB)
      GetPrerenderContents()->set_should_be_shown(false);

    scoped_ptr<content::WindowedNotificationObserver> page_load_observer;
    WebContents* web_contents = NULL;

    if (GetPrerenderContents()->prerender_contents()) {
      // In the case of zero loads, need to wait for the page load to complete
      // before running any Javascript.
      web_contents = GetPrerenderContents()->prerender_contents();
      if (GetPrerenderContents()->number_of_loads() == 0) {
        page_load_observer.reset(
            new content::WindowedNotificationObserver(
                content::NOTIFICATION_LOAD_STOP,
                content::Source<NavigationController>(
                    &web_contents->GetController())));
      }
    }

    // Navigate to the prerendered URL, but don't run the message loop. Browser
    // issued navigations to prerendered pages will synchronously swap in the
    // prerendered page.
    ui_test_utils::NavigateToURLWithDisposition(
        current_browser(), dest_url, disposition,
        ui_test_utils::BROWSER_TEST_NONE);

    if (call_javascript_ && web_contents && expect_swap_to_succeed) {
      if (page_load_observer.get())
        page_load_observer->Wait();

      bool display_test_result = false;
      ASSERT_TRUE(content::ExecuteScriptAndExtractBool(
          web_contents,
          "window.domAutomationController.send(DidDisplayPass())",
          &display_test_result));
      EXPECT_TRUE(display_test_result);
    }
  }

  // Opens the prerendered page using javascript functions in the
  // loader page. |javascript_function_name| should be a 0 argument function
  // which is invoked.
  void OpenDestURLWithJSImpl(const std::string& javascript_function_name)
      const {
    TestPrerenderContents* prerender_contents = GetPrerenderContents();
    ASSERT_NE(static_cast<PrerenderContents*>(NULL), prerender_contents);

    RenderViewHost* render_view_host = current_browser()->tab_strip_model()->
        GetActiveWebContents()->GetRenderViewHost();

    render_view_host->ExecuteJavascriptInWebFrame(
        string16(), ASCIIToUTF16(javascript_function_name));

    if (prerender_contents->quit_message_loop_on_destruction()) {
      // Run message loop until the prerender contents is destroyed.
      content::RunMessageLoop();
    } else {
      // We don't expect to pick up a running prerender, so instead
      // observe one navigation.
      content::TestNavigationObserver observer(
          current_browser()->tab_strip_model()->GetActiveWebContents());
      observer.StartWatchingNewWebContents();
      base::RunLoop run_loop;
      observer.WaitForObservation(
          base::Bind(&content::RunThisRunLoop,
                     base::Unretained(&run_loop)),
          content::GetQuitTaskForRunLoop(&run_loop));
    }
  }

  WaitForLoadPrerenderContentsFactory* prerender_contents_factory_;
#if defined(FULL_SAFE_BROWSING)
  scoped_ptr<TestSafeBrowsingServiceFactory> safe_browsing_factory_;
#endif
  GURL dest_url_;
  bool use_https_src_server_;
  bool call_javascript_;
  std::string loader_path_;
  std::string loader_query_and_fragment_;
  Browser* explicitly_set_browser_;
};

// Checks that a page is correctly prerendered in the case of a
// <link rel=prerender> tag and then loaded into a tab in response to a
// navigation.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderPage) {
  PrerenderTestURL("files/prerender/prerender_page.html", FINAL_STATUS_USED, 1);

  ChannelDestructionWatcher channel_close_watcher;
  channel_close_watcher.WatchChannel(browser()->tab_strip_model()->
      GetActiveWebContents()->GetRenderProcessHost());
  NavigateToDestURL();
  channel_close_watcher.WaitForChannelClose();

  ASSERT_TRUE(IsEmptyPrerenderLinkManager());
}

// Checks that pending prerenders launch and receive proper event treatment.
// Disabled due to http://crbug.com/167792
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, DISABLED_PrerenderPagePending) {
  std::deque<FinalStatus> expected_final_status_queue;
  expected_final_status_queue.push_back(FINAL_STATUS_USED);
  expected_final_status_queue.push_back(FINAL_STATUS_USED);
  PrerenderTestURL("files/prerender/prerender_page_pending.html",
                   expected_final_status_queue, 1);

  ChannelDestructionWatcher first_channel_close_watcher;

  first_channel_close_watcher.WatchChannel(browser()->tab_strip_model()->
      GetActiveWebContents()->GetRenderProcessHost());
  NavigateToDestURL();
  // NavigateToDestURL doesn't run a message loop. Normally that's fine, but in
  // this case, we need the pending prerenders to start.
  content::RunMessageLoop();
  first_channel_close_watcher.WaitForChannelClose();

  EXPECT_TRUE(DidReceivePrerenderStartEventForLinkNumber(0));
  EXPECT_FALSE(DidReceivePrerenderStopEventForLinkNumber(0));
  EXPECT_FALSE(HadPrerenderEventErrors());

  const GURL prerender_page_url =
      test_server()->GetURL("files/prerender/prerender_page.html");
  EXPECT_FALSE(IsEmptyPrerenderLinkManager());
  EXPECT_NE(static_cast<TestPrerenderContents*>(NULL),
            GetPrerenderContentsFor(prerender_page_url));

  // Now navigate to our target page.
  ChannelDestructionWatcher second_channel_close_watcher;
  second_channel_close_watcher.WatchChannel(browser()->tab_strip_model()->
      GetActiveWebContents()->GetRenderProcessHost());
  ui_test_utils::NavigateToURLWithDisposition(
      current_browser(), prerender_page_url, CURRENT_TAB,
      ui_test_utils::BROWSER_TEST_NONE);
  second_channel_close_watcher.WaitForChannelClose();

  EXPECT_TRUE(IsEmptyPrerenderLinkManager());
}

// Checks that pending prerenders which are canceled before they are launched
// never get started.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderPageRemovesPending) {
  PrerenderTestURL("files/prerender/prerender_page_removes_pending.html",
                   FINAL_STATUS_USED, 1);

  ChannelDestructionWatcher channel_close_watcher;
  channel_close_watcher.WatchChannel(browser()->tab_strip_model()->
      GetActiveWebContents()->GetRenderProcessHost());
  NavigateToDestURL();
  channel_close_watcher.WaitForChannelClose();

  EXPECT_FALSE(DidReceivePrerenderStartEventForLinkNumber(1));
  EXPECT_FALSE(DidReceivePrerenderStopEventForLinkNumber(1));
  EXPECT_FALSE(HadPrerenderEventErrors());
  // IsEmptyPrerenderLinkManager() is not racy because the earlier DidReceive*
  // calls did a thread/process hop to the renderer which insured pending
  // renderer events have arrived.
  ASSERT_TRUE(IsEmptyPrerenderLinkManager());
}

// Flaky, http://crbug.com/167340.
IN_PROC_BROWSER_TEST_F(
    PrerenderBrowserTest, DISABLED_PrerenderPageRemovingLink) {
  set_loader_path("files/prerender/prerender_loader_removing_links.html");
  set_loader_query_and_fragment("?links_to_insert=1");
  PrerenderTestURL("files/prerender/prerender_page.html",
                   FINAL_STATUS_CANCELLED, 1);
  EXPECT_TRUE(DidReceivePrerenderStartEventForLinkNumber(0));
  EXPECT_FALSE(DidReceivePrerenderStopEventForLinkNumber(0));

  // No ChannelDestructionWatcher is needed here, since prerenders in the
  // PrerenderLinkManager should be deleted by removing the links, rather than
  // shutting down the renderer process.
  RemoveLinkElement(0);
  EXPECT_TRUE(DidReceivePrerenderStartEventForLinkNumber(0));
  EXPECT_FALSE(DidReceivePrerenderStopEventForLinkNumber(0));
  EXPECT_FALSE(HadPrerenderEventErrors());
  // IsEmptyPrerenderLinkManager() is not racy because the earlier DidReceive*
  // calls did a thread/process hop to the renderer which insured pending
  // renderer events have arrived.
  EXPECT_TRUE(IsEmptyPrerenderLinkManager());
}

// Flaky, http://crbug.com/167340.
IN_PROC_BROWSER_TEST_F(
    PrerenderBrowserTest, DISABLED_PrerenderPageRemovingLinkWithTwoLinks) {
  GetPrerenderManager()->mutable_config().max_link_concurrency = 2;
  GetPrerenderManager()->mutable_config().max_link_concurrency_per_launcher = 2;

  set_loader_path("files/prerender/prerender_loader_removing_links.html");
  set_loader_query_and_fragment("?links_to_insert=2");
  PrerenderTestURL("files/prerender/prerender_page.html",
                   FINAL_STATUS_CANCELLED, 1);
  EXPECT_TRUE(DidReceivePrerenderStartEventForLinkNumber(0));
  EXPECT_FALSE(DidReceivePrerenderStopEventForLinkNumber(0));
  EXPECT_TRUE(DidReceivePrerenderStartEventForLinkNumber(1));
  EXPECT_FALSE(DidReceivePrerenderStopEventForLinkNumber(1));

  RemoveLinkElement(0);
  RemoveLinkElement(1);
  EXPECT_TRUE(DidReceivePrerenderStartEventForLinkNumber(0));
  EXPECT_FALSE(DidReceivePrerenderStopEventForLinkNumber(0));
  EXPECT_TRUE(DidReceivePrerenderStartEventForLinkNumber(1));
  EXPECT_FALSE(DidReceivePrerenderStopEventForLinkNumber(1));
  EXPECT_FALSE(HadPrerenderEventErrors());
  // IsEmptyPrerenderLinkManager() is not racy because the earlier DidReceive*
  // calls did a thread/process hop to the renderer which insured pending
  // renderer events have arrived.
  EXPECT_TRUE(IsEmptyPrerenderLinkManager());
}

#if defined(OS_WIN)
// TODO(gavinp): Fails on XP Rel - http://crbug.com/128841
#define MAYBE_PrerenderPageRemovingLinkWithTwoLinksRemovingOne \
    DISABLED_PrerenderPageRemovingLinkWithTwoLinksRemovingOne
#else
#define MAYBE_PrerenderPageRemovingLinkWithTwoLinksRemovingOne \
    PrerenderPageRemovingLinkWithTwoLinksRemovingOne
#endif  // defined(OS_WIN)
IN_PROC_BROWSER_TEST_F(
    PrerenderBrowserTest,
    MAYBE_PrerenderPageRemovingLinkWithTwoLinksRemovingOne) {
  GetPrerenderManager()->mutable_config().max_link_concurrency = 2;
  GetPrerenderManager()->mutable_config().max_link_concurrency_per_launcher = 2;
  set_loader_path("files/prerender/prerender_loader_removing_links.html");
  set_loader_query_and_fragment("?links_to_insert=2");
  PrerenderTestURL("files/prerender/prerender_page.html",
                   FINAL_STATUS_USED, 1);
  EXPECT_TRUE(DidReceivePrerenderStartEventForLinkNumber(0));
  EXPECT_FALSE(DidReceivePrerenderStopEventForLinkNumber(0));
  EXPECT_TRUE(DidReceivePrerenderStartEventForLinkNumber(1));
  EXPECT_FALSE(DidReceivePrerenderStopEventForLinkNumber(1));

  RemoveLinkElement(0);
  EXPECT_TRUE(DidReceivePrerenderStartEventForLinkNumber(0));
  EXPECT_FALSE(DidReceivePrerenderStopEventForLinkNumber(0));
  EXPECT_TRUE(DidReceivePrerenderStartEventForLinkNumber(1));
  EXPECT_FALSE(DidReceivePrerenderStopEventForLinkNumber(1));
  EXPECT_FALSE(HadPrerenderEventErrors());
  // IsEmptyPrerenderLinkManager() is not racy because the earlier DidReceive*
  // calls did a thread/process hop to the renderer which insured pending
  // renderer events have arrived.
  EXPECT_FALSE(IsEmptyPrerenderLinkManager());

  ChannelDestructionWatcher channel_close_watcher;
  channel_close_watcher.WatchChannel(browser()->tab_strip_model()->
      GetActiveWebContents()->GetRenderProcessHost());
  NavigateToDestURL();
  channel_close_watcher.WaitForChannelClose();

  EXPECT_TRUE(IsEmptyPrerenderLinkManager());
}

// Checks that prerendering works in incognito mode.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderIncognito) {
  Profile* normal_profile = current_browser()->profile();
  set_browser(
      ui_test_utils::OpenURLOffTheRecord(normal_profile, GURL("about:blank")));
  // Increase memory expectations on the incognito PrerenderManager.
  IncreasePrerenderMemory();
  PrerenderTestURL("files/prerender/prerender_page.html", FINAL_STATUS_USED, 1);
  NavigateToDestURL();
}

// Checks that the visibility API works.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       DISABLED_PrerenderVisibility) {
  PrerenderTestURL("files/prerender/prerender_visibility.html",
                   FINAL_STATUS_USED,
                   1);
  NavigateToDestURL();
}

// Checks that the prerendering of a page is canceled correctly if we try to
// swap it in before it commits.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderNoCommitNoSwap) {
  // Navigate to a page that triggers a prerender for a URL that never commits.
  const GURL kNoCommitUrl("http://never-respond.example.com");
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&CreateNeverStartProtocolHandlerOnIO, kNoCommitUrl));
  PrerenderTestURL(kNoCommitUrl,
                   FINAL_STATUS_CANCELLED,
                   0);

  // Navigate to the URL, but assume the contents won't be swapped in.
  NavigateToDestURLWithDisposition(CURRENT_TAB, false);

  // Confirm that the prerendered version of the URL is not swapped in,
  // since it never committed.
  EXPECT_TRUE(UrlIsInPrerenderManager(kNoCommitUrl));

  // Post a task to cancel all the prerenders, so that we don't wait further.
  base::MessageLoop::current()->PostTask(
      FROM_HERE, base::Bind(&CancelAllPrerenders, GetPrerenderManager()));
  content::RunMessageLoop();
}

// Checks that the prerendering of a page is canceled correctly when a
// Javascript alert is called.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderAlertBeforeOnload) {
  PrerenderTestURL("files/prerender/prerender_alert_before_onload.html",
                   FINAL_STATUS_JAVASCRIPT_ALERT,
                   1);
}

// Checks that the prerendering of a page is canceled correctly when a
// Javascript alert is called.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderAlertAfterOnload) {
  PrerenderTestURL("files/prerender/prerender_alert_after_onload.html",
                   FINAL_STATUS_JAVASCRIPT_ALERT,
                   1);
}

// Checks that plugins are not loaded while a page is being preloaded, but
// are loaded when the page is displayed.
#if defined(USE_AURA)
// http://crbug.com/103496
#define MAYBE_PrerenderDelayLoadPlugin DISABLED_PrerenderDelayLoadPlugin
#elif defined(OS_MACOSX)
// http://crbug.com/100514
#define MAYBE_PrerenderDelayLoadPlugin DISABLED_PrerenderDelayLoadPlugin
#elif defined(OS_WIN) && defined(ARCH_CPU_X86_64)
// TODO(jschuh): Failing plugin tests. crbug.com/244653
#define MAYBE_PrerenderDelayLoadPlugin DISABLED_PrerenderDelayLoadPlugin
#else
#define MAYBE_PrerenderDelayLoadPlugin PrerenderDelayLoadPlugin
#endif
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, MAYBE_PrerenderDelayLoadPlugin) {
  PrerenderTestURL("files/prerender/plugin_delay_load.html",
                   FINAL_STATUS_USED,
                   1);
  NavigateToDestURL();
}

// Checks that plugins are not loaded on prerendering pages when click-to-play
// is enabled.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderClickToPlay) {
  // Enable click-to-play.
  HostContentSettingsMap* content_settings_map =
      current_browser()->profile()->GetHostContentSettingsMap();
  content_settings_map->SetDefaultContentSetting(
      CONTENT_SETTINGS_TYPE_PLUGINS, CONTENT_SETTING_ASK);

  PrerenderTestURL("files/prerender/prerender_plugin_click_to_play.html",
                   FINAL_STATUS_USED,
                   1);
  NavigateToDestURL();
}

// Checks that we don't load a NaCl plugin when NaCl is disabled.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderNaClPluginDisabled) {
  PrerenderTestURL("files/prerender/prerender_plugin_nacl_disabled.html",
                   FINAL_STATUS_USED,
                   1);
  NavigateToDestURL();


  // Run this check again.  When we try to load aa ppapi plugin, the
  // "loadstart" event is asynchronously posted to a message loop.
  // It's possible that earlier call could have been run before the
  // the "loadstart" event was posted.
  // TODO(mmenke):  While this should reliably fail on regressions, the
  //                reliability depends on the specifics of ppapi plugin
  //                loading.  It would be great if we could avoid that.
  WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  bool display_test_result = false;
  ASSERT_TRUE(content::ExecuteScriptAndExtractBool(
      web_contents,
      "window.domAutomationController.send(DidDisplayPass())",
      &display_test_result));
  EXPECT_TRUE(display_test_result);
}

// Checks that plugins in an iframe are not loaded while a page is
// being preloaded, but are loaded when the page is displayed.
#if defined(USE_AURA)
// http://crbug.com/103496
#define MAYBE_PrerenderIframeDelayLoadPlugin \
        DISABLED_PrerenderIframeDelayLoadPlugin
#elif defined(OS_MACOSX)
// http://crbug.com/100514
#define MAYBE_PrerenderIframeDelayLoadPlugin \
        DISABLED_PrerenderIframeDelayLoadPlugin
#elif defined(OS_WIN) && defined(ARCH_CPU_X86_64)
// TODO(jschuh): Failing plugin tests. crbug.com/244653
#define MAYBE_PrerenderIframeDelayLoadPlugin \
        DISABLED_PrerenderIframeDelayLoadPlugin
#else
#define MAYBE_PrerenderIframeDelayLoadPlugin PrerenderIframeDelayLoadPlugin
#endif
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       MAYBE_PrerenderIframeDelayLoadPlugin) {
  PrerenderTestURL("files/prerender/prerender_iframe_plugin_delay_load.html",
                   FINAL_STATUS_USED,
                   1);
  NavigateToDestURL();
}

// Renders a page that contains a prerender link to a page that contains an
// iframe with a source that requires http authentication. This should not
// prerender successfully.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderHttpAuthentication) {
  PrerenderTestURL("files/prerender/prerender_http_auth_container.html",
                   FINAL_STATUS_AUTH_NEEDED,
                   1);
}

// Checks that client-issued redirects work with prerendering.
// This version navigates to the page which issues the redirection, rather
// than the final destination page.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       DISABLED_PrerenderClientRedirectNavigateToFirst) {
  PrerenderTestURL(CreateClientRedirect("files/prerender/prerender_page.html"),
                   FINAL_STATUS_USED,
                   2);
  NavigateToDestURL();
}

// Checks that client-issued redirects work with prerendering.
// This version navigates to the final destination page, rather than the
// page which does the redirection.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       PrerenderClientRedirectNavigateToSecond) {
  PrerenderTestURL(CreateClientRedirect("files/prerender/prerender_page.html"),
                   FINAL_STATUS_USED,
                   2);
  NavigateToURL("files/prerender/prerender_page.html");
}

// Checks that client-issued redirects work with prerendering.
// This version navigates to the final destination page, rather than the
// page which does the redirection via a mouse click.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
    DISABLED_PrerenderClientRedirectNavigateToSecondViaClick) {
  GURL prerender_url = test_server()->GetURL(
      CreateClientRedirect("files/prerender/prerender_page.html"));
  GURL destination_url = test_server()->GetURL(
      "files/prerender/prerender_page.html");
  PrerenderTestURL(prerender_url, destination_url, FINAL_STATUS_USED, 2);
  OpenDestURLViaClick();
}

// Checks that a prerender for an https will prevent a prerender from happening.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderHttps) {
  net::SpawnedTestServer https_server(
      net::SpawnedTestServer::TYPE_HTTPS, net::SpawnedTestServer::kLocalhost,
      base::FilePath(FILE_PATH_LITERAL("chrome/test/data")));
  ASSERT_TRUE(https_server.Start());
  GURL https_url = https_server.GetURL("files/prerender/prerender_page.html");
  PrerenderTestURL(https_url,
                   FINAL_STATUS_USED,
                   1);
  NavigateToDestURL();
}

// Checks that client-issued redirects to an https page will cancel prerenders.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       DISABLED_PrerenderClientRedirectToHttps) {
  net::SpawnedTestServer https_server(
      net::SpawnedTestServer::TYPE_HTTPS, net::SpawnedTestServer::kLocalhost,
      base::FilePath(FILE_PATH_LITERAL("chrome/test/data")));
  ASSERT_TRUE(https_server.Start());
  GURL https_url = https_server.GetURL("files/prerender/prerender_page.html");
  PrerenderTestURL(CreateClientRedirect(https_url.spec()),
                   FINAL_STATUS_USED,
                   2);
  NavigateToDestURL();
}

// Checks that client-issued redirects within an iframe in a prerendered
// page will not count as an "alias" for the prerendered page.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       DISABLED_PrerenderClientRedirectInIframe) {
  std::string redirect_path = CreateClientRedirect(
      "/files/prerender/prerender_embedded_content.html");
  std::vector<net::SpawnedTestServer::StringPair> replacement_text;
  replacement_text.push_back(
      std::make_pair("REPLACE_WITH_URL", "/" + redirect_path));
  std::string replacement_path;
  ASSERT_TRUE(net::SpawnedTestServer::GetFilePathWithReplacements(
      "files/prerender/prerender_with_iframe.html",
      replacement_text,
      &replacement_path));
  PrerenderTestURL(replacement_path, FINAL_STATUS_USED, 2);
  EXPECT_FALSE(UrlIsInPrerenderManager(
      "files/prerender/prerender_embedded_content.html"));
  NavigateToDestURL();
}

// Checks that client-issued redirects within an iframe in a prerendered
// page to an https page will not cancel the prerender, nor will it
// count as an "alias" for the prerendered page.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       DISABLED_PrerenderClientRedirectToHttpsInIframe) {
  net::SpawnedTestServer https_server(
      net::SpawnedTestServer::TYPE_HTTPS, net::SpawnedTestServer::kLocalhost,
      base::FilePath(FILE_PATH_LITERAL("chrome/test/data")));
  ASSERT_TRUE(https_server.Start());
  GURL https_url = https_server.GetURL("files/prerender/prerender_page.html");
  std::string redirect_path = CreateClientRedirect(https_url.spec());
  std::vector<net::SpawnedTestServer::StringPair> replacement_text;
  replacement_text.push_back(
      std::make_pair("REPLACE_WITH_URL", "/" + redirect_path));
  std::string replacement_path;
  ASSERT_TRUE(net::SpawnedTestServer::GetFilePathWithReplacements(
      "files/prerender/prerender_with_iframe.html",
      replacement_text,
      &replacement_path));
  PrerenderTestURL(replacement_path, FINAL_STATUS_USED, 2);
  EXPECT_FALSE(UrlIsInPrerenderManager(https_url));
  NavigateToDestURL();
}

// Checks that server-issued redirects work with prerendering.
// This version navigates to the page which issues the redirection, rather
// than the final destination page.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       PrerenderServerRedirectNavigateToFirst) {
  PrerenderTestURL(CreateServerRedirect("files/prerender/prerender_page.html"),
                   FINAL_STATUS_USED,
                   1);
  NavigateToDestURL();
}

// Checks that server-issued redirects work with prerendering.
// This version navigates to the final destination page, rather than the
// page which does the redirection.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       PrerenderServerRedirectNavigateToSecond) {
  PrerenderTestURL(CreateServerRedirect("files/prerender/prerender_page.html"),
                   FINAL_STATUS_USED,
                   1);
  NavigateToURL("files/prerender/prerender_page.html");
}

// Checks that server-issued redirects work with prerendering.
// This version navigates to the final destination page, rather than the
// page which does the redirection via a mouse click.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       PrerenderServerRedirectNavigateToSecondViaClick) {
  GURL prerender_url = test_server()->GetURL(
      CreateServerRedirect("files/prerender/prerender_page.html"));
  GURL destination_url = test_server()->GetURL(
      "files/prerender/prerender_page.html");
  PrerenderTestURL(prerender_url, destination_url, FINAL_STATUS_USED, 1);
  OpenDestURLViaClick();
}

// Checks that server-issued redirects from an http to an https
// location will cancel prerendering.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       PrerenderServerRedirectToHttps) {
  net::SpawnedTestServer https_server(
      net::SpawnedTestServer::TYPE_HTTPS, net::SpawnedTestServer::kLocalhost,
      base::FilePath(FILE_PATH_LITERAL("chrome/test/data")));
  ASSERT_TRUE(https_server.Start());
  GURL https_url = https_server.GetURL("files/prerender/prerender_page.html");
  PrerenderTestURL(CreateServerRedirect(https_url.spec()),
                   FINAL_STATUS_USED,
                   1);
  NavigateToDestURL();
}

// Checks that server-issued redirects within an iframe in a prerendered
// page will not count as an "alias" for the prerendered page.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderServerRedirectInIframe) {
  std::string redirect_path = CreateServerRedirect(
      "/files/prerender/prerender_embedded_content.html");
  std::vector<net::SpawnedTestServer::StringPair> replacement_text;
  replacement_text.push_back(
      std::make_pair("REPLACE_WITH_URL", "/" + redirect_path));
  std::string replacement_path;
  ASSERT_TRUE(net::SpawnedTestServer::GetFilePathWithReplacements(
      "files/prerender/prerender_with_iframe.html",
      replacement_text,
      &replacement_path));
  PrerenderTestURL(replacement_path, FINAL_STATUS_USED, 1);
  EXPECT_FALSE(UrlIsInPrerenderManager(
      "files/prerender/prerender_embedded_content.html"));
  NavigateToDestURL();
}

// Checks that server-issued redirects within an iframe in a prerendered
// page to an https page will not cancel the prerender, nor will it
// count as an "alias" for the prerendered page.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       DISABLED_PrerenderServerRedirectToHttpsInIframe) {
  net::SpawnedTestServer https_server(
      net::SpawnedTestServer::TYPE_HTTPS, net::SpawnedTestServer::kLocalhost,
      base::FilePath(FILE_PATH_LITERAL("chrome/test/data")));
  ASSERT_TRUE(https_server.Start());
  GURL https_url = https_server.GetURL("files/prerender/prerender_page.html");
  std::string redirect_path = CreateServerRedirect(https_url.spec());
  std::vector<net::SpawnedTestServer::StringPair> replacement_text;
  replacement_text.push_back(
      std::make_pair("REPLACE_WITH_URL", "/" + redirect_path));
  std::string replacement_path;
  ASSERT_TRUE(net::SpawnedTestServer::GetFilePathWithReplacements(
      "files/prerender/prerender_with_iframe.html",
      replacement_text,
      &replacement_path));
  PrerenderTestURL(replacement_path, FINAL_STATUS_USED, 1);
  EXPECT_FALSE(UrlIsInPrerenderManager(https_url));
  NavigateToDestURL();
}

// Prerenders a page that contains an automatic download triggered through an
// iframe. This should not prerender successfully.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderDownloadIframe) {
  PrerenderTestURL("files/prerender/prerender_download_iframe.html",
                   FINAL_STATUS_DOWNLOAD,
                   1);
}

// Prerenders a page that contains an automatic download triggered through
// Javascript changing the window.location. This should not prerender
// successfully
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderDownloadLocation) {
  PrerenderTestURL(CreateClientRedirect("files/download-test1.lib"),
                   FINAL_STATUS_DOWNLOAD,
                   1);
}

// Prerenders a page that contains an automatic download triggered through a
// client-issued redirect. This should not prerender successfully.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderDownloadClientRedirect) {
  PrerenderTestURL("files/prerender/prerender_download_refresh.html",
                   FINAL_STATUS_DOWNLOAD,
                   1);
}

// Checks that the referrer is set when prerendering.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderReferrer) {
  PrerenderTestURL("files/prerender/prerender_referrer.html",
                   FINAL_STATUS_USED,
                   1);
  NavigateToDestURL();
}

// Checks that the referrer is not set when prerendering and the source page is
// HTTPS.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       DISABLED_PrerenderNoSSLReferrer) {
  set_use_https_src(true);
  PrerenderTestURL("files/prerender/prerender_no_referrer.html",
                   FINAL_STATUS_USED,
                   1);
  NavigateToDestURL();
}

// Checks that popups on a prerendered page cause cancellation.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderPopup) {
  PrerenderTestURL("files/prerender/prerender_popup.html",
                   FINAL_STATUS_CREATE_NEW_WINDOW,
                   1);
}

// Checks that registering a protocol handler causes cancellation.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderRegisterProtocolHandler) {
  PrerenderTestURL("files/prerender/prerender_register_protocol_handler.html",
                   FINAL_STATUS_REGISTER_PROTOCOL_HANDLER,
                   1);
}

// Checks that renderers using excessive memory will be terminated.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderExcessiveMemory) {
  ASSERT_TRUE(GetPrerenderManager());
  GetPrerenderManager()->mutable_config().max_bytes = 30 * 1024 * 1024;
  PrerenderTestURL("files/prerender/prerender_excessive_memory.html",
                   FINAL_STATUS_MEMORY_LIMIT_EXCEEDED,
                   1);
}

// Checks shutdown code while a prerender is active.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderQuickQuit) {
  PrerenderTestURL("files/prerender/prerender_page.html",
                   FINAL_STATUS_APP_TERMINATING,
                   0);
}

// TODO(gavinp,sreeram): Fix http://crbug.com/145248 and deflake this test.
// Checks that we don't prerender in an infinite loop.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, DISABLED_PrerenderInfiniteLoop) {
  const char* const kHtmlFileA = "files/prerender/prerender_infinite_a.html";
  const char* const kHtmlFileB = "files/prerender/prerender_infinite_b.html";

  std::deque<FinalStatus> expected_final_status_queue;
  expected_final_status_queue.push_back(FINAL_STATUS_USED);
  expected_final_status_queue.push_back(FINAL_STATUS_APP_TERMINATING);

  PrerenderTestURL(kHtmlFileA, expected_final_status_queue, 1);
  ASSERT_TRUE(GetPrerenderContents());
  GetPrerenderContents()->WaitForPendingPrerenders(1u);

  // Next url should be in pending list but not an active entry.
  EXPECT_FALSE(UrlIsInPrerenderManager(kHtmlFileB));

  NavigateToDestURL();

  // Make sure the PrerenderContents for the next url is now in the manager
  // and not pending.
  EXPECT_TRUE(UrlIsInPrerenderManager(kHtmlFileB));
}

// TODO(gavinp,sreeram): Fix http://crbug.com/145248 and deflake this test.
// Checks that we don't prerender in an infinite loop and multiple links are
// handled correctly.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       DISABLED_PrerenderInfiniteLoopMultiple) {
  const char* const kHtmlFileA =
      "files/prerender/prerender_infinite_a_multiple.html";
  const char* const kHtmlFileB =
      "files/prerender/prerender_infinite_b_multiple.html";
  const char* const kHtmlFileC =
      "files/prerender/prerender_infinite_c_multiple.html";

  // This test is conceptually simplest if concurrency is at two, since we
  // don't have to worry about which of kHtmlFileB or kHtmlFileC gets evicted.
  GetPrerenderManager()->mutable_config().max_link_concurrency = 2;
  GetPrerenderManager()->mutable_config().max_link_concurrency_per_launcher = 2;

  std::deque<FinalStatus> expected_final_status_queue;
  expected_final_status_queue.push_back(FINAL_STATUS_USED);
  expected_final_status_queue.push_back(FINAL_STATUS_APP_TERMINATING);
  expected_final_status_queue.push_back(FINAL_STATUS_APP_TERMINATING);

  PrerenderTestURL(kHtmlFileA, expected_final_status_queue, 1);
  ASSERT_TRUE(GetPrerenderContents());
  GetPrerenderContents()->WaitForPendingPrerenders(2u);

  // Next url should be in pending list but not an active entry.
  EXPECT_FALSE(UrlIsInPrerenderManager(kHtmlFileB));
  EXPECT_FALSE(UrlIsInPrerenderManager(kHtmlFileC));

  NavigateToDestURL();

  // Make sure the PrerenderContents for the next urls are now in the manager
  // and not pending. One and only one of the URLs (the last seen) should be the
  // active entry.
  bool url_b_is_active_prerender = UrlIsInPrerenderManager(kHtmlFileB);
  bool url_c_is_active_prerender = UrlIsInPrerenderManager(kHtmlFileC);
  EXPECT_TRUE(url_b_is_active_prerender && url_c_is_active_prerender);
}

// See crbug.com/131836.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, DISABLED_PrerenderTaskManager) {
  // Show the task manager. This populates the model.
  chrome::OpenTaskManager(current_browser());
  // Wait for the model of task manager to start.
  TaskManagerBrowserTestUtil::WaitForWebResourceChange(1);

  // Start with two resources.
  PrerenderTestURL("files/prerender/prerender_page.html", FINAL_STATUS_USED, 1);

  // One of the resources that has a WebContents associated with it should have
  // the Prerender prefix.
  const string16 prefix =
      l10n_util::GetStringFUTF16(IDS_TASK_MANAGER_PRERENDER_PREFIX, string16());
  string16 prerender_title;
  int num_prerender_tabs = 0;

  const TaskManagerModel* model = GetModel();
  for (int i = 0; i < model->ResourceCount(); ++i) {
    if (model->GetResourceWebContents(i)) {
      prerender_title = model->GetResourceTitle(i);
      if (StartsWith(prerender_title, prefix, true))
        ++num_prerender_tabs;
    }
  }
  EXPECT_EQ(1, num_prerender_tabs);
  const string16 prerender_page_title = prerender_title.substr(prefix.length());

  NavigateToDestURL();

  // There should be no tabs with the Prerender prefix.
  const string16 tab_prefix =
      l10n_util::GetStringFUTF16(IDS_TASK_MANAGER_TAB_PREFIX, string16());
  num_prerender_tabs = 0;
  int num_tabs_with_prerender_page_title = 0;
  for (int i = 0; i < model->ResourceCount(); ++i) {
    if (model->GetResourceWebContents(i)) {
      string16 tab_title = model->GetResourceTitle(i);
      if (StartsWith(tab_title, prefix, true)) {
        ++num_prerender_tabs;
      } else {
        EXPECT_TRUE(StartsWith(tab_title, tab_prefix, true));

        // The prerender tab should now be a normal tab but the title should be
        // the same. Depending on timing, there may be more than one of these.
        const string16 tab_page_title = tab_title.substr(tab_prefix.length());
        if (prerender_page_title.compare(tab_page_title) == 0)
          ++num_tabs_with_prerender_page_title;
      }
    }
  }
  EXPECT_EQ(0, num_prerender_tabs);

  // We may have deleted the prerender tab, but the swapped in tab should be
  // active.
  EXPECT_GE(num_tabs_with_prerender_page_title, 1);
  EXPECT_LE(num_tabs_with_prerender_page_title, 2);
}

// Checks that audio loads are deferred on prerendering.
// Times out under AddressSanitizer, see http://crbug.com/108402
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, DISABLED_PrerenderHTML5Audio) {
  PrerenderTestURL("files/prerender/prerender_html5_audio.html",
                  FINAL_STATUS_USED,
                  1);
  NavigateToDestUrlAndWaitForPassTitle();
}

// Checks that audio loads are deferred on prerendering and played back when
// the prerender is swapped in if autoplay is set.
// Periodically fails on chrome-os.  See http://crbug.com/145263
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       DISABLED_PrerenderHTML5AudioAutoplay) {
  PrerenderTestURL("files/prerender/prerender_html5_audio_autoplay.html",
                   FINAL_STATUS_USED,
                   1);
  NavigateToDestUrlAndWaitForPassTitle();
}

// Checks that audio loads are deferred on prerendering and played back when
// the prerender is swapped in if js starts playing.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       DISABLED_PrerenderHTML5AudioJsplay) {
  PrerenderTestURL("files/prerender/prerender_html5_audio_jsplay.html",
                   FINAL_STATUS_USED,
                   1);
  NavigateToDestUrlAndWaitForPassTitle();
}

// Checks that video loads are deferred on prerendering.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, DISABLED_PrerenderHTML5Video) {
  PrerenderTestURL("files/prerender/prerender_html5_video.html",
                   FINAL_STATUS_USED,
                   1);
  NavigateToDestUrlAndWaitForPassTitle();
}

// Checks that video tags inserted by javascript are deferred and played
// correctly on swap in.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       DISABLED_PrerenderHTML5VideoJs) {
  PrerenderTestURL("files/prerender/prerender_html5_video_script.html",
                   FINAL_STATUS_USED,
                   1);
  NavigateToDestUrlAndWaitForPassTitle();
}

// Checks for correct network events by using a busy sleep the javascript.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       DISABLED_PrerenderHTML5VideoNetwork) {
  PrerenderTestURL("files/prerender/prerender_html5_video_network.html",
                   FINAL_STATUS_USED,
                   1,
                   true);
  NavigateToDestUrlAndWaitForPassTitle();
}

// Checks that scripts can retrieve the correct window size while prerendering.
#if defined(TOOLKIT_VIEWS)
// TODO(beng): Widget hierarchy split causes this to fail http://crbug.com/82363
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, DISABLED_PrerenderWindowSize) {
#else
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderWindowSize) {
#endif
  PrerenderTestURL("files/prerender/prerender_size.html",
                   FINAL_STATUS_USED,
                   1);
  NavigateToDestURL();
}

// Flakily times out: http://crbug.com/171546
// Checks that prerenderers will terminate when the RenderView crashes.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, DISABLED_PrerenderRendererCrash) {
  PrerenderTestURL("files/prerender/prerender_page.html",
                   FINAL_STATUS_RENDERER_CRASHED,
                   1);

  // Navigate to about:crash and then wait for the renderer to crash.
  ASSERT_TRUE(GetPrerenderContents());
  ASSERT_TRUE(GetPrerenderContents()->prerender_contents());
  GetPrerenderContents()->prerender_contents()->GetController().
      LoadURL(
          GURL(content::kChromeUICrashURL),
          content::Referrer(),
          content::PAGE_TRANSITION_TYPED,
          std::string());
  content::RunMessageLoop();
}

IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       PrerenderPageWithFragment) {
  PrerenderTestURL("files/prerender/prerender_page.html#fragment",
                   FINAL_STATUS_USED,
                   1);

  ChannelDestructionWatcher channel_close_watcher;
  channel_close_watcher.WatchChannel(browser()->tab_strip_model()->
      GetActiveWebContents()->GetRenderProcessHost());
  NavigateToDestURL();
  channel_close_watcher.WaitForChannelClose();

  ASSERT_TRUE(IsEmptyPrerenderLinkManager());
}

// Checks that we correctly use a prerendered page when navigating to a
// fragment.
// DISABLED: http://crbug.com/84154
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       DISABLED_PrerenderPageNavigateFragment) {
  PrerenderTestURL("files/prerender/no_prerender_page.html",
                   FINAL_STATUS_APP_TERMINATING,
                   1);
  NavigateToURL("files/prerender/no_prerender_page.html#fragment");
}

// Checks that we correctly use a prerendered page when we prerender a fragment
// but navigate to the main page.
// http://crbug.com/83901
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       DISABLED_PrerenderFragmentNavigatePage) {
  PrerenderTestURL("files/prerender/no_prerender_page.html#fragment",
                   FINAL_STATUS_APP_TERMINATING,
                   1);
  NavigateToURL("files/prerender/no_prerender_page.html");
}

// Checks that we correctly use a prerendered page when we prerender a fragment
// but navigate to a different fragment on the same page.
// DISABLED: http://crbug.com/84154
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       DISABLED_PrerenderFragmentNavigateFragment) {
  PrerenderTestURL("files/prerender/no_prerender_page.html#other_fragment",
                   FINAL_STATUS_APP_TERMINATING,
                   1);
  NavigateToURL("files/prerender/no_prerender_page.html#fragment");
}

// Checks that we correctly use a prerendered page when the page uses a client
// redirect to refresh from a fragment on the same page.
// http://crbug.com/83901
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       DISABLED_PrerenderClientRedirectFromFragment) {
  PrerenderTestURL(
      CreateClientRedirect("files/prerender/no_prerender_page.html#fragment"),
      FINAL_STATUS_APP_TERMINATING,
      2);
  NavigateToURL("files/prerender/no_prerender_page.html");
}

// Checks that we correctly use a prerendered page when the page uses a client
// redirect to refresh to a fragment on the same page.
// DISABLED: http://crbug.com/84154
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       DISABLED_PrerenderClientRedirectToFragment) {
  PrerenderTestURL(
      CreateClientRedirect("files/prerender/no_prerender_page.html"),
      FINAL_STATUS_APP_TERMINATING,
      2);
  NavigateToURL("files/prerender/no_prerender_page.html#fragment");
}

// Checks that we correctly use a prerendered page when the page uses JS to set
// the window.location.hash to a fragment on the same page.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       PrerenderPageChangeFragmentLocationHash) {
  PrerenderTestURL("files/prerender/prerender_fragment_location_hash.html",
                   FINAL_STATUS_USED,
                   1);
  NavigateToURL("files/prerender/prerender_fragment_location_hash.html");
}

// Checks that prerendering a PNG works correctly.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderImagePng) {
  DisableJavascriptCalls();
  PrerenderTestURL("files/prerender/image.png", FINAL_STATUS_USED, 1);
  NavigateToDestURL();
}

// Checks that prerendering a JPG works correctly.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderImageJpeg) {
  DisableJavascriptCalls();
  PrerenderTestURL("files/prerender/image.jpeg", FINAL_STATUS_USED, 1);
  NavigateToDestURL();
}

// Checks that a prerender of a CRX will result in a cancellation due to
// download.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderCrx) {
  PrerenderTestURL("files/prerender/extension.crx", FINAL_STATUS_DOWNLOAD, 1);
}

// Checks that xhr GET requests allow prerenders.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderXhrGet) {
  PrerenderTestURL("files/prerender/prerender_xhr_get.html",
                   FINAL_STATUS_USED,
                   1);
  NavigateToDestURL();
}

// Checks that xhr HEAD requests allow prerenders.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderXhrHead) {
  PrerenderTestURL("files/prerender/prerender_xhr_head.html",
                   FINAL_STATUS_USED,
                   1);
  NavigateToDestURL();
}

// Checks that xhr OPTIONS requests allow prerenders.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderXhrOptions) {
  PrerenderTestURL("files/prerender/prerender_xhr_options.html",
                   FINAL_STATUS_USED,
                   1);
  NavigateToDestURL();
}

// Checks that xhr TRACE requests allow prerenders.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderXhrTrace) {
  PrerenderTestURL("files/prerender/prerender_xhr_trace.html",
                   FINAL_STATUS_USED,
                   1);
  NavigateToDestURL();
}

// Checks that xhr POST requests allow prerenders.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, DISABLED_PrerenderXhrPost) {
  PrerenderTestURL("files/prerender/prerender_xhr_post.html",
                   FINAL_STATUS_USED,
                   1);
  NavigateToDestURL();
}

// Checks that xhr PUT cancels prerenders.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderXhrPut) {
  PrerenderTestURL("files/prerender/prerender_xhr_put.html",
                   FINAL_STATUS_INVALID_HTTP_METHOD,
                   1);
}

// Checks that xhr DELETE cancels prerenders.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderXhrDelete) {
  PrerenderTestURL("files/prerender/prerender_xhr_delete.html",
                   FINAL_STATUS_INVALID_HTTP_METHOD,
                   1);
}

// Checks that a top-level page which would trigger an SSL error is canceled.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderSSLErrorTopLevel) {
  net::SpawnedTestServer::SSLOptions ssl_options;
  ssl_options.server_certificate =
      net::SpawnedTestServer::SSLOptions::CERT_MISMATCHED_NAME;
    net::SpawnedTestServer https_server(
        net::SpawnedTestServer::TYPE_HTTPS, ssl_options,
        base::FilePath(FILE_PATH_LITERAL("chrome/test/data")));
  ASSERT_TRUE(https_server.Start());
  GURL https_url = https_server.GetURL("files/prerender/prerender_page.html");
  PrerenderTestURL(https_url,
                   FINAL_STATUS_SSL_ERROR,
                   1);
}

// Checks that an SSL error that comes from a subresource does not cancel
// the page. Non-main-frame requests are simply cancelled if they run into
// an SSL problem.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderSSLErrorSubresource) {
  net::SpawnedTestServer::SSLOptions ssl_options;
  ssl_options.server_certificate =
      net::SpawnedTestServer::SSLOptions::CERT_MISMATCHED_NAME;
    net::SpawnedTestServer https_server(
        net::SpawnedTestServer::TYPE_HTTPS, ssl_options,
        base::FilePath(FILE_PATH_LITERAL("chrome/test/data")));
  ASSERT_TRUE(https_server.Start());
  GURL https_url = https_server.GetURL("files/prerender/image.jpeg");
  std::vector<net::SpawnedTestServer::StringPair> replacement_text;
  replacement_text.push_back(
      std::make_pair("REPLACE_WITH_IMAGE_URL", https_url.spec()));
  std::string replacement_path;
  ASSERT_TRUE(net::SpawnedTestServer::GetFilePathWithReplacements(
      "files/prerender/prerender_with_image.html",
      replacement_text,
      &replacement_path));
  PrerenderTestURL(replacement_path, FINAL_STATUS_USED, 1);
  NavigateToDestURL();
}

// Checks that an SSL error that comes from an iframe does not cancel
// the page. Non-main-frame requests are simply cancelled if they run into
// an SSL problem.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderSSLErrorIframe) {
  net::SpawnedTestServer::SSLOptions ssl_options;
  ssl_options.server_certificate =
      net::SpawnedTestServer::SSLOptions::CERT_MISMATCHED_NAME;
    net::SpawnedTestServer https_server(
        net::SpawnedTestServer::TYPE_HTTPS, ssl_options,
        base::FilePath(FILE_PATH_LITERAL("chrome/test/data")));
  ASSERT_TRUE(https_server.Start());
  GURL https_url = https_server.GetURL(
      "files/prerender/prerender_embedded_content.html");
  std::vector<net::SpawnedTestServer::StringPair> replacement_text;
  replacement_text.push_back(
      std::make_pair("REPLACE_WITH_URL", https_url.spec()));
  std::string replacement_path;
  ASSERT_TRUE(net::SpawnedTestServer::GetFilePathWithReplacements(
      "files/prerender/prerender_with_iframe.html",
      replacement_text,
      &replacement_path));
  PrerenderTestURL(replacement_path, FINAL_STATUS_USED, 1);
  NavigateToDestURL();
}

// Checks that we cancel correctly when window.print() is called.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderPrint) {
  PrerenderTestURL("files/prerender/prerender_print.html",
                   FINAL_STATUS_WINDOW_PRINT,
                   1);
}

// Checks that if a page is opened in a new window by javascript and both the
// pages are in the same domain, the prerendered page is not used.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       PrerenderSameDomainWindowOpenerWindowOpen) {
  PrerenderTestURL("files/prerender/prerender_page.html",
                   FINAL_STATUS_APP_TERMINATING,
                   1);
  OpenDestURLViaWindowOpen();
}

// Checks that if a page is opened due to click on a href with target="_blank"
// and both pages are in the same domain the prerendered page is not used.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       PrerenderSameDomainWindowOpenerClickTarget) {
  PrerenderTestURL("files/prerender/prerender_page.html",
                   FINAL_STATUS_APP_TERMINATING,
                   1);
  OpenDestURLViaClickTarget();
}

// Checks that a top-level page which would normally request an SSL client
// certificate will never be seen since it's an https top-level resource.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderSSLClientCertTopLevel) {
  net::SpawnedTestServer::SSLOptions ssl_options;
  ssl_options.request_client_certificate = true;
    net::SpawnedTestServer https_server(
        net::SpawnedTestServer::TYPE_HTTPS, ssl_options,
        base::FilePath(FILE_PATH_LITERAL("chrome/test/data")));
  ASSERT_TRUE(https_server.Start());
  GURL https_url = https_server.GetURL("files/prerender/prerender_page.html");
  PrerenderTestURL(https_url, FINAL_STATUS_SSL_CLIENT_CERTIFICATE_REQUESTED, 1);
}

// Checks that an SSL Client Certificate request that originates from a
// subresource will cancel the prerendered page.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       PrerenderSSLClientCertSubresource) {
  net::SpawnedTestServer::SSLOptions ssl_options;
  ssl_options.request_client_certificate = true;
    net::SpawnedTestServer https_server(
        net::SpawnedTestServer::TYPE_HTTPS, ssl_options,
        base::FilePath(FILE_PATH_LITERAL("chrome/test/data")));
  ASSERT_TRUE(https_server.Start());
  GURL https_url = https_server.GetURL("files/prerender/image.jpeg");
  std::vector<net::SpawnedTestServer::StringPair> replacement_text;
  replacement_text.push_back(
      std::make_pair("REPLACE_WITH_IMAGE_URL", https_url.spec()));
  std::string replacement_path;
  ASSERT_TRUE(net::SpawnedTestServer::GetFilePathWithReplacements(
      "files/prerender/prerender_with_image.html",
      replacement_text,
      &replacement_path));
  PrerenderTestURL(replacement_path,
                   FINAL_STATUS_SSL_CLIENT_CERTIFICATE_REQUESTED,
                   1);
}

// Checks that an SSL Client Certificate request that originates from an
// iframe will cancel the prerendered page.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderSSLClientCertIframe) {
  net::SpawnedTestServer::SSLOptions ssl_options;
  ssl_options.request_client_certificate = true;
    net::SpawnedTestServer https_server(
        net::SpawnedTestServer::TYPE_HTTPS, ssl_options,
        base::FilePath(FILE_PATH_LITERAL("chrome/test/data")));
  ASSERT_TRUE(https_server.Start());
  GURL https_url = https_server.GetURL(
      "files/prerender/prerender_embedded_content.html");
  std::vector<net::SpawnedTestServer::StringPair> replacement_text;
  replacement_text.push_back(
      std::make_pair("REPLACE_WITH_URL", https_url.spec()));
  std::string replacement_path;
  ASSERT_TRUE(net::SpawnedTestServer::GetFilePathWithReplacements(
      "files/prerender/prerender_with_iframe.html",
      replacement_text,
      &replacement_path));
  PrerenderTestURL(replacement_path,
                   FINAL_STATUS_SSL_CLIENT_CERTIFICATE_REQUESTED,
                   1);
}

#if defined(FULL_SAFE_BROWSING)
// Ensures that we do not prerender pages with a safe browsing
// interstitial.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderSafeBrowsingTopLevel) {
  GURL url = test_server()->GetURL("files/prerender/prerender_page.html");
  GetFakeSafeBrowsingDatabaseManager()->SetThreatTypeForUrl(
      url, SB_THREAT_TYPE_URL_MALWARE);
  PrerenderTestURL("files/prerender/prerender_page.html",
                   FINAL_STATUS_SAFE_BROWSING, 1);
}

// Ensures that server redirects to a malware page will cancel prerenders.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       PrerenderSafeBrowsingServerRedirect) {
  GURL url = test_server()->GetURL("files/prerender/prerender_page.html");
  GetFakeSafeBrowsingDatabaseManager()->SetThreatTypeForUrl(
      url, SB_THREAT_TYPE_URL_MALWARE);
  PrerenderTestURL(CreateServerRedirect("files/prerender/prerender_page.html"),
                   FINAL_STATUS_SAFE_BROWSING,
                   1);
}

// Ensures that client redirects to a malware page will cancel prerenders.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       PrerenderSafeBrowsingClientRedirect) {
  GURL url = test_server()->GetURL("files/prerender/prerender_page.html");
  GetFakeSafeBrowsingDatabaseManager()->SetThreatTypeForUrl(
      url, SB_THREAT_TYPE_URL_MALWARE);
  PrerenderTestURL(CreateClientRedirect("files/prerender/prerender_page.html"),
                   FINAL_STATUS_SAFE_BROWSING,
                   1);
}

// Ensures that we do not prerender pages which have a malware subresource.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderSafeBrowsingSubresource) {
  GURL image_url = test_server()->GetURL("files/prerender/image.jpeg");
  GetFakeSafeBrowsingDatabaseManager()->SetThreatTypeForUrl(
      image_url, SB_THREAT_TYPE_URL_MALWARE);
  std::vector<net::SpawnedTestServer::StringPair> replacement_text;
  replacement_text.push_back(
      std::make_pair("REPLACE_WITH_IMAGE_URL", image_url.spec()));
  std::string replacement_path;
  ASSERT_TRUE(net::SpawnedTestServer::GetFilePathWithReplacements(
      "files/prerender/prerender_with_image.html",
      replacement_text,
      &replacement_path));
  PrerenderTestURL(replacement_path,
                   FINAL_STATUS_SAFE_BROWSING,
                   1);
}

// Ensures that we do not prerender pages which have a malware iframe.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderSafeBrowsingIframe) {
  GURL iframe_url = test_server()->GetURL(
      "files/prerender/prerender_embedded_content.html");
  GetFakeSafeBrowsingDatabaseManager()->SetThreatTypeForUrl(
      iframe_url, SB_THREAT_TYPE_URL_MALWARE);
  std::vector<net::SpawnedTestServer::StringPair> replacement_text;
  replacement_text.push_back(
      std::make_pair("REPLACE_WITH_URL", iframe_url.spec()));
  std::string replacement_path;
  ASSERT_TRUE(net::SpawnedTestServer::GetFilePathWithReplacements(
      "files/prerender/prerender_with_iframe.html",
      replacement_text,
      &replacement_path));
  PrerenderTestURL(replacement_path,
                   FINAL_STATUS_SAFE_BROWSING,
                   1);
}

#endif

// Checks that a local storage read will not cause prerender to fail.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderLocalStorageRead) {
  PrerenderTestURL("files/prerender/prerender_localstorage_read.html",
                   FINAL_STATUS_USED,
                   1);
  NavigateToDestURL();
}

// Checks that a local storage write will not cause prerender to fail.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderLocalStorageWrite) {
  PrerenderTestURL("files/prerender/prerender_localstorage_write.html",
                   FINAL_STATUS_USED,
                   1);
  NavigateToDestURL();
}

// Checks that the favicon is properly loaded on prerender.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       DISABLED_PrerenderFavicon) {
  PrerenderTestURL("files/prerender/prerender_favicon.html",
                   FINAL_STATUS_USED,
                   1);
  TestPrerenderContents* prerender_contents = GetPrerenderContents();
  ASSERT_TRUE(prerender_contents != NULL);
  content::WindowedNotificationObserver favicon_update_watcher(
      chrome::NOTIFICATION_FAVICON_UPDATED,
      content::Source<WebContents>(prerender_contents->prerender_contents()));
  NavigateToDestURL();
  favicon_update_watcher.Wait();
}

// Checks that when a prerendered page is swapped in to a referring page, the
// unload handlers on the referring page are executed.
// Fails about 50% on CrOS, 5-10% on linux, win, mac. http://crbug.com/128986
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, DISABLED_PrerenderUnload) {
  set_loader_path("files/prerender/prerender_loader_with_unload.html");
  PrerenderTestURL("files/prerender/prerender_page.html", FINAL_STATUS_USED, 1);
  string16 expected_title = ASCIIToUTF16("Unloaded");
  content::TitleWatcher title_watcher(
      current_browser()->tab_strip_model()->GetActiveWebContents(),
      expected_title);
  NavigateToDestURL();
  EXPECT_EQ(expected_title, title_watcher.WaitAndGetTitle());
}

// Checks that when the history is cleared, prerendering is cancelled and
// prerendering history is cleared.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderClearHistory) {
  PrerenderTestURL("files/prerender/prerender_page.html",
                   FINAL_STATUS_CACHE_OR_HISTORY_CLEARED,
                   1);

  // Post a task to clear the history, and run the message loop until it
  // destroys the prerender.
  base::MessageLoop::current()->PostTask(
      FROM_HERE,
      base::Bind(&ClearBrowsingData, current_browser(),
                 BrowsingDataRemover::REMOVE_HISTORY));
  content::RunMessageLoop();

  // Make sure prerender history was cleared.
  EXPECT_EQ(0, GetHistoryLength());
}

// Checks that when the cache is cleared, prerenders are cancelled but
// prerendering history is not cleared.
// Flaky/times out on linux_aura, win, mac - http://crbug.com/270948
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, DISABLED_PrerenderClearCache) {
  PrerenderTestURL("files/prerender/prerender_page.html",
                   FINAL_STATUS_CACHE_OR_HISTORY_CLEARED,
                   1);

  // Post a task to clear the cache, and run the message loop until it
  // destroys the prerender.
  base::MessageLoop::current()->PostTask(FROM_HERE,
      base::Bind(&ClearBrowsingData, current_browser(),
                 BrowsingDataRemover::REMOVE_CACHE));
  content::RunMessageLoop();

  // Make sure prerender history was not cleared.  Not a vital behavior, but
  // used to compare with PrerenderClearHistory test.
  EXPECT_EQ(1, GetHistoryLength());
}

IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderCancelAll) {
  PrerenderTestURL("files/prerender/prerender_page.html",
                   FINAL_STATUS_CANCELLED,
                   1);
  // Post a task to cancel all the prerenders.
  base::MessageLoop::current()->PostTask(
      FROM_HERE, base::Bind(&CancelAllPrerenders, GetPrerenderManager()));
  content::RunMessageLoop();
  EXPECT_TRUE(GetPrerenderContents() == NULL);
}

IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderEvents) {
  PrerenderTestURL("files/prerender/prerender_page.html",
                   FINAL_STATUS_CANCELLED, 1);
  EXPECT_TRUE(DidReceivePrerenderStartEventForLinkNumber(0));
  EXPECT_TRUE(DidReceivePrerenderLoadEventForLinkNumber(0));
  EXPECT_FALSE(DidReceivePrerenderStopEventForLinkNumber(0));

  base::MessageLoop::current()->PostTask(
      FROM_HERE, base::Bind(&CancelAllPrerenders, GetPrerenderManager()));
  content::RunMessageLoop();

  EXPECT_TRUE(DidReceivePrerenderStartEventForLinkNumber(0));
  EXPECT_TRUE(DidReceivePrerenderStopEventForLinkNumber(0));
  EXPECT_FALSE(HadPrerenderEventErrors());
}

// Cancels the prerender of a page with its own prerender.  The second prerender
// should never be started.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       PrerenderCancelPrerenderWithPrerender) {
  PrerenderTestURL("files/prerender/prerender_infinite_a.html",
                   FINAL_STATUS_CANCELLED,
                   1);
  // Post a task to cancel all the prerenders.
  base::MessageLoop::current()->PostTask(
      FROM_HERE, base::Bind(&CancelAllPrerenders, GetPrerenderManager()));
  content::RunMessageLoop();
  EXPECT_TRUE(GetPrerenderContents() == NULL);
}

// PrerenderBrowserTest.PrerenderEventsNoLoad may pass flakily on regression,
// so please be aggressive about filing bugs when this test is failing.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderEventsNoLoad) {
  // This should be canceled.
  PrerenderTestURL("files/prerender/prerender_http_auth_container.html",
                   FINAL_STATUS_AUTH_NEEDED,
                   1);
  EXPECT_TRUE(DidReceivePrerenderStartEventForLinkNumber(0));
  EXPECT_FALSE(DidReceivePrerenderLoadEventForLinkNumber(0));
  EXPECT_FALSE(DidReceivePrerenderStopEventForLinkNumber(0));
  EXPECT_FALSE(HadPrerenderEventErrors());
}

// Prerendering and history tests.
// The prerendered page is navigated to in several ways [navigate via
// omnibox, click on link, key-modified click to open in background tab, etc],
// followed by a navigation to another page from the prerendered page, followed
// by a back navigation.

IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       DISABLED_PrerenderNavigateClickGoBack) {
  PrerenderTestURL("files/prerender/prerender_page_with_link.html",
                   FINAL_STATUS_USED,
                   1);
  NavigateToDestURL();
  ClickToNextPageAfterPrerender();
  GoBackToPrerender();
}

// Disabled due to timeouts on commit queue.
// http://crbug.com/121130
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       DISABLED_PrerenderNavigateNavigateGoBack) {
  PrerenderTestURL("files/prerender/prerender_page_with_link.html",
                   FINAL_STATUS_USED,
                   1);
  NavigateToDestURL();
  NavigateToNextPageAfterPrerender();
  GoBackToPrerender();
}

IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       DISABLED_PrerenderClickClickGoBack) {
  PrerenderTestURL("files/prerender/prerender_page_with_link.html",
                   FINAL_STATUS_USED,
                   1);
  OpenDestURLViaClick();
  ClickToNextPageAfterPrerender();
  GoBackToPrerender();
}

// Disabled due to timeouts on commit queue.
// http://crbug.com/121130
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       DISABLED_PrerenderClickNavigateGoBack) {
  PrerenderTestURL("files/prerender/prerender_page_with_link.html",
                   FINAL_STATUS_USED,
                   1);
  OpenDestURLViaClick();
  NavigateToNextPageAfterPrerender();
  GoBackToPrerender();
}

IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderClickNewWindow) {
  PrerenderTestURL("files/prerender/prerender_page_with_link.html",
                   FINAL_STATUS_APP_TERMINATING,
                   1);
  OpenDestURLViaClickNewWindow();
}

IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderClickNewForegroundTab) {
  PrerenderTestURL("files/prerender/prerender_page_with_link.html",
                   FINAL_STATUS_APP_TERMINATING,
                   1);
  OpenDestURLViaClickNewForegroundTab();
}

IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       DISABLED_PrerenderClickNewBackgroundTab) {
  PrerenderTestURL("files/prerender/prerender_page_with_link.html",
                   FINAL_STATUS_APP_TERMINATING,
                   1);
  OpenDestURLViaClickNewBackgroundTab();
}

IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       NavigateToPrerenderedPageWhenDevToolsAttached) {
  DisableJavascriptCalls();
  WebContents* web_contents =
      current_browser()->tab_strip_model()->GetActiveWebContents();
  scoped_refptr<DevToolsAgentHost> agent(DevToolsAgentHost::GetOrCreateFor(
      web_contents->GetRenderViewHost()));
  DevToolsManager* manager = DevToolsManager::GetInstance();
  FakeDevToolsClientHost client_host;
  manager->RegisterDevToolsClientHostFor(agent.get(), &client_host);
  const char* url = "files/prerender/prerender_page.html";
  PrerenderTestURL(url, FINAL_STATUS_DEVTOOLS_ATTACHED, 1);
  NavigateToURL(url);
  manager->ClientHostClosing(&client_host);
}

// Validate that the sessionStorage namespace remains the same when swapping
// in a prerendered page.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       DISABLED_PrerenderSessionStorage) {
  set_loader_path("files/prerender/prerender_loader_with_session_storage.html");
  PrerenderTestURL(GetCrossDomainTestUrl("files/prerender/prerender_page.html"),
                   FINAL_STATUS_USED,
                   1);
  NavigateToDestURL();
  GoBackToPageBeforePrerender();
}

#if defined(OS_MACOSX)
// http://crbug.com/142535 - Times out on Chrome Mac release builder
#define MAYBE_ControlGroup DISABLED_ControlGroup
#else
#define MAYBE_ControlGroup ControlGroup
#endif
// Checks that the control group works.  A JS alert cannot be detected in the
// control group.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, MAYBE_ControlGroup) {
  RestorePrerenderMode restore_prerender_mode;
  PrerenderManager::SetMode(
      PrerenderManager::PRERENDER_MODE_EXPERIMENT_CONTROL_GROUP);
  PrerenderTestURL("files/prerender/prerender_alert_before_onload.html",
                   FINAL_STATUS_WOULD_HAVE_BEEN_USED, 0);
  NavigateToDestURL();
}

// Make sure that the MatchComplete dummy works in the normal case.  Once
// a prerender is cancelled because of a script, a dummy must be created to
// account for the MatchComplete case, and it must have a final status of
// FINAL_STATUS_WOULD_HAVE_BEEN_USED.
#if defined(OS_MACOSX)
// http://crbug.com/142912 - Times out on Chrome Mac release builder
#define MAYBE_MatchCompleteDummy DISABLED_MatchCompleteDummy
#else
#define MAYBE_MatchCompleteDummy MatchCompleteDummy
#endif
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, MAYBE_MatchCompleteDummy) {
  std::deque<FinalStatus> expected_final_status_queue;
  expected_final_status_queue.push_back(FINAL_STATUS_JAVASCRIPT_ALERT);
  expected_final_status_queue.push_back(FINAL_STATUS_WOULD_HAVE_BEEN_USED);
  PrerenderTestURL("files/prerender/prerender_alert_before_onload.html",
                   expected_final_status_queue, 1);
  NavigateToDestURL();
}

class PrerenderBrowserTestWithNaCl : public PrerenderBrowserTest {
 public:
  PrerenderBrowserTestWithNaCl() {}
  virtual ~PrerenderBrowserTestWithNaCl() {}

  virtual void SetUpCommandLine(CommandLine* command_line) OVERRIDE {
    PrerenderBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch(switches::kEnableNaCl);
  }
};

// Check that NaCl plugins work when enabled, with prerendering.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTestWithNaCl,
                       PrerenderNaClPluginEnabled) {
#if defined(OS_WIN) && defined(USE_ASH)
  // Disable this test in Metro+Ash for now (http://crbug.com/262796).
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kAshBrowserTests))
    return;
#endif

  PrerenderTestURL("files/prerender/prerender_plugin_nacl_enabled.html",
                   FINAL_STATUS_USED,
                   1);
  NavigateToDestURL();

  // To avoid any chance of a race, we have to let the script send its response
  // asynchronously.
  WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  bool display_test_result = false;
  ASSERT_TRUE(content::ExecuteScriptAndExtractBool(web_contents,
                                                   "DidDisplayReallyPass()",
                                                   &display_test_result));
  ASSERT_TRUE(display_test_result);
}

// Checks that the referrer policy is used when prerendering.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderReferrerPolicy) {
  set_loader_path("files/prerender/prerender_loader_with_referrer_policy.html");
  PrerenderTestURL("files/prerender/prerender_referrer_policy.html",
                   FINAL_STATUS_USED,
                   1);
  NavigateToDestURL();
}

// Checks that the referrer policy is used when prerendering on HTTPS.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       DISABLED_PrerenderSSLReferrerPolicy) {
  set_use_https_src(true);
  set_loader_path("files/prerender/prerender_loader_with_referrer_policy.html");
  PrerenderTestURL("files/prerender/prerender_referrer_policy.html",
                   FINAL_STATUS_USED,
                   1);
  NavigateToDestURL();
}

// Test interaction of the webNavigation and tabs API with prerender.
class PrerenderBrowserTestWithExtensions : public PrerenderBrowserTest,
                                           public ExtensionApiTest {
 public:
  virtual void SetUp() OVERRIDE {
    PrerenderBrowserTest::SetUp();
  }

  virtual void SetUpCommandLine(CommandLine* command_line) OVERRIDE {
    PrerenderBrowserTest::SetUpCommandLine(command_line);
    ExtensionApiTest::SetUpCommandLine(command_line);
  }

  virtual void SetUpInProcessBrowserTestFixture() OVERRIDE {
    PrerenderBrowserTest::SetUpInProcessBrowserTestFixture();
    ExtensionApiTest::SetUpInProcessBrowserTestFixture();
  }

  virtual void TearDownInProcessBrowserTestFixture() OVERRIDE {
    PrerenderBrowserTest::TearDownInProcessBrowserTestFixture();
    ExtensionApiTest::TearDownInProcessBrowserTestFixture();
  }

  virtual void SetUpOnMainThread() OVERRIDE {
    PrerenderBrowserTest::SetUpOnMainThread();
  }
};

// http://crbug.com/177163
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTestWithExtensions,
                       DISABLED_WebNavigation) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  extensions::FrameNavigationState::set_allow_extension_scheme(true);

  CommandLine::ForCurrentProcess()->AppendSwitch(
      extensions::switches::kAllowLegacyExtensionManifests);

  // Wait for the extension to set itself up and return control to us.
  ASSERT_TRUE(
      RunExtensionSubtest("webnavigation", "test_prerender.html")) << message_;

  ResultCatcher catcher;

  PrerenderTestURL("files/prerender/prerender_page.html", FINAL_STATUS_USED, 1);

  ChannelDestructionWatcher channel_close_watcher;
  channel_close_watcher.WatchChannel(browser()->tab_strip_model()->
      GetActiveWebContents()->GetRenderProcessHost());
  NavigateToDestURL();
  channel_close_watcher.WaitForChannelClose();

  ASSERT_TRUE(IsEmptyPrerenderLinkManager());
  ASSERT_TRUE(catcher.GetNextResult()) << catcher.message();
}

// Fails often on Windows dbg bots. http://crbug.com/177163
#if defined(OS_WIN)
#define MAYBE_TabsApi DISABLED_TabsApi
#else
#define MAYBE_TabsApi TabsApi
#endif  // defined(OS_WIN)
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTestWithExtensions, MAYBE_TabsApi) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  extensions::FrameNavigationState::set_allow_extension_scheme(true);

  // Wait for the extension to set itself up and return control to us.
  ASSERT_TRUE(RunExtensionSubtest("tabs/on_replaced", "on_replaced.html"))
      << message_;

  ResultCatcher catcher;

  PrerenderTestURL("files/prerender/prerender_page.html", FINAL_STATUS_USED, 1);

  ChannelDestructionWatcher channel_close_watcher;
  channel_close_watcher.WatchChannel(browser()->tab_strip_model()->
      GetActiveWebContents()->GetRenderProcessHost());
  NavigateToDestURL();
  channel_close_watcher.WaitForChannelClose();

  ASSERT_TRUE(IsEmptyPrerenderLinkManager());
  ASSERT_TRUE(catcher.GetNextResult()) << catcher.message();
}

// Checks that non-http/https/chrome-extension subresource cancels the
// prerender.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       DISABLED_PrerenderCancelSubresourceUnsupportedScheme) {
  GURL image_url = GURL("invalidscheme://www.google.com/test.jpg");
  std::vector<net::SpawnedTestServer::StringPair> replacement_text;
  replacement_text.push_back(
      std::make_pair("REPLACE_WITH_IMAGE_URL", image_url.spec()));
  std::string replacement_path;
  ASSERT_TRUE(net::SpawnedTestServer::GetFilePathWithReplacements(
      "files/prerender/prerender_with_image.html",
      replacement_text,
      &replacement_path));
  PrerenderTestURL(replacement_path, FINAL_STATUS_UNSUPPORTED_SCHEME, 1);
  NavigateToDestURL();
}

// Ensure that about:blank is permitted for any subresource.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       PrerenderAllowAboutBlankSubresource) {
  GURL image_url = GURL("about:blank");
  std::vector<net::SpawnedTestServer::StringPair> replacement_text;
  replacement_text.push_back(
      std::make_pair("REPLACE_WITH_IMAGE_URL", image_url.spec()));
  std::string replacement_path;
  ASSERT_TRUE(net::SpawnedTestServer::GetFilePathWithReplacements(
      "files/prerender/prerender_with_image.html",
      replacement_text,
      &replacement_path));
  PrerenderTestURL(replacement_path, FINAL_STATUS_USED, 1);
  NavigateToDestURL();
}

// Checks that non-http/https/chrome-extension subresource cancels the prerender
// on redirect.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       PrerenderCancelSubresourceRedirectUnsupportedScheme) {
  GURL image_url = test_server()->GetURL(
      CreateServerRedirect("invalidscheme://www.google.com/test.jpg"));
  std::vector<net::SpawnedTestServer::StringPair> replacement_text;
  replacement_text.push_back(
      std::make_pair("REPLACE_WITH_IMAGE_URL", image_url.spec()));
  std::string replacement_path;
  ASSERT_TRUE(net::SpawnedTestServer::GetFilePathWithReplacements(
      "files/prerender/prerender_with_image.html",
      replacement_text,
      &replacement_path));
  PrerenderTestURL(replacement_path, FINAL_STATUS_UNSUPPORTED_SCHEME, 1);
  NavigateToDestURL();
}

// Checks that chrome-extension subresource does not cancel the prerender.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       PrerenderKeepSubresourceExtensionScheme) {
  GURL image_url = GURL("chrome-extension://abcdefg/test.jpg");
  std::vector<net::SpawnedTestServer::StringPair> replacement_text;
  replacement_text.push_back(
      std::make_pair("REPLACE_WITH_IMAGE_URL", image_url.spec()));
  std::string replacement_path;
  ASSERT_TRUE(net::SpawnedTestServer::GetFilePathWithReplacements(
      "files/prerender/prerender_with_image.html",
      replacement_text,
      &replacement_path));
  PrerenderTestURL(replacement_path, FINAL_STATUS_USED, 1);
  NavigateToDestURL();
}

// Checks that redirect to chrome-extension subresource does not cancel the
// prerender.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       PrerenderKeepSubresourceRedirectExtensionScheme) {
  GURL image_url = test_server()->GetURL(
      CreateServerRedirect("chrome-extension://abcdefg/test.jpg"));
  std::vector<net::SpawnedTestServer::StringPair> replacement_text;
  replacement_text.push_back(
      std::make_pair("REPLACE_WITH_IMAGE_URL", image_url.spec()));
  std::string replacement_path;
  ASSERT_TRUE(net::SpawnedTestServer::GetFilePathWithReplacements(
      "files/prerender/prerender_with_image.html",
      replacement_text,
      &replacement_path));
  PrerenderTestURL(replacement_path, FINAL_STATUS_USED, 1);
  NavigateToDestURL();
}


// Checks that non-http/https main page redirects cancel the prerender.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest,
                       PrerenderCancelMainFrameRedirectUnsupportedScheme) {
  GURL url = test_server()->GetURL(
      CreateServerRedirect("invalidscheme://www.google.com/test.html"));
  PrerenderTestURL(url, FINAL_STATUS_UNSUPPORTED_SCHEME, 1);
  NavigateToDestURL();
}

// Checks that media source video loads are deferred on prerendering.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderHTML5MediaSourceVideo) {
  PrerenderTestURL("files/prerender/prerender_html5_video_media_source.html",
                   FINAL_STATUS_USED,
                   1);
  NavigateToDestUrlAndWaitForPassTitle();
}

// Checks that a prerender that creates an audio stream (via a WebAudioDevice)
// is cancelled.
// http://crbug.com/261489
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, DISABLED_PrerenderWebAudioDevice) {
  PrerenderTestURL("files/prerender/prerender_web_audio_device.html",
                   FINAL_STATUS_CREATING_AUDIO_STREAM, 1);
}

// Checks that prerenders do not swap in to WebContents being captured.
IN_PROC_BROWSER_TEST_F(PrerenderBrowserTest, PrerenderCapturedWebContents) {
  PrerenderTestURL("files/prerender/prerender_page.html",
                   FINAL_STATUS_PAGE_BEING_CAPTURED, 1);
  WebContents* web_contents =
      current_browser()->tab_strip_model()->GetActiveWebContents();
  web_contents->IncrementCapturerCount();
  NavigateToDestURLWithDisposition(CURRENT_TAB, false);
  web_contents->DecrementCapturerCount();
}

}  // namespace prerender
