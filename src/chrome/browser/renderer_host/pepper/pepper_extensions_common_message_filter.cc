// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/renderer_host/pepper/pepper_extensions_common_message_filter.h"

#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/extensions/extension_function_dispatcher.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/extensions/extension_messages.h"
#include "content/public/browser/browser_ppapi_host.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_view_host_observer.h"
#include "extensions/common/constants.h"
#include "ipc/ipc_message.h"
#include "ipc/ipc_message_macros.h"
#include "ppapi/c/pp_errors.h"
#include "ppapi/host/dispatch_host_message.h"
#include "ppapi/host/host_message_context.h"
#include "ppapi/proxy/ppapi_messages.h"

namespace chrome {

class PepperExtensionsCommonMessageFilter::DispatcherOwner
    : public content::RenderViewHostObserver,
      public ExtensionFunctionDispatcher::Delegate {
 public:
  DispatcherOwner(PepperExtensionsCommonMessageFilter* message_filter,
                  Profile* profile,
                  content::RenderViewHost* view_host)
      : content::RenderViewHostObserver(view_host),
        message_filter_(message_filter),
        dispatcher_(profile, this) {
  }

  virtual ~DispatcherOwner() {
    message_filter_->DetachDispatcherOwner();
  }

  // ExtensionFunctionDispatcher::Delegate implementation.
  virtual extensions::WindowController* GetExtensionWindowController(
      ) const OVERRIDE {
    NOTREACHED();
    return NULL;
  }

  virtual content::WebContents* GetAssociatedWebContents() const OVERRIDE {
    NOTREACHED();
    return NULL;
  }

  ExtensionFunctionDispatcher* dispatcher() { return &dispatcher_; }
  content::RenderViewHost* render_view_host() {
    return content::RenderViewHostObserver::render_view_host();
  }

 private:
  scoped_refptr<PepperExtensionsCommonMessageFilter> message_filter_;
  ExtensionFunctionDispatcher dispatcher_;

  DISALLOW_COPY_AND_ASSIGN(DispatcherOwner);
};

// static
PepperExtensionsCommonMessageFilter*
PepperExtensionsCommonMessageFilter::Create(content::BrowserPpapiHost* host,
                                            PP_Instance instance) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::IO));

  int render_process_id = 0;
  int render_view_id = 0;
  if (!host->GetRenderViewIDsForInstance(instance, &render_process_id,
                                         &render_view_id)) {
    return NULL;
  }

  base::FilePath profile_directory = host->GetProfileDataDirectory();
  GURL document_url = host->GetDocumentURLForInstance(instance);

  return new PepperExtensionsCommonMessageFilter(render_process_id,
                                                 render_view_id,
                                                 profile_directory,
                                                 document_url);
}

PepperExtensionsCommonMessageFilter::PepperExtensionsCommonMessageFilter(
    int render_process_id,
    int render_view_id,
    const base::FilePath& profile_directory,
    const GURL& document_url)
    : render_process_id_(render_process_id),
      render_view_id_(render_view_id),
      profile_directory_(profile_directory),
      document_url_(document_url),
      dispatcher_owner_(NULL),
      dispatcher_owner_initialized_(false) {
}

PepperExtensionsCommonMessageFilter::~PepperExtensionsCommonMessageFilter() {
}

scoped_refptr<base::TaskRunner>
PepperExtensionsCommonMessageFilter::OverrideTaskRunnerForMessage(
    const IPC::Message& msg) {
  return content::BrowserThread::GetMessageLoopProxyForThread(
      content::BrowserThread::UI);
}

int32_t PepperExtensionsCommonMessageFilter::OnResourceMessageReceived(
    const IPC::Message& msg,
    ppapi::host::HostMessageContext* context) {
  IPC_BEGIN_MESSAGE_MAP(PepperExtensionsCommonMessageFilter, msg)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL(PpapiHostMsg_ExtensionsCommon_Post,
                                      OnPost)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL(PpapiHostMsg_ExtensionsCommon_Call,
                                      OnCall)
  IPC_END_MESSAGE_MAP()
  return PP_ERROR_FAILED;
}

