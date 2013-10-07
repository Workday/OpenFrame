// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/renderer_host/pepper/pepper_flash_drm_host.h"

#if defined(OS_WIN)
#include <Windows.h>
#endif

#include "base/bind.h"
#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "content/public/browser/browser_ppapi_host.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/child_process_security_policy.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/common/pepper_plugin_info.h"
#include "ppapi/c/pp_errors.h"
#include "ppapi/host/dispatch_host_message.h"
#include "ppapi/host/host_message_context.h"
#include "ppapi/host/ppapi_host.h"
#include "ppapi/proxy/ppapi_messages.h"

#if defined(USE_AURA)
#include "ui/aura/root_window.h"
#endif

using content::BrowserPpapiHost;

namespace chrome {

namespace {
const base::FilePath::CharType kVoucherFilename[] =
    FILE_PATH_LITERAL("plugin.vch");
}

#if defined (OS_WIN)
// Helper class to get the UI thread which monitor is showing the
// window associated with the instance's render view. Since we get
// called by the IO thread and we cannot block, the first answer is
// of GetMonitor() may be NULL, but eventually it will contain the
// right monitor.
class MonitorFinder : public base::RefCountedThreadSafe<MonitorFinder> {
 public:
  MonitorFinder(int process_id, int render_id)
      : process_id_(process_id),
        render_id_(render_id),
        monitor_(NULL),
        request_sent_(0) {
  }

  int64_t GetMonitor() {
    // We use |request_sent_| as an atomic boolean so that we
    // never have more than one task posted at a given time. We
    // do this because we don't know how often our client is going
    // to call and we can't cache the |monitor_| value.
    if (InterlockedCompareExchange(&request_sent_, 1, 0) == 0) {
      content::BrowserThread::PostTask(
          content::BrowserThread::UI, FROM_HERE,
          base::Bind(&MonitorFinder::FetchMonitorFromWidget, this));
    }
    return reinterpret_cast<int64_t>(monitor_);
  }

 private:
  friend class base::RefCountedThreadSafe<MonitorFinder>;
  ~MonitorFinder() { }

  void FetchMonitorFromWidget() {
    InterlockedExchange(&request_sent_, 0);
    content::RenderWidgetHost* rwh =
        content::RenderWidgetHost::FromID(process_id_, render_id_);
    if (!rwh)
      return;
    content::RenderWidgetHostView* view = rwh->GetView();
    if (!view)
      return;
    gfx::NativeView native_view = view->GetNativeView();
#if defined(USE_AURA)
    aura::RootWindow* root = native_view->GetRootWindow();
    if (!root)
      return;
    HWND window = root->GetAcceleratedWidget();
#else
    HWND window = native_view;
#endif
    HMONITOR monitor = ::MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
    InterlockedExchangePointer(reinterpret_cast<void* volatile *>(&monitor_),
                               monitor);
  }

  const int process_id_;
  const int render_id_;
  volatile HMONITOR monitor_;
  volatile long request_sent_;
};
#else
// TODO(cpu): Support Mac and Linux someday.
class MonitorFinder : public base::RefCountedThreadSafe<MonitorFinder> {
 public:
  MonitorFinder(int, int) { }
  int64_t GetMonitor() { return 0; }

 private:
  friend class base::RefCountedThreadSafe<MonitorFinder>;
  ~MonitorFinder() { }
};
#endif

PepperFlashDRMHost::PepperFlashDRMHost(BrowserPpapiHost* host,
                                       PP_Instance instance,
                                       PP_Resource resource)
    : ppapi::host::ResourceHost(host->GetPpapiHost(), instance, resource),
      weak_factory_(this){
  // Grant permissions to read the flash voucher file.
  int render_process_id;
  int render_view_id;
  bool success =
      host->GetRenderViewIDsForInstance(
          instance, &render_process_id, &render_view_id);
  base::FilePath plugin_dir = host->GetPluginPath().DirName();
  DCHECK(!plugin_dir.empty() && success);
  base::FilePath voucher_file = plugin_dir.Append(
      base::FilePath(kVoucherFilename));
  content::ChildProcessSecurityPolicy::GetInstance()->GrantReadFile(
      render_process_id, voucher_file);

  fetcher_ = new DeviceIDFetcher(render_process_id);
  monitor_finder_ = new MonitorFinder(render_process_id, render_view_id);
  monitor_finder_->GetMonitor();
}

PepperFlashDRMHost::~PepperFlashDRMHost() {
}

int32_t PepperFlashDRMHost::OnResourceMessageReceived(
    const IPC::Message& msg,
    ppapi::host::HostMessageContext* context) {
  IPC_BEGIN_MESSAGE_MAP(PepperFlashDRMHost, msg)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL_0(PpapiHostMsg_FlashDRM_GetDeviceID,
                                        OnHostMsgGetDeviceID)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL_0(PpapiHostMsg_FlashDRM_GetHmonitor,
                                        OnHostMsgGetHmonitor)
  IPC_END_MESSAGE_MAP()
  return PP_ERROR_FAILED;
}

int32_t PepperFlashDRMHost::OnHostMsgGetDeviceID(
    ppapi::host::HostMessageContext* context) {
  if (!fetcher_->Start(base::Bind(&PepperFlashDRMHost::GotDeviceID,
                                  weak_factory_.GetWeakPtr(),
                                  context->MakeReplyMessageContext()))) {
    return PP_ERROR_INPROGRESS;
  }
  return PP_OK_COMPLETIONPENDING;
}

int32_t PepperFlashDRMHost::OnHostMsgGetHmonitor(
    ppapi::host::HostMessageContext* context) {
  int64_t monitor_id = monitor_finder_->GetMonitor();
  if (monitor_id) {
    context->reply_msg = PpapiPluginMsg_FlashDRM_GetHmonitorReply(monitor_id);
    return PP_OK;
  } else {
    return PP_ERROR_FAILED;
  }
}

void PepperFlashDRMHost::GotDeviceID(
    ppapi::host::ReplyMessageContext reply_context,
    const std::string& id) {
  reply_context.params.set_result(
      id.empty() ? PP_ERROR_FAILED : PP_OK);
  host()->SendReply(reply_context,
                    PpapiPluginMsg_FlashDRM_GetDeviceIDReply(id));
}

}  // namespace chrome
