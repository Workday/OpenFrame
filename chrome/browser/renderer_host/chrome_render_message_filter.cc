// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/renderer_host/chrome_render_message_filter.h"

#include <string>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/metrics/histogram.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/automation/automation_resource_message_filter.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/content_settings/cookie_settings.h"
#include "chrome/browser/content_settings/tab_specific_content_settings.h"
#include "chrome/browser/extensions/activity_log/activity_action_constants.h"
#include "chrome/browser/extensions/activity_log/activity_actions.h"
#include "chrome/browser/extensions/activity_log/activity_log.h"
#include "chrome/browser/extensions/api/activity_log_private/activity_log_private_api.h"
#include "chrome/browser/extensions/api/messaging/message_service.h"
#include "chrome/browser/extensions/event_router.h"
#include "chrome/browser/extensions/extension_process_manager.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/net/chrome_url_request_context.h"
#include "chrome/browser/net/predictor.h"
#include "chrome/browser/task_manager/task_manager.h"
#include "chrome/common/extensions/api/i18n/default_locale_handler.h"
#include "chrome/common/extensions/extension_file_util.h"
#include "chrome/common/extensions/extension_messages.h"
#include "chrome/common/extensions/message_bundle.h"
#include "chrome/common/render_messages.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/resource_dispatcher_host.h"
#include "extensions/common/constants.h"

#if defined(USE_TCMALLOC)
#include "chrome/browser/browser_about_handler.h"
#endif

using content::BrowserThread;
using extensions::APIPermission;
using extensions::api::activity_log_private::BlockedChromeActivityDetail;
using WebKit::WebCache;

namespace {

// Logs an action to the extension activity log for the specified profile.  Can
// be called from any thread.
void AddActionToExtensionActivityLog(
    Profile* profile,
    scoped_refptr<extensions::Action> action) {
  // The ActivityLog can only be accessed from the main (UI) thread.  If we're
  // running on the wrong thread, re-dispatch from the main thread.
  if (!BrowserThread::CurrentlyOn(BrowserThread::UI)) {
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(&AddActionToExtensionActivityLog, profile, action));
  } else {
    // If the action included a URL, check whether it is for an incognito
    // profile.  The check is performed here so that it can safely be done from
    // the UI thread.
    if (action->page_url().is_valid() || !action->page_title().empty())
      action->set_page_incognito(profile->IsOffTheRecord());
    extensions::ActivityLog* activity_log =
        extensions::ActivityLog::GetInstance(profile);
    activity_log->LogAction(action);
  }
}

} // namespace

ChromeRenderMessageFilter::ChromeRenderMessageFilter(
    int render_process_id,
    Profile* profile,
    net::URLRequestContextGetter* request_context)
    : render_process_id_(render_process_id),
      profile_(profile),
      off_the_record_(profile_->IsOffTheRecord()),
      request_context_(request_context),
      extension_info_map_(
          extensions::ExtensionSystem::Get(profile)->info_map()),
      cookie_settings_(CookieSettings::Factory::GetForProfile(profile)),
      weak_ptr_factory_(this) {
}

ChromeRenderMessageFilter::~ChromeRenderMessageFilter() {
}

