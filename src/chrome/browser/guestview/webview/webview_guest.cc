// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/guestview/webview/webview_guest.h"

#include "chrome/browser/extensions/api/web_request/web_request_api.h"
#include "chrome/browser/extensions/extension_renderer_state.h"
#include "chrome/browser/extensions/script_executor.h"
#include "chrome/browser/guestview/guestview_constants.h"
#include "chrome/browser/guestview/webview/webview_constants.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/native_web_keyboard_event.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/resource_request_details.h"
#include "content/public/browser/user_metrics.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/result_codes.h"
#include "net/base/net_errors.h"

using content::WebContents;

namespace {

static std::string TerminationStatusToString(base::TerminationStatus status) {
  switch (status) {
    case base::TERMINATION_STATUS_NORMAL_TERMINATION:
      return "normal";
    case base::TERMINATION_STATUS_ABNORMAL_TERMINATION:
    case base::TERMINATION_STATUS_STILL_RUNNING:
      return "abnormal";
    case base::TERMINATION_STATUS_PROCESS_WAS_KILLED:
      return "killed";
    case base::TERMINATION_STATUS_PROCESS_CRASHED:
      return "crashed";
    case base::TERMINATION_STATUS_MAX_ENUM:
      break;
  }
  NOTREACHED() << "Unknown Termination Status.";
  return "unknown";
}

static std::string PermissionTypeToString(BrowserPluginPermissionType type) {
  switch (type) {
    case BROWSER_PLUGIN_PERMISSION_TYPE_DOWNLOAD:
      return webview::kPermissionTypeDownload;
    case BROWSER_PLUGIN_PERMISSION_TYPE_GEOLOCATION:
      return webview::kPermissionTypeGeolocation;
    case BROWSER_PLUGIN_PERMISSION_TYPE_MEDIA:
      return webview::kPermissionTypeMedia;
    case BROWSER_PLUGIN_PERMISSION_TYPE_NEW_WINDOW:
      return webview::kPermissionTypeNewWindow;
    case BROWSER_PLUGIN_PERMISSION_TYPE_POINTER_LOCK:
      return webview::kPermissionTypePointerLock;
    case BROWSER_PLUGIN_PERMISSION_TYPE_JAVASCRIPT_DIALOG:
      return webview::kPermissionTypeDialog;
    case BROWSER_PLUGIN_PERMISSION_TYPE_UNKNOWN:
    default:
      NOTREACHED();
      break;
  }
  return std::string();
}

void RemoveWebViewEventListenersOnIOThread(
    void* profile,
    const std::string& extension_id,
    int embedder_process_id,
    int guest_instance_id) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::IO));
  ExtensionWebRequestEventRouter::GetInstance()->RemoveWebViewEventListeners(
      profile, extension_id, embedder_process_id, guest_instance_id);
}

}  // namespace

WebViewGuest::WebViewGuest(WebContents* guest_web_contents)
    : GuestView(guest_web_contents),
      WebContentsObserver(guest_web_contents),
      script_executor_(new extensions::ScriptExecutor(guest_web_contents,
                                                      &script_observers_)),
      next_permission_request_id_(0) {
  notification_registrar_.Add(
      this, content::NOTIFICATION_LOAD_COMPLETED_MAIN_FRAME,
      content::Source<WebContents>(guest_web_contents));

  notification_registrar_.Add(
      this, content::NOTIFICATION_RESOURCE_RECEIVED_REDIRECT,
      content::Source<WebContents>(guest_web_contents));
}

// static
WebViewGuest* WebViewGuest::From(int embedder_process_id,
                                 int guest_instance_id) {
  GuestView* guest = GuestView::From(embedder_process_id, guest_instance_id);
  if (!guest)
    return NULL;
  return guest->AsWebView();
}

