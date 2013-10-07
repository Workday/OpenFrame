// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "android_webview/lib/main/aw_main_delegate.h"

#include "android_webview/browser/aw_content_browser_client.h"
#include "android_webview/browser/gpu_memory_buffer_factory_impl.h"
#include "android_webview/browser/scoped_allow_wait_for_legacy_web_view_api.h"
#include "android_webview/lib/aw_browser_dependency_factory_impl.h"
#include "android_webview/native/aw_geolocation_permission_context.h"
#include "android_webview/native/aw_quota_manager_bridge_impl.h"
#include "android_webview/native/aw_web_contents_view_delegate.h"
#include "android_webview/renderer/aw_content_renderer_client.h"
#include "base/command_line.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/threading/thread_restrictions.h"
#include "cc/base/switches.h"
#include "content/public/browser/browser_main_runner.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/common/content_switches.h"
#include "gpu/command_buffer/client/gl_in_process_context.h"
#include "gpu/command_buffer/service/in_process_command_buffer.h"
#include "webkit/common/gpu/webgraphicscontext3d_in_process_command_buffer_impl.h"

namespace android_webview {

namespace {

// TODO(boliu): Remove this global Allow once the underlying issues are
// resolved - http://crbug.com/240453. See AwMainDelegate::RunProcess below.
base::LazyInstance<scoped_ptr<ScopedAllowWaitForLegacyWebViewApi> >
    g_allow_wait_in_ui_thread = LAZY_INSTANCE_INITIALIZER;

}

AwMainDelegate::AwMainDelegate()
    : gpu_memory_buffer_factory_(new GpuMemoryBufferFactoryImpl) {
}

AwMainDelegate::~AwMainDelegate() {
}

bool AwMainDelegate::BasicStartupComplete(int* exit_code) {
  content::SetContentClient(&content_client_);

  gpu::GLInProcessContext::SetGpuMemoryBufferFactory(
      gpu_memory_buffer_factory_.get());
  gpu::InProcessCommandBuffer::EnableVirtualizedContext();

  CommandLine* cl = CommandLine::ForCurrentProcess();
  cl->AppendSwitch(switches::kEnableBeginFrameScheduling);
  if (!cl->HasSwitch("disable-map-image"))
    cl->AppendSwitch(cc::switches::kUseMapImage);

  // WebView uses the Android system's scrollbars and overscroll glow.
  cl->AppendSwitch(switches::kHideScrollbars);
  cl->AppendSwitch(switches::kDisableOverscrollEdgeEffect);

  // Not yet secure in single-process mode.
  cl->AppendSwitch(switches::kDisableExperimentalWebGL);

  return false;
}

void AwMainDelegate::PreSandboxStartup() {
  // TODO(torne): When we have a separate renderer process, we need to handle
  // being passed open FDs for the resource paks here.
}

void AwMainDelegate::SandboxInitialized(const std::string& process_type) {
  // TODO(torne): Adjust linux OOM score here.
}

int AwMainDelegate::RunProcess(
    const std::string& process_type,
    const content::MainFunctionParams& main_function_params) {
  if (process_type.empty()) {
    AwBrowserDependencyFactoryImpl::InstallInstance();

    browser_runner_.reset(content::BrowserMainRunner::Create());
    int exit_code = browser_runner_->Initialize(main_function_params);
    DCHECK(exit_code < 0);

    g_allow_wait_in_ui_thread.Get().reset(
        new ScopedAllowWaitForLegacyWebViewApi);

    // Return 0 so that we do NOT trigger the default behavior. On Android, the
    // UI message loop is managed by the Java application.
    return 0;
  }

  return -1;
}

void AwMainDelegate::ProcessExiting(const std::string& process_type) {
  // TODO(torne): Clean up resources when we handle them.

  logging::CloseLogFile();
}

content::ContentBrowserClient*
    AwMainDelegate::CreateContentBrowserClient() {
  content_browser_client_.reset(new AwContentBrowserClient(this));
  return content_browser_client_.get();
}

content::ContentRendererClient*
    AwMainDelegate::CreateContentRendererClient() {
  content_renderer_client_.reset(new AwContentRendererClient());
  return content_renderer_client_.get();
}

AwQuotaManagerBridge* AwMainDelegate::CreateAwQuotaManagerBridge(
    AwBrowserContext* browser_context) {
  return new AwQuotaManagerBridgeImpl(browser_context);
}

content::GeolocationPermissionContext*
    AwMainDelegate::CreateGeolocationPermission(
        AwBrowserContext* browser_context) {
  return AwGeolocationPermissionContext::Create(browser_context);
}

content::WebContentsViewDelegate* AwMainDelegate::CreateViewDelegate(
    content::WebContents* web_contents) {
  return AwWebContentsViewDelegate::Create(web_contents);
}

}  // namespace android_webview