bool ChromeRenderMessageFilter::OnMessageReceived(const IPC::Message& message,
                                                  bool* message_was_ok) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP_EX(ChromeRenderMessageFilter, message, *message_was_ok)
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_DnsPrefetch, OnDnsPrefetch)
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_Preconnect, OnPreconnect)
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_ResourceTypeStats,
                        OnResourceTypeStats)
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_UpdatedCacheStats,
                        OnUpdatedCacheStats)
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_FPS, OnFPS)
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_V8HeapStats, OnV8HeapStats)
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_OpenChannelToExtension,
                        OnOpenChannelToExtension)
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_OpenChannelToTab, OnOpenChannelToTab)
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_OpenChannelToNativeApp,
                        OnOpenChannelToNativeApp)
    IPC_MESSAGE_HANDLER_DELAY_REPLY(ExtensionHostMsg_GetMessageBundle,
                                    OnGetExtensionMessageBundle)
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_AddListener, OnExtensionAddListener)
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_RemoveListener,
                        OnExtensionRemoveListener)
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_AddLazyListener,
                        OnExtensionAddLazyListener)
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_RemoveLazyListener,
                        OnExtensionRemoveLazyListener)
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_AddFilteredListener,
                        OnExtensionAddFilteredListener)
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_RemoveFilteredListener,
                        OnExtensionRemoveFilteredListener)
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_CloseChannel, OnExtensionCloseChannel)
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_RequestForIOThread,
                        OnExtensionRequestForIOThread)
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_ShouldSuspendAck,
                        OnExtensionShouldSuspendAck)
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_GenerateUniqueID,
                        OnExtensionGenerateUniqueID)
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_SuspendAck, OnExtensionSuspendAck)
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_ResumeRequests,
                        OnExtensionResumeRequests);
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_AddAPIActionToActivityLog,
                        OnAddAPIActionToExtensionActivityLog);
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_AddDOMActionToActivityLog,
                        OnAddDOMActionToExtensionActivityLog);
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_AddBlockedCallToActivityLog,
                        OnAddBlockedCallToExtensionActivityLog);
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_AddEventToActivityLog,
                        OnAddEventToExtensionActivityLog);
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_AllowDatabase, OnAllowDatabase)
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_AllowDOMStorage, OnAllowDOMStorage)
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_AllowFileSystem, OnAllowFileSystem)
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_AllowIndexedDB, OnAllowIndexedDB)
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_CanTriggerClipboardRead,
                        OnCanTriggerClipboardRead)
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_CanTriggerClipboardWrite,
                        OnCanTriggerClipboardWrite)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()

#if defined(ENABLE_AUTOMATION)
  if ((message.type() == ChromeViewHostMsg_GetCookies::ID ||
       message.type() == ChromeViewHostMsg_SetCookie::ID) &&
    AutomationResourceMessageFilter::ShouldFilterCookieMessages(
        render_process_id_, message.routing_id())) {
    // ChromeFrame then we need to get/set cookies from the external host.
    IPC_BEGIN_MESSAGE_MAP_EX(ChromeRenderMessageFilter, message,
                             *message_was_ok)
      IPC_MESSAGE_HANDLER_DELAY_REPLY(ChromeViewHostMsg_GetCookies,
                                      OnGetCookies)
      IPC_MESSAGE_HANDLER(ChromeViewHostMsg_SetCookie, OnSetCookie)
    IPC_END_MESSAGE_MAP()
    handled = true;
  }
#endif

  return handled;
}

void ChromeRenderMessageFilter::OverrideThreadForMessage(
    const IPC::Message& message, BrowserThread::ID* thread) {
  switch (message.type()) {
    case ChromeViewHostMsg_ResourceTypeStats::ID:
    case ExtensionHostMsg_AddListener::ID:
    case ExtensionHostMsg_RemoveListener::ID:
    case ExtensionHostMsg_AddLazyListener::ID:
    case ExtensionHostMsg_RemoveLazyListener::ID:
    case ExtensionHostMsg_AddFilteredListener::ID:
    case ExtensionHostMsg_RemoveFilteredListener::ID:
    case ExtensionHostMsg_CloseChannel::ID:
    case ExtensionHostMsg_ShouldSuspendAck::ID:
    case ExtensionHostMsg_SuspendAck::ID:
    case ChromeViewHostMsg_UpdatedCacheStats::ID:
      *thread = BrowserThread::UI;
      break;
    default:
      break;
  }
}

net::HostResolver* ChromeRenderMessageFilter::GetHostResolver() {
  return request_context_->GetURLRequestContext()->host_resolver();
}

void ChromeRenderMessageFilter::OnDnsPrefetch(
    const std::vector<std::string>& hostnames) {
  if (profile_->GetNetworkPredictor())
    profile_->GetNetworkPredictor()->DnsPrefetchList(hostnames);
}

void ChromeRenderMessageFilter::OnPreconnect(const GURL& url) {
  if (profile_->GetNetworkPredictor())
    profile_->GetNetworkPredictor()->PreconnectUrl(
        url, GURL(), chrome_browser_net::UrlInfo::MOUSE_OVER_MOTIVATED, 1);
}