void WebViewGuest::Attach(WebContents* embedder_web_contents,
                          const std::string& extension_id,
                          const base::DictionaryValue& args) {
  GuestView::Attach(
      embedder_web_contents, extension_id, args);

  AddWebViewToExtensionRendererState();
}

GuestView::Type WebViewGuest::GetViewType() const {
  return GuestView::WEBVIEW;
}

WebViewGuest* WebViewGuest::AsWebView() {
  return this;
}

AdViewGuest* WebViewGuest::AsAdView() {
  return NULL;
}

void WebViewGuest::AddMessageToConsole(int32 level,
                                       const string16& message,
                                       int32 line_no,
                                       const string16& source_id) {
  scoped_ptr<DictionaryValue> args(new DictionaryValue());
  // Log levels are from base/logging.h: LogSeverity.
  args->SetInteger(webview::kLevel, level);
  args->SetString(webview::kMessage, message);
  args->SetInteger(webview::kLine, line_no);
  args->SetString(webview::kSourceId, source_id);
  DispatchEvent(
      new GuestView::Event(webview::kEventConsoleMessage, args.Pass()));
}

void WebViewGuest::Close() {
  scoped_ptr<DictionaryValue> args(new DictionaryValue());
  DispatchEvent(new GuestView::Event(webview::kEventClose, args.Pass()));
}

void WebViewGuest::GuestProcessGone(base::TerminationStatus status) {
  scoped_ptr<DictionaryValue> args(new DictionaryValue());
  args->SetInteger(webview::kProcessId,
                   web_contents()->GetRenderProcessHost()->GetID());
  args->SetString(webview::kReason, TerminationStatusToString(status));
  DispatchEvent(
      new GuestView::Event(webview::kEventExit, args.Pass()));
}

bool WebViewGuest::HandleKeyboardEvent(
    const content::NativeWebKeyboardEvent& event) {
  if (event.type != WebKit::WebInputEvent::RawKeyDown)
    return false;

#if defined(OS_MACOSX)
  if (event.modifiers != WebKit::WebInputEvent::MetaKey)
    return false;

  if (event.windowsKeyCode == ui::VKEY_OEM_4) {
    Go(-1);
    return true;
  }

  if (event.windowsKeyCode == ui::VKEY_OEM_6) {
    Go(1);
    return true;
  }
#else
  if (event.windowsKeyCode == ui::VKEY_BROWSER_BACK) {
    Go(-1);
    return true;
  }

  if (event.windowsKeyCode == ui::VKEY_BROWSER_FORWARD) {
    Go(1);
    return true;
  }
#endif
  return false;
}

// TODO(fsamuel): Find a reliable way to test the 'responsive' and
// 'unresponsive' events.
void WebViewGuest::RendererResponsive() {
  scoped_ptr<DictionaryValue> args(new DictionaryValue());
  args->SetInteger(webview::kProcessId,
      guest_web_contents()->GetRenderProcessHost()->GetID());
  DispatchEvent(new GuestView::Event(webview::kEventResponsive, args.Pass()));
}

void WebViewGuest::RendererUnresponsive() {
  scoped_ptr<DictionaryValue> args(new DictionaryValue());
  args->SetInteger(webview::kProcessId,
      guest_web_contents()->GetRenderProcessHost()->GetID());
  DispatchEvent(new GuestView::Event(webview::kEventUnresponsive, args.Pass()));
}

bool WebViewGuest::RequestPermission(
    BrowserPluginPermissionType permission_type,
    const base::DictionaryValue& request_info,
    const PermissionResponseCallback& callback) {
  int request_id = next_permission_request_id_++;
  pending_permission_requests_[request_id] = callback;
  scoped_ptr<base::DictionaryValue> args(request_info.DeepCopy());
  args->SetInteger(webview::kRequestId, request_id);
  switch (permission_type) {
    case BROWSER_PLUGIN_PERMISSION_TYPE_NEW_WINDOW: {
      DispatchEvent(new GuestView::Event(webview::kEventNewWindow,
                                         args.Pass()));
      break;
    }
    case BROWSER_PLUGIN_PERMISSION_TYPE_JAVASCRIPT_DIALOG: {
      DispatchEvent(new GuestView::Event(webview::kEventDialog,
                                         args.Pass()));
      break;
    }
    default: {
      args->SetString(webview::kPermission,
                      PermissionTypeToString(permission_type));
      DispatchEvent(new GuestView::Event(webview::kEventPermissionRequest,
                                         args.Pass()));
      break;
    }
  }
  return true;
}

