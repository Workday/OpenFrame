// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// An implementation of BrowserProcess for unit tests that fails for most
// services. By preventing creation of services, we reduce dependencies and
// keep the profile clean. Clients of this class must handle the NULL return
// value, however.

#ifndef CHROME_TEST_BASE_TESTING_BROWSER_PROCESS_H_
#define CHROME_TEST_BASE_TESTING_BROWSER_PROCESS_H_

#include <string>

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browser_process_platform_part.h"

class BackgroundModeManager;
class CRLSetFetcher;
class IOThread;
class MHTMLGenerationManager;
class NotificationUIManager;
class PrefService;
class WatchDogThread;

namespace content {
class NotificationService;
}

namespace policy {
class BrowserPolicyConnector;
class PolicyService;
}

namespace prerender {
class PrerenderTracker;
}

class TestingBrowserProcess : public BrowserProcess {
 public:
  TestingBrowserProcess();
  virtual ~TestingBrowserProcess();

  // Convenience method to get g_browser_process as a TestingBrowserProcess*.
  static TestingBrowserProcess* GetGlobal();

  virtual void ResourceDispatcherHostCreated() OVERRIDE;
  virtual void EndSession() OVERRIDE;
  virtual MetricsService* metrics_service() OVERRIDE;
  virtual IOThread* io_thread() OVERRIDE;
  virtual WatchDogThread* watchdog_thread() OVERRIDE;
  virtual ProfileManager* profile_manager() OVERRIDE;
  virtual PrefService* local_state() OVERRIDE;
  virtual chrome_variations::VariationsService* variations_service() OVERRIDE;
  virtual policy::BrowserPolicyConnector* browser_policy_connector() OVERRIDE;
  virtual policy::PolicyService* policy_service() OVERRIDE;
  virtual IconManager* icon_manager() OVERRIDE;
  virtual GLStringManager* gl_string_manager() OVERRIDE;
  virtual GpuModeManager* gpu_mode_manager() OVERRIDE;
  virtual RenderWidgetSnapshotTaker* GetRenderWidgetSnapshotTaker() OVERRIDE;
  virtual BackgroundModeManager* background_mode_manager() OVERRIDE;
  virtual void set_background_mode_manager_for_test(
      scoped_ptr<BackgroundModeManager> manager) OVERRIDE;
  virtual StatusTray* status_tray() OVERRIDE;
  virtual SafeBrowsingService* safe_browsing_service() OVERRIDE;
  virtual safe_browsing::ClientSideDetectionService*
      safe_browsing_detection_service() OVERRIDE;
  virtual net::URLRequestContextGetter* system_request_context() OVERRIDE;
  virtual BrowserProcessPlatformPart* platform_part() OVERRIDE;

  virtual extensions::EventRouterForwarder*
      extension_event_router_forwarder() OVERRIDE;
  virtual NotificationUIManager* notification_ui_manager() OVERRIDE;
  virtual message_center::MessageCenter* message_center() OVERRIDE;
  virtual IntranetRedirectDetector* intranet_redirect_detector() OVERRIDE;
  virtual AutomationProviderList* GetAutomationProviderList() OVERRIDE;
  virtual void CreateDevToolsHttpProtocolHandler(
      chrome::HostDesktopType host_desktop_type,
      const std::string& ip,
      int port,
      const std::string& frontend_url) OVERRIDE;
  virtual unsigned int AddRefModule() OVERRIDE;
  virtual unsigned int ReleaseModule() OVERRIDE;
  virtual bool IsShuttingDown() OVERRIDE;
  virtual printing::PrintJobManager* print_job_manager() OVERRIDE;
  virtual printing::PrintPreviewDialogController*
      print_preview_dialog_controller() OVERRIDE;
  virtual printing::BackgroundPrintingManager*
      background_printing_manager() OVERRIDE;
  virtual const std::string& GetApplicationLocale() OVERRIDE;
  virtual void SetApplicationLocale(const std::string& app_locale) OVERRIDE;
  virtual DownloadStatusUpdater* download_status_updater() OVERRIDE;
  virtual DownloadRequestLimiter* download_request_limiter() OVERRIDE;

#if (defined(OS_WIN) || defined(OS_LINUX)) && !defined(OS_CHROMEOS)
  virtual void StartAutoupdateTimer() OVERRIDE {}
#endif