void ChromeRenderMessageFilter::OnResourceTypeStats(
    const WebCache::ResourceTypeStats& stats) {
  HISTOGRAM_COUNTS("WebCoreCache.ImagesSizeKB",
                   static_cast<int>(stats.images.size / 1024));
  HISTOGRAM_COUNTS("WebCoreCache.CSSStylesheetsSizeKB",
                   static_cast<int>(stats.cssStyleSheets.size / 1024));
  HISTOGRAM_COUNTS("WebCoreCache.ScriptsSizeKB",
                   static_cast<int>(stats.scripts.size / 1024));
  HISTOGRAM_COUNTS("WebCoreCache.XSLStylesheetsSizeKB",
                   static_cast<int>(stats.xslStyleSheets.size / 1024));
  HISTOGRAM_COUNTS("WebCoreCache.FontsSizeKB",
                   static_cast<int>(stats.fonts.size / 1024));

  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
#if defined(ENABLE_TASK_MANAGER)
  TaskManager::GetInstance()->model()->NotifyResourceTypeStats(peer_pid(),
                                                               stats);
#endif  // defined(ENABLE_TASK_MANAGER)
}

void ChromeRenderMessageFilter::OnUpdatedCacheStats(
    const WebCache::UsageStats& stats) {
  WebCacheManager::GetInstance()->ObserveStats(render_process_id_, stats);
}

void ChromeRenderMessageFilter::OnFPS(int routing_id, float fps) {
  if (!BrowserThread::CurrentlyOn(BrowserThread::UI)) {
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(
            &ChromeRenderMessageFilter::OnFPS, this,
            routing_id, fps));
    return;
  }

  base::ProcessId renderer_id = peer_pid();

#if defined(ENABLE_TASK_MANAGER)
  TaskManager::GetInstance()->model()->NotifyFPS(
      renderer_id, routing_id, fps);
#endif  // defined(ENABLE_TASK_MANAGER)

  FPSDetails details(routing_id, fps);
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_RENDERER_FPS_COMPUTED,
      content::Source<const base::ProcessId>(&renderer_id),
      content::Details<const FPSDetails>(&details));
}

void ChromeRenderMessageFilter::OnV8HeapStats(int v8_memory_allocated,
                                              int v8_memory_used) {
  if (!BrowserThread::CurrentlyOn(BrowserThread::UI)) {
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(&ChromeRenderMessageFilter::OnV8HeapStats, this,
                   v8_memory_allocated, v8_memory_used));
    return;
  }

  base::ProcessId renderer_id = peer_pid();

#if defined(ENABLE_TASK_MANAGER)
  TaskManager::GetInstance()->model()->NotifyV8HeapStats(
      renderer_id,
      static_cast<size_t>(v8_memory_allocated),
      static_cast<size_t>(v8_memory_used));
#endif  // defined(ENABLE_TASK_MANAGER)

  V8HeapStatsDetails details(v8_memory_allocated, v8_memory_used);
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_RENDERER_V8_HEAP_STATS_COMPUTED,
      content::Source<const base::ProcessId>(&renderer_id),
      content::Details<const V8HeapStatsDetails>(&details));
}

void ChromeRenderMessageFilter::OnOpenChannelToExtension(
    int routing_id,
    const ExtensionMsg_ExternalConnectionInfo& info,
    const std::string& channel_name, int* port_id) {
  int port2_id;
  extensions::MessageService::AllocatePortIdPair(port_id, &port2_id);

  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&ChromeRenderMessageFilter::OpenChannelToExtensionOnUIThread,
                 this, render_process_id_, routing_id, port2_id, info,
                 channel_name));
}

void ChromeRenderMessageFilter::OpenChannelToExtensionOnUIThread(
    int source_process_id, int source_routing_id,
    int receiver_port_id,
    const ExtensionMsg_ExternalConnectionInfo& info,
    const std::string& channel_name) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  extensions::MessageService::Get(profile_)->OpenChannelToExtension(
      source_process_id, source_routing_id, receiver_port_id,
      info.source_id, info.target_id, info.source_url, channel_name);
}

void ChromeRenderMessageFilter::OnOpenChannelToNativeApp(
    int routing_id,
    const std::string& source_extension_id,
    const std::string& native_app_name,
    int* port_id) {
  int port2_id;
  extensions::MessageService::AllocatePortIdPair(port_id, &port2_id);

  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&ChromeRenderMessageFilter::OpenChannelToNativeAppOnUIThread,
                 this, routing_id, port2_id, source_extension_id,
                 native_app_name));
}