void WebViewGuest::Observe(int type,
                           const content::NotificationSource& source,
                           const content::NotificationDetails& details) {
  switch (type) {
    case content::NOTIFICATION_LOAD_COMPLETED_MAIN_FRAME: {
      DCHECK_EQ(content::Source<WebContents>(source).ptr(),
                guest_web_contents());
      if (content::Source<WebContents>(source).ptr() == guest_web_contents())
        LoadHandlerCalled();
      break;
    }
    case content::NOTIFICATION_RESOURCE_RECEIVED_REDIRECT: {
      DCHECK_EQ(content::Source<WebContents>(source).ptr(),
                guest_web_contents());
      content::ResourceRedirectDetails* resource_redirect_details =
          content::Details<content::ResourceRedirectDetails>(details).ptr();
      bool is_top_level =
          resource_redirect_details->resource_type == ResourceType::MAIN_FRAME;
      LoadRedirect(resource_redirect_details->url,
                   resource_redirect_details->new_url,
                   is_top_level);
      break;
    }
    default:
      NOTREACHED() << "Unexpected notification sent.";
      break;
  }
}

void WebViewGuest::Go(int relative_index) {
  guest_web_contents()->GetController().GoToOffset(relative_index);
}

void WebViewGuest::Reload() {
  // TODO(fsamuel): Don't check for repost because we don't want to show
  // Chromium's repost warning. We might want to implement a separate API
  // for registering a callback if a repost is about to happen.
  guest_web_contents()->GetController().Reload(false);
}

bool WebViewGuest::SetPermission(int request_id,
                                 bool should_allow,
                                 const std::string& user_input) {
  RequestMap::iterator request_itr =
      pending_permission_requests_.find(request_id);

  if (request_itr == pending_permission_requests_.end())
    return false;

  request_itr->second.Run(should_allow, user_input);
  pending_permission_requests_.erase(request_itr);
  return true;
}

void WebViewGuest::Stop() {
  guest_web_contents()->Stop();
}

void WebViewGuest::Terminate() {
  content::RecordAction(content::UserMetricsAction("WebView.Guest.Terminate"));
  base::ProcessHandle process_handle =
      guest_web_contents()->GetRenderProcessHost()->GetHandle();
  if (process_handle)
    base::KillProcess(process_handle, content::RESULT_CODE_KILLED, false);
}

WebViewGuest::~WebViewGuest() {
}

void WebViewGuest::DidCommitProvisionalLoadForFrame(
    int64 frame_id,
    bool is_main_frame,
    const GURL& url,
    content::PageTransition transition_type,
    content::RenderViewHost* render_view_host) {
  scoped_ptr<DictionaryValue> args(new DictionaryValue());
  args->SetString(guestview::kUrl, url.spec());
  args->SetBoolean(guestview::kIsTopLevel, is_main_frame);
  args->SetInteger(webview::kInternalCurrentEntryIndex,
      web_contents()->GetController().GetCurrentEntryIndex());
  args->SetInteger(webview::kInternalEntryCount,
      web_contents()->GetController().GetEntryCount());
  args->SetInteger(webview::kInternalProcessId,
      web_contents()->GetRenderProcessHost()->GetID());
  DispatchEvent(new GuestView::Event(webview::kEventLoadCommit, args.Pass()));
}

