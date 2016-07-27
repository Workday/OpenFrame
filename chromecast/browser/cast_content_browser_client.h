// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMECAST_BROWSER_CAST_CONTENT_BROWSER_CLIENT_H_
#define CHROMECAST_BROWSER_CAST_CONTENT_BROWSER_CLIENT_H_

#include <map>
#include <string>
#include <vector>

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "content/public/browser/content_browser_client.h"

class PrefService;

namespace breakpad {
class CrashHandlerHostLinux;
}

namespace media {
class AudioManagerFactory;
class BrowserCdmFactory;
}

namespace metrics {
class MetricsService;
}

namespace chromecast {
class CastService;

namespace media {
class MediaPipelineBackend;
struct MediaPipelineDeviceParams;
class CmaMediaPipelineClient;
}

namespace shell {

class CastBrowserMainParts;
class URLRequestContextFactory;

class CastContentBrowserClient : public content::ContentBrowserClient {
 public:
  // Creates an implementation of CastContentBrowserClient. Platform should
  // link in an implementation as needed.
  static scoped_ptr<CastContentBrowserClient> Create();

  ~CastContentBrowserClient() override;

  // Appends extra command line arguments before launching a new process.
  virtual void AppendExtraCommandLineSwitches(base::CommandLine* command_line);

  // Creates and returns the CastService instance for the current process.
  // Note: |request_context_getter| might be different than the main request
  // getter accessible via CastBrowserProcess.
  virtual scoped_ptr<CastService> CreateCastService(
      content::BrowserContext* browser_context,
      PrefService* pref_service,
      net::URLRequestContextGetter* request_context_getter);

#if !defined(OS_ANDROID)
  virtual scoped_ptr<::media::AudioManagerFactory> CreateAudioManagerFactory();

  // Creates a CmaMediaPipelineClient which is responsible to create (CMA
  // backend)
  // for media playback and watch media pipeline status, called once per media
  // player
  // instance.
  virtual scoped_refptr<media::CmaMediaPipelineClient>
  CreateCmaMediaPipelineClient();
#endif

  // Performs cleanup for process exit (but before AtExitManager cleanup).
  void ProcessExiting();

  // Invoked when the metrics client ID changes.
  virtual void SetMetricsClientId(const std::string& client_id);

  // Allows registration of extra metrics providers.
  virtual void RegisterMetricsProviders(
      ::metrics::MetricsService* metrics_service);

  // Returns whether or not the remote debugging service should be started
  // on browser startup.
  virtual bool EnableRemoteDebuggingImmediately();

  // content::ContentBrowserClient implementation:
  content::BrowserMainParts* CreateBrowserMainParts(
      const content::MainFunctionParams& parameters) override;
  void RenderProcessWillLaunch(content::RenderProcessHost* host) override;
  net::URLRequestContextGetter* CreateRequestContext(
      content::BrowserContext* browser_context,
      content::ProtocolHandlerMap* protocol_handlers,
      content::URLRequestInterceptorScopedVector request_interceptors)
      override;
  bool IsHandledURL(const GURL& url) override;
  void AppendExtraCommandLineSwitches(base::CommandLine* command_line,
                                      int child_process_id) override;
  content::AccessTokenStore* CreateAccessTokenStore() override;
  void OverrideWebkitPrefs(content::RenderViewHost* render_view_host,
                           content::WebPreferences* prefs) override;
  void ResourceDispatcherHostCreated() override;
  std::string GetApplicationLocale() override;
  content::QuotaPermissionContext* CreateQuotaPermissionContext() override;
  void AllowCertificateError(
      content::WebContents* web_contents,
      int cert_error,
      const net::SSLInfo& ssl_info,
      const GURL& request_url,
      content::ResourceType resource_type,
      bool overridable,
      bool strict_enforcement,
      bool expired_previous_decision,
      const base::Callback<void(bool)>& callback,
      content::CertificateRequestResultType* result) override;
  void SelectClientCertificate(
      content::WebContents* web_contents,
      net::SSLCertRequestInfo* cert_request_info,
      scoped_ptr<content::ClientCertificateDelegate> delegate) override;
  bool CanCreateWindow(
      const GURL& opener_url,
      const GURL& opener_top_level_frame_url,
      const GURL& source_origin,
      WindowContainerType container_type,
      const GURL& target_url,
      const content::Referrer& referrer,
      WindowOpenDisposition disposition,
      const blink::WebWindowFeatures& features,
      bool user_gesture,
      bool opener_suppressed,
      content::ResourceContext* context,
      int render_process_id,
      int opener_render_view_id,
      int opener_render_frame_id,
      bool* no_javascript_access) override;
  void RegisterUnsandboxedOutOfProcessMojoApplications(
      std::map<GURL, base::string16>* apps) override;
#if defined(OS_ANDROID)
  void GetAdditionalMappedFilesForChildProcess(
      const base::CommandLine& command_line,
      int child_process_id,
      content::FileDescriptorInfo* mappings,
      std::map<int, base::MemoryMappedFile::Region>* regions) override;
#else
  scoped_ptr<::media::CdmFactory> CreateCdmFactory() override;
  void GetAdditionalMappedFilesForChildProcess(
      const base::CommandLine& command_line,
      int child_process_id,
      content::FileDescriptorInfo* mappings) override;
#endif  // defined(OS_ANDROID)
#if defined(OS_ANDROID) && defined(VIDEO_HOLE)
  content::ExternalVideoSurfaceContainer*
  OverrideCreateExternalVideoSurfaceContainer(
      content::WebContents* web_contents) override;
#endif  // defined(OS_ANDROID) && defined(VIDEO_HOLE)

 protected:
  CastContentBrowserClient();

  URLRequestContextFactory* url_request_context_factory() const {
    return url_request_context_factory_.get();
  }

 private:
  void AddNetworkHintsMessageFilter(int render_process_id,
                                    net::URLRequestContext* context);

  net::X509Certificate* SelectClientCertificateOnIOThread(
      GURL requesting_url,
      int render_process_id);

#if !defined(OS_ANDROID)
  // Returns the crash signal FD corresponding to the current process type.
  int GetCrashSignalFD(const base::CommandLine& command_line);

  // Creates a CrashHandlerHost instance for the given process type.
  breakpad::CrashHandlerHostLinux* CreateCrashHandlerHost(
      const std::string& process_type);

  // A static cache to hold crash_handlers for each process_type
  std::map<std::string, breakpad::CrashHandlerHostLinux*> crash_handlers_;
#endif

  scoped_ptr<URLRequestContextFactory> url_request_context_factory_;

  DISALLOW_COPY_AND_ASSIGN(CastContentBrowserClient);
};

}  // namespace shell
}  // namespace chromecast

#endif  // CHROMECAST_BROWSER_CAST_CONTENT_BROWSER_CLIENT_H_