void ChromeRenderMessageFilter::OpenChannelToNativeAppOnUIThread(
    int source_routing_id,
    int receiver_port_id,
    const std::string& source_extension_id,
    const std::string& native_app_name) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  extensions::MessageService::Get(profile_)->OpenChannelToNativeApp(
      render_process_id_, source_routing_id, receiver_port_id,
      source_extension_id, native_app_name);
}

void ChromeRenderMessageFilter::OnOpenChannelToTab(
    int routing_id, int tab_id, const std::string& extension_id,
    const std::string& channel_name, int* port_id) {
  int port2_id;
  extensions::MessageService::AllocatePortIdPair(port_id, &port2_id);

  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&ChromeRenderMessageFilter::OpenChannelToTabOnUIThread, this,
                 render_process_id_, routing_id, port2_id, tab_id, extension_id,
                 channel_name));
}

void ChromeRenderMessageFilter::OpenChannelToTabOnUIThread(
    int source_process_id, int source_routing_id,
    int receiver_port_id,
    int tab_id,
    const std::string& extension_id,
    const std::string& channel_name) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  extensions::MessageService::Get(profile_)->OpenChannelToTab(
      source_process_id, source_routing_id, receiver_port_id,
      tab_id, extension_id, channel_name);
}

void ChromeRenderMessageFilter::OnGetExtensionMessageBundle(
    const std::string& extension_id, IPC::Message* reply_msg) {
  const extensions::Extension* extension =
      extension_info_map_->extensions().GetByID(extension_id);
  base::FilePath extension_path;
  std::string default_locale;
  if (extension) {
    extension_path = extension->path();
    default_locale = extensions::LocaleInfo::GetDefaultLocale(extension);
  }

  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      base::Bind(
          &ChromeRenderMessageFilter::OnGetExtensionMessageBundleOnFileThread,
          this, extension_path, extension_id, default_locale, reply_msg));
}

void ChromeRenderMessageFilter::OnGetExtensionMessageBundleOnFileThread(
    const base::FilePath& extension_path,
    const std::string& extension_id,
    const std::string& default_locale,
    IPC::Message* reply_msg) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));

  scoped_ptr<extensions::MessageBundle::SubstitutionMap> dictionary_map(
      extension_file_util::LoadMessageBundleSubstitutionMap(
          extension_path, extension_id, default_locale));

  ExtensionHostMsg_GetMessageBundle::WriteReplyParams(reply_msg,
                                                      *dictionary_map);
  Send(reply_msg);
}

void ChromeRenderMessageFilter::OnExtensionAddListener(
    const std::string& extension_id,
    const std::string& event_name) {
  content::RenderProcessHost* process =
      content::RenderProcessHost::FromID(render_process_id_);
  if (!process || !extensions::ExtensionSystem::Get(profile_)->event_router())
    return;

  extensions::ExtensionSystem::Get(profile_)->event_router()->AddEventListener(
      event_name, process, extension_id);
}

void ChromeRenderMessageFilter::OnExtensionRemoveListener(
    const std::string& extension_id,
    const std::string& event_name) {
  content::RenderProcessHost* process =
      content::RenderProcessHost::FromID(render_process_id_);
  if (!process || !extensions::ExtensionSystem::Get(profile_)->event_router())
    return;

  extensions::ExtensionSystem::Get(profile_)->event_router()->
      RemoveEventListener(event_name, process, extension_id);
}

void ChromeRenderMessageFilter::OnExtensionAddLazyListener(
    const std::string& extension_id, const std::string& event_name) {
  if (extensions::ExtensionSystem::Get(profile_)->event_router()) {
    extensions::ExtensionSystem::Get(profile_)->event_router()->
        AddLazyEventListener(event_name, extension_id);
  }
}

void ChromeRenderMessageFilter::OnExtensionRemoveLazyListener(
    const std::string& extension_id, const std::string& event_name) {
  if (extensions::ExtensionSystem::Get(profile_)->event_router()) {
    extensions::ExtensionSystem::Get(profile_)->event_router()->
        RemoveLazyEventListener(event_name, extension_id);
  }
}

void ChromeRenderMessageFilter::OnExtensionAddFilteredListener(
    const std::string& extension_id,
    const std::string& event_name,
    const base::DictionaryValue& filter,
    bool lazy) {
  content::RenderProcessHost* process =
      content::RenderProcessHost::FromID(render_process_id_);
  if (!process || !extensions::ExtensionSystem::Get(profile_)->event_router())
    return;

  extensions::ExtensionSystem::Get(profile_)->event_router()->
      AddFilteredEventListener(event_name, process, extension_id, filter, lazy);
}