void WebViewGuest::DidFailProvisionalLoad(
    int64 frame_id,
    bool is_main_frame,
    const GURL& validated_url,
    int error_code,
    const string16& error_description,
    content::RenderViewHost* render_view_host) {
  // Translate the |error_code| into an error string.
  std::string error_type;
  RemoveChars(net::ErrorToString(error_code), "net::", &error_type);

  scoped_ptr<DictionaryValue> args(new DictionaryValue());
  args->SetBoolean(guestview::kIsTopLevel, is_main_frame);
  args->SetString(guestview::kUrl, validated_url.spec());
  args->SetString(guestview::kReason, error_type);
  DispatchEvent(new GuestView::Event(webview::kEventLoadAbort, args.Pass()));
}

void WebViewGuest::DidStartProvisionalLoadForFrame(
    int64 frame_id,
    int64 parent_frame_id,
    bool is_main_frame,
    const GURL& validated_url,
    bool is_error_page,
    bool is_iframe_srcdoc,
    content::RenderViewHost* render_view_host) {
  scoped_ptr<DictionaryValue> args(new DictionaryValue());
  args->SetString(guestview::kUrl, validated_url.spec());
  args->SetBoolean(guestview::kIsTopLevel, is_main_frame);
  DispatchEvent(new GuestView::Event(webview::kEventLoadStart, args.Pass()));
}

void WebViewGuest::DidStopLoading(content::RenderViewHost* render_view_host) {
  scoped_ptr<DictionaryValue> args(new DictionaryValue());
  DispatchEvent(new GuestView::Event(webview::kEventLoadStop, args.Pass()));
}

void WebViewGuest::WebContentsDestroyed(WebContents* web_contents) {
  RemoveWebViewFromExtensionRendererState(web_contents);
  content::BrowserThread::PostTask(
      content::BrowserThread::IO,
      FROM_HERE,
      base::Bind(
          &RemoveWebViewEventListenersOnIOThread,
          browser_context(), extension_id(),
          embedder_render_process_id(),
          view_instance_id()));
}

void WebViewGuest::LoadHandlerCalled() {
  scoped_ptr<DictionaryValue> args(new DictionaryValue());
  DispatchEvent(new GuestView::Event(webview::kEventContentLoad, args.Pass()));
}

void WebViewGuest::LoadRedirect(const GURL& old_url,
                                const GURL& new_url,
                                bool is_top_level) {
  scoped_ptr<DictionaryValue> args(new DictionaryValue());
  args->SetBoolean(guestview::kIsTopLevel, is_top_level);
  args->SetString(webview::kNewURL, new_url.spec());
  args->SetString(webview::kOldURL, old_url.spec());
  DispatchEvent(new GuestView::Event(webview::kEventLoadRedirect, args.Pass()));
}

void WebViewGuest::AddWebViewToExtensionRendererState() {
  ExtensionRendererState::WebViewInfo webview_info;
  webview_info.embedder_process_id = embedder_render_process_id();
  webview_info.embedder_routing_id = embedder_web_contents()->GetRoutingID();
  webview_info.instance_id = view_instance_id();

  content::BrowserThread::PostTask(
      content::BrowserThread::IO, FROM_HERE,
      base::Bind(
          &ExtensionRendererState::AddWebView,
          base::Unretained(ExtensionRendererState::GetInstance()),
          guest_web_contents()->GetRenderProcessHost()->GetID(),
          guest_web_contents()->GetRoutingID(),
          webview_info));
}

// static
void WebViewGuest::RemoveWebViewFromExtensionRendererState(
    WebContents* web_contents) {
  content::BrowserThread::PostTask(
      content::BrowserThread::IO, FROM_HERE,
      base::Bind(
          &ExtensionRendererState::RemoveWebView,
          base::Unretained(ExtensionRendererState::GetInstance()),
          web_contents->GetRenderProcessHost()->GetID(),
          web_contents->GetRoutingID()));
}