int32_t PepperExtensionsCommonMessageFilter::OnPost(
    ppapi::host::HostMessageContext* context,
    const std::string& request_name,
    base::ListValue& args) {
  if (HandleRequest(context, request_name, &args, false))
    return PP_OK;
  else
    return PP_ERROR_FAILED;
}

int32_t PepperExtensionsCommonMessageFilter::OnCall(
    ppapi::host::HostMessageContext* context,
    const std::string& request_name,
    base::ListValue& args) {
  if (HandleRequest(context, request_name, &args, true))
    return PP_OK_COMPLETIONPENDING;
  else
    return PP_ERROR_FAILED;
}

void PepperExtensionsCommonMessageFilter::EnsureDispatcherOwnerInitialized() {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  if (dispatcher_owner_initialized_)
    return;
  dispatcher_owner_initialized_ = true;

  DCHECK(!dispatcher_owner_);
  content::RenderViewHost* view_host = content::RenderViewHost::FromID(
      render_process_id_, render_view_id_);
  if (!view_host)
    return;

  if (!document_url_.SchemeIs(extensions::kExtensionScheme))
    return;

  ProfileManager* profile_manager = g_browser_process->profile_manager();
  if (!profile_manager)
    return;
  Profile* profile = profile_manager->GetProfile(profile_directory_);

  // It will be automatically destroyed when |view_host| goes away.
  dispatcher_owner_ = new DispatcherOwner(this, profile, view_host);
}

void PepperExtensionsCommonMessageFilter::DetachDispatcherOwner() {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  dispatcher_owner_ = NULL;
}

void PepperExtensionsCommonMessageFilter::PopulateParams(
    const std::string& request_name,
    base::ListValue* args,
    bool has_callback,
    ExtensionHostMsg_Request_Params* params) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));

  params->name = request_name;
  params->arguments.Swap(args);

  params->extension_id = document_url_.host();
  params->source_url = document_url_;

  // We don't need an ID to map a response to the corresponding request.
  params->request_id = 0;
  params->has_callback = has_callback;
  params->user_gesture = false;
}

void PepperExtensionsCommonMessageFilter::OnExtensionFunctionCompleted(
    scoped_ptr<ppapi::host::ReplyMessageContext> reply_context,
    ExtensionFunction::ResponseType type,
    const base::ListValue& results,
    const std::string& /* error */) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));

  // Ignore responses resulted from calls to OnPost().
  if (!reply_context) {
    DCHECK_EQ(0u, results.GetSize());
    return;
  }

  if (type == ExtensionFunction::BAD_MESSAGE) {
    // The input arguments were not validated at the plugin side, so don't kill
    // the plugin process when we see a message with invalid arguments.
    // TODO(yzshen): It is nicer to also do the validation at the plugin side.
    type = ExtensionFunction::FAILED;
  }

  reply_context->params.set_result(
      type == ExtensionFunction::SUCCEEDED ? PP_OK : PP_ERROR_FAILED);
  SendReply(*reply_context, PpapiPluginMsg_ExtensionsCommon_CallReply(results));
}

bool PepperExtensionsCommonMessageFilter::HandleRequest(
    ppapi::host::HostMessageContext* context,
    const std::string& request_name,
    base::ListValue* args,
    bool has_callback) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));

  EnsureDispatcherOwnerInitialized();
  if (!dispatcher_owner_)
    return false;

  ExtensionHostMsg_Request_Params params;
  PopulateParams(request_name, args, has_callback, &params);

  scoped_ptr<ppapi::host::ReplyMessageContext> reply_context;
  if (has_callback) {
    reply_context.reset(new ppapi::host::ReplyMessageContext(
        context->MakeReplyMessageContext()));
  }

  dispatcher_owner_->dispatcher()->DispatchWithCallback(
      params, dispatcher_owner_->render_view_host(),
      base::Bind(
          &PepperExtensionsCommonMessageFilter::OnExtensionFunctionCompleted,
          this,
          base::Passed(&reply_context)));
  return true;
}

}  // namespace chrome