void ChromeRenderMessageFilter::OnExtensionRemoveFilteredListener(
    const std::string& extension_id,
    const std::string& event_name,
    const base::DictionaryValue& filter,
    bool lazy) {
  content::RenderProcessHost* process =
      content::RenderProcessHost::FromID(render_process_id_);
  if (!process || !extensions::ExtensionSystem::Get(profile_)->event_router())
    return;

  extensions::ExtensionSystem::Get(profile_)->event_router()->
      RemoveFilteredEventListener(event_name, process, extension_id, filter,
                                  lazy);
}

void ChromeRenderMessageFilter::OnExtensionCloseChannel(
    int port_id,
    const std::string& error_message) {
  if (!content::RenderProcessHost::FromID(render_process_id_))
    return;  // To guard against crash in browser_tests shutdown.

  extensions::MessageService* message_service =
      extensions::MessageService::Get(profile_);
  if (message_service)
    message_service->CloseChannel(port_id, error_message);
}

void ChromeRenderMessageFilter::OnExtensionRequestForIOThread(
    int routing_id,
    const ExtensionHostMsg_Request_Params& params) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  ExtensionFunctionDispatcher::DispatchOnIOThread(
      extension_info_map_.get(), profile_, render_process_id_,
      weak_ptr_factory_.GetWeakPtr(), routing_id, params);
}

void ChromeRenderMessageFilter::OnExtensionShouldSuspendAck(
     const std::string& extension_id, int sequence_id) {
  if (extensions::ExtensionSystem::Get(profile_)->process_manager()) {
    extensions::ExtensionSystem::Get(profile_)->process_manager()->
        OnShouldSuspendAck(extension_id, sequence_id);
  }
}

void ChromeRenderMessageFilter::OnExtensionSuspendAck(
     const std::string& extension_id) {
  if (extensions::ExtensionSystem::Get(profile_)->process_manager()) {
    extensions::ExtensionSystem::Get(profile_)->process_manager()->
        OnSuspendAck(extension_id);
  }
}

void ChromeRenderMessageFilter::OnExtensionGenerateUniqueID(int* unique_id) {
  static int next_unique_id = 0;
  *unique_id = ++next_unique_id;
}

void ChromeRenderMessageFilter::OnExtensionResumeRequests(int route_id) {
  content::ResourceDispatcherHost::Get()->ResumeBlockedRequestsForRoute(
      render_process_id_, route_id);
}

void ChromeRenderMessageFilter::OnAddAPIActionToExtensionActivityLog(
    const std::string& extension_id,
    const ExtensionHostMsg_APIActionOrEvent_Params& params) {
  scoped_refptr<extensions::Action> action = new extensions::Action(
      extension_id, base::Time::Now(), extensions::Action::ACTION_API_CALL,
      params.api_call);
  action->set_args(make_scoped_ptr(params.arguments.DeepCopy()));
  if (!params.extra.empty()) {
    action->mutable_other()->SetString(
        activity_log_constants::kActionExtra, params.extra);
  }
  AddActionToExtensionActivityLog(profile_, action);
}

void ChromeRenderMessageFilter::OnAddDOMActionToExtensionActivityLog(
    const std::string& extension_id,
    const ExtensionHostMsg_DOMAction_Params& params) {
  scoped_refptr<extensions::Action> action = new extensions::Action(
      extension_id, base::Time::Now(), extensions::Action::ACTION_DOM_ACCESS,
      params.api_call);
  action->set_args(make_scoped_ptr(params.arguments.DeepCopy()));
  action->set_page_url(params.url);
  action->set_page_title(base::UTF16ToUTF8(params.url_title));
  action->mutable_other()->SetInteger(activity_log_constants::kActionDomVerb,
                                      params.call_type);
  AddActionToExtensionActivityLog(profile_, action);
}

void ChromeRenderMessageFilter::OnAddEventToExtensionActivityLog(
    const std::string& extension_id,
    const ExtensionHostMsg_APIActionOrEvent_Params& params) {
  scoped_refptr<extensions::Action> action = new extensions::Action(
      extension_id, base::Time::Now(), extensions::Action::ACTION_API_EVENT,
      params.api_call);
  action->set_args(make_scoped_ptr(params.arguments.DeepCopy()));
  if (!params.extra.empty()) {
    action->mutable_other()->SetString(activity_log_constants::kActionExtra,
                                       params.extra);
  }
  AddActionToExtensionActivityLog(profile_, action);
}