  virtual ChromeNetLog* net_log() OVERRIDE;
  virtual prerender::PrerenderTracker* prerender_tracker() OVERRIDE;
  virtual ComponentUpdateService* component_updater() OVERRIDE;
  virtual CRLSetFetcher* crl_set_fetcher() OVERRIDE;
  virtual PnaclComponentInstaller* pnacl_component_installer() OVERRIDE;
  virtual BookmarkPromptController* bookmark_prompt_controller() OVERRIDE;
  virtual chrome::StorageMonitor* storage_monitor() OVERRIDE;
  virtual chrome::MediaFileSystemRegistry*
      media_file_system_registry() OVERRIDE;
  virtual bool created_local_state() const OVERRIDE;

#if defined(ENABLE_WEBRTC)
  virtual WebRtcLogUploader* webrtc_log_uploader() OVERRIDE;
#endif

  // Set the local state for tests. Consumer is responsible for cleaning it up
  // afterwards (using ScopedTestingLocalState, for example).
  void SetLocalState(PrefService* local_state);
  void SetProfileManager(ProfileManager* profile_manager);
  void SetIOThread(IOThread* io_thread);
  void SetBrowserPolicyConnector(policy::BrowserPolicyConnector* connector);
  void SetSafeBrowsingService(SafeBrowsingService* sb_service);
  void SetBookmarkPromptController(BookmarkPromptController* controller);
  void SetSystemRequestContext(net::URLRequestContextGetter* context_getter);
  void SetStorageMonitor(scoped_ptr<chrome::StorageMonitor> storage_monitor);

 private:
  scoped_ptr<content::NotificationService> notification_service_;
  unsigned int module_ref_count_;
  std::string app_locale_;

  // TODO(ios): Add back members as more code is compiled.
#if !defined(OS_IOS)
#if defined(ENABLE_CONFIGURATION_POLICY)
  scoped_ptr<policy::BrowserPolicyConnector> browser_policy_connector_;
#else
  scoped_ptr<policy::PolicyService> policy_service_;
#endif
  scoped_ptr<ProfileManager> profile_manager_;
  scoped_ptr<NotificationUIManager> notification_ui_manager_;

#if defined(ENABLE_FULL_PRINTING)
  scoped_ptr<printing::BackgroundPrintingManager> background_printing_manager_;
  scoped_refptr<printing::PrintPreviewDialogController>
      print_preview_dialog_controller_;
#endif

  scoped_ptr<prerender::PrerenderTracker> prerender_tracker_;
  scoped_ptr<RenderWidgetSnapshotTaker> render_widget_snapshot_taker_;
  scoped_refptr<SafeBrowsingService> sb_service_;
  scoped_ptr<BookmarkPromptController> bookmark_prompt_controller_;
#endif  // !defined(OS_IOS)

#if !defined(OS_IOS) && !defined(OS_ANDROID)
  scoped_ptr<chrome::StorageMonitor> storage_monitor_;
  scoped_ptr<chrome::MediaFileSystemRegistry> media_file_system_registry_;
#endif

  // The following objects are not owned by TestingBrowserProcess:
  PrefService* local_state_;
  IOThread* io_thread_;
  net::URLRequestContextGetter* system_request_context_;

  scoped_ptr<BrowserProcessPlatformPart> platform_part_;

  DISALLOW_COPY_AND_ASSIGN(TestingBrowserProcess);
};

#endif  // CHROME_TEST_BASE_TESTING_BROWSER_PROCESS_H_