void ChromeRenderMessageFilter::OnAddBlockedCallToExtensionActivityLog(
    const std::string& extension_id,
    const std::string& function_name) {
  scoped_refptr<extensions::Action> action = new extensions::Action(
      extension_id, base::Time::Now(), extensions::Action::ACTION_API_BLOCKED,
      function_name);
  action->mutable_other()->SetString(
      activity_log_constants::kActionBlockedReason,
      BlockedChromeActivityDetail::ToString(
          BlockedChromeActivityDetail::REASON_ACCESS_DENIED));
  AddActionToExtensionActivityLog(profile_, action);
}

void ChromeRenderMessageFilter::OnAllowDatabase(int render_view_id,
                                                const GURL& origin_url,
                                                const GURL& top_origin_url,
                                                const string16& name,
                                                const string16& display_name,
                                                bool* allowed) {
  *allowed =
      cookie_settings_->IsSettingCookieAllowed(origin_url, top_origin_url);
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&TabSpecificContentSettings::WebDatabaseAccessed,
                 render_process_id_, render_view_id, origin_url, name,
                 display_name, !*allowed));
}

void ChromeRenderMessageFilter::OnAllowDOMStorage(int render_view_id,
                                                  const GURL& origin_url,
                                                  const GURL& top_origin_url,
                                                  bool local,
                                                  bool* allowed) {
  *allowed =
      cookie_settings_->IsSettingCookieAllowed(origin_url, top_origin_url);
  // Record access to DOM storage for potential display in UI.
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&TabSpecificContentSettings::DOMStorageAccessed,
                 render_process_id_, render_view_id, origin_url, local,
                 !*allowed));
}

void ChromeRenderMessageFilter::OnAllowFileSystem(int render_view_id,
                                                  const GURL& origin_url,
                                                  const GURL& top_origin_url,
                                                  bool* allowed) {
  *allowed =
      cookie_settings_->IsSettingCookieAllowed(origin_url, top_origin_url);
  // Record access to file system for potential display in UI.
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&TabSpecificContentSettings::FileSystemAccessed,
                 render_process_id_, render_view_id, origin_url, !*allowed));
}

void ChromeRenderMessageFilter::OnAllowIndexedDB(int render_view_id,
                                                 const GURL& origin_url,
                                                 const GURL& top_origin_url,
                                                 const string16& name,
                                                 bool* allowed) {
  *allowed =
      cookie_settings_->IsSettingCookieAllowed(origin_url, top_origin_url);
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&TabSpecificContentSettings::IndexedDBAccessed,
                 render_process_id_, render_view_id, origin_url, name,
                 !*allowed));
}

void ChromeRenderMessageFilter::OnCanTriggerClipboardRead(
    const GURL& origin, bool* allowed) {
  *allowed = extension_info_map_->SecurityOriginHasAPIPermission(
      origin, render_process_id_, APIPermission::kClipboardRead);
}

void ChromeRenderMessageFilter::OnCanTriggerClipboardWrite(
    const GURL& origin, bool* allowed) {
  // Since all extensions could historically write to the clipboard, preserve it
  // for compatibility.
  *allowed = (origin.SchemeIs(extensions::kExtensionScheme) ||
      extension_info_map_->SecurityOriginHasAPIPermission(
          origin, render_process_id_, APIPermission::kClipboardWrite));
}

void ChromeRenderMessageFilter::OnGetCookies(
    const GURL& url,
    const GURL& first_party_for_cookies,
    IPC::Message* reply_msg) {
#if defined(ENABLE_AUTOMATION)
  AutomationResourceMessageFilter::GetCookiesForUrl(
      this, request_context_->GetURLRequestContext(), render_process_id_,
      reply_msg, url);
#endif
}

void ChromeRenderMessageFilter::OnSetCookie(const IPC::Message& message,
                                            const GURL& url,
                                            const GURL& first_party_for_cookies,
                                            const std::string& cookie) {
#if defined(ENABLE_AUTOMATION)
  AutomationResourceMessageFilter::SetCookiesForUrl(
      render_process_id_, message.routing_id(), url, cookie);
#endif
}
