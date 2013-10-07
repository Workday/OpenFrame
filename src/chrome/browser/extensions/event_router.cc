// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/event_router.h"

#include <utility>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/message_loop/message_loop.h"
#include "base/stl_util.h"
#include "base/values.h"
#include "base/version.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/activity_log/activity_log.h"
#include "chrome/browser/extensions/api/runtime/runtime_api.h"
#include "chrome/browser/extensions/api/web_request/web_request_api.h"
#include "chrome/browser/extensions/event_names.h"
#include "chrome/browser/extensions/extension_host.h"
#include "chrome/browser/extensions/extension_prefs.h"
#include "chrome/browser/extensions/extension_process_manager.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/extensions/lazy_background_task_queue.h"
#include "chrome/browser/extensions/process_map.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/chrome_version_info.h"
#include "chrome/common/extensions/api/extension_api.h"
#include "chrome/common/extensions/background_info.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_messages.h"
#include "chrome/common/extensions/incognito_handler.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/render_process_host.h"

using base::DictionaryValue;
using base::ListValue;
using content::BrowserThread;

namespace extensions {

namespace {

void NotifyEventListenerRemovedOnIOThread(
    void* profile,
    const std::string& extension_id,
    const std::string& sub_event_name) {
  ExtensionWebRequestEventRouter::GetInstance()->RemoveEventListener(
      profile, extension_id, sub_event_name);
}

void DispatchOnInstalledEvent(
    Profile* profile,
    const std::string& extension_id,
    const Version& old_version,
    bool chrome_updated) {
  if (!g_browser_process->profile_manager()->IsValidProfile(profile))
    return;

  RuntimeEventRouter::DispatchOnInstalledEvent(profile, extension_id,
                                               old_version, chrome_updated);
}

void DoNothing(ExtensionHost* host) {}

// A dictionary of event names to lists of filters that this extension has
// registered from its lazy background page.
const char kFilteredEvents[] = "filtered_events";

}  // namespace

const char EventRouter::kRegisteredEvents[] = "events";

struct EventRouter::ListenerProcess {
  content::RenderProcessHost* process;
  std::string extension_id;

  ListenerProcess(content::RenderProcessHost* process,
                  const std::string& extension_id)
      : process(process), extension_id(extension_id) {}

  bool operator<(const ListenerProcess& that) const {
    if (process < that.process)
      return true;
    if (process == that.process && extension_id < that.extension_id)
      return true;
    return false;
  }
};

// static
void EventRouter::LogExtensionEventMessage(void* profile_id,
                                           const std::string& extension_id,
                                           const std::string& event_name,
                                           scoped_ptr<ListValue> event_args) {
  if (!BrowserThread::CurrentlyOn(BrowserThread::UI)) {
    BrowserThread::PostTask(BrowserThread::UI,
                            FROM_HERE,
                            base::Bind(&LogExtensionEventMessage,
                                       profile_id,
                                       extension_id,
                                       event_name,
                                       base::Passed(&event_args)));
  } else {
    Profile* profile = reinterpret_cast<Profile*>(profile_id);
    if (!g_browser_process->profile_manager()->IsValidProfile(profile))
      return;
    scoped_refptr<Action> action = new Action(
        extension_id, base::Time::Now(), Action::ACTION_API_EVENT, event_name);
    action->set_args(event_args.Pass());
    ActivityLog::GetInstance(profile)->LogAction(action);
  }
}

// static
void EventRouter::DispatchExtensionMessage(IPC::Sender* ipc_sender,
                                           void* profile_id,
                                           const std::string& extension_id,
                                           const std::string& event_name,
                                           ListValue* event_args,
                                           UserGestureState user_gesture,
                                           const EventFilteringInfo& info) {
  if (ActivityLog::IsLogEnabledOnAnyProfile()) {
    LogExtensionEventMessage(profile_id, extension_id, event_name,
                             scoped_ptr<ListValue>(event_args->DeepCopy()));
  }

  ListValue args;
  args.Set(0, Value::CreateStringValue(event_name));
  args.Set(1, event_args);
  args.Set(2, info.AsValue().release());
  ipc_sender->Send(new ExtensionMsg_MessageInvoke(
      MSG_ROUTING_CONTROL,
      extension_id,
      "event_bindings",
      "dispatchEvent",
      args,
      user_gesture == USER_GESTURE_ENABLED));

  // DispatchExtensionMessage does _not_ take ownership of event_args, so we
  // must ensure that the destruction of args does not attempt to free it.
  scoped_ptr<Value> removed_event_args;
  args.Remove(1, &removed_event_args);
  ignore_result(removed_event_args.release());
}

// static
void EventRouter::DispatchEvent(IPC::Sender* ipc_sender,
                                void* profile_id,
                                const std::string& extension_id,
                                const std::string& event_name,
                                scoped_ptr<ListValue> event_args,
                                UserGestureState user_gesture,
                                const EventFilteringInfo& info) {
  DispatchExtensionMessage(ipc_sender, profile_id, extension_id, event_name,
                           event_args.get(), user_gesture, info);

  BrowserThread::PostTask(
      BrowserThread::UI,
      FROM_HERE,
      base::Bind(&EventRouter::IncrementInFlightEventsOnUI,
                  profile_id,
                  extension_id));
}

EventRouter::EventRouter(Profile* profile, ExtensionPrefs* extension_prefs)
    : profile_(profile),
      listeners_(this),
      dispatch_chrome_updated_event_(false) {
  registrar_.Add(this, content::NOTIFICATION_RENDERER_PROCESS_TERMINATED,
                 content::NotificationService::AllSources());
  registrar_.Add(this, content::NOTIFICATION_RENDERER_PROCESS_CLOSED,
                 content::NotificationService::AllSources());
  registrar_.Add(this, chrome::NOTIFICATION_EXTENSIONS_READY,
                 content::Source<Profile>(profile_));
  registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_ENABLED,
                 content::Source<Profile>(profile_));
  registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_LOADED,
                 content::Source<Profile>(profile_));
  registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_UNLOADED,
                 content::Source<Profile>(profile_));
  registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_INSTALLED,
                 content::Source<Profile>(profile_));

  // NULL in unit_tests.
  if (extension_prefs) {
    // Check if registered events are up-to-date. We can only do this once
    // per profile, since it updates internal state when called.
    dispatch_chrome_updated_event_ = !CheckRegisteredEventsUpToDate();
  }
}

EventRouter::~EventRouter() {}

void EventRouter::AddEventListener(const std::string& event_name,
                                   content::RenderProcessHost* process,
                                   const std::string& extension_id) {
  listeners_.AddListener(scoped_ptr<EventListener>(new EventListener(
      event_name, extension_id, process, scoped_ptr<DictionaryValue>())));
}

void EventRouter::RemoveEventListener(const std::string& event_name,
                                      content::RenderProcessHost* process,
                                      const std::string& extension_id) {
  EventListener listener(event_name, extension_id, process,
                         scoped_ptr<DictionaryValue>());
  listeners_.RemoveListener(&listener);
}

void EventRouter::RegisterObserver(Observer* observer,
                                   const std::string& event_name) {
  observers_[event_name] = observer;
}

void EventRouter::UnregisterObserver(Observer* observer) {
  std::vector<ObserverMap::iterator> iters_to_remove;
  for (ObserverMap::iterator iter = observers_.begin();
       iter != observers_.end(); ++iter) {
    if (iter->second == observer)
      iters_to_remove.push_back(iter);
  }
  for (size_t i = 0; i < iters_to_remove.size(); ++i)
    observers_.erase(iters_to_remove[i]);
}

void EventRouter::OnListenerAdded(const EventListener* listener) {
  const std::string& event_name = listener->event_name;
  const EventListenerInfo details(event_name, listener->extension_id);
  ObserverMap::iterator observer = observers_.find(event_name);
  if (observer != observers_.end())
    observer->second->OnListenerAdded(details);

#if 0
  // TODO(felt): Experimentally determine if these are needed, or if they
  // can be permanently removed. Temporarily removing for now to reduce log
  // size while under investigation.
  const Extension* extension = ExtensionSystem::Get(profile_)->
      extension_service()->GetExtensionById(listener->extension_id,
                                            ExtensionService::INCLUDE_ENABLED);
  if (extension) {
    scoped_ptr<ListValue> args(new ListValue());
    if (listener->filter)
      args->Append(listener->filter->DeepCopy());
    ActivityLog::GetInstance(profile)->LogAPIAction(
        extension, event_name + ".addListener", args.get(), std::string());
  }
#endif
}

void EventRouter::OnListenerRemoved(const EventListener* listener) {
  const std::string& event_name = listener->event_name;
  const EventListenerInfo details(event_name, listener->extension_id);
  ObserverMap::iterator observer = observers_.find(event_name);
  if (observer != observers_.end())
    observer->second->OnListenerRemoved(details);

  void* profile =
      listener->process
          ? Profile::FromBrowserContext(listener->process->GetBrowserContext())
          : NULL;
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&NotifyEventListenerRemovedOnIOThread,
                 profile, listener->extension_id, event_name));
#if 0
  // TODO(felt): Experimentally determine if these are needed, or if they
  // can be permanently removed. Temporarily removing for now to reduce log
  // size while under investigation.
  const Extension* extension = ExtensionSystem::Get(profile_)->
      extension_service()->GetExtensionById(listener->extension_id,
                                            ExtensionService::INCLUDE_ENABLED);
  if (extension) {
    scoped_ptr<ListValue> args(new ListValue());
    ActivityLog::GetInstance(profile)->LogAPIAction(
        extension, event_name + ".removeListener", args.get(), std::string());
  }
#endif
}

void EventRouter::AddLazyEventListener(const std::string& event_name,
                                       const std::string& extension_id) {
  scoped_ptr<EventListener> listener(new EventListener(
      event_name, extension_id, NULL, scoped_ptr<DictionaryValue>()));
  bool is_new = listeners_.AddListener(listener.Pass());

  if (is_new) {
    std::set<std::string> events = GetRegisteredEvents(extension_id);
    bool prefs_is_new = events.insert(event_name).second;
    if (prefs_is_new)
      SetRegisteredEvents(extension_id, events);
  }
}

void EventRouter::RemoveLazyEventListener(const std::string& event_name,
                                          const std::string& extension_id) {
  EventListener listener(event_name, extension_id, NULL,
                         scoped_ptr<DictionaryValue>());
  bool did_exist = listeners_.RemoveListener(&listener);

  if (did_exist) {
    std::set<std::string> events = GetRegisteredEvents(extension_id);
    bool prefs_did_exist = events.erase(event_name) > 0;
    DCHECK(prefs_did_exist);
    SetRegisteredEvents(extension_id, events);
  }
}

void EventRouter::AddFilteredEventListener(const std::string& event_name,
                                           content::RenderProcessHost* process,
                                           const std::string& extension_id,
                                           const base::DictionaryValue& filter,
                                           bool add_lazy_listener) {
  listeners_.AddListener(scoped_ptr<EventListener>(new EventListener(
      event_name, extension_id, process,
      scoped_ptr<DictionaryValue>(filter.DeepCopy()))));

  if (add_lazy_listener) {
    bool added = listeners_.AddListener(scoped_ptr<EventListener>(
        new EventListener(event_name, extension_id, NULL,
        scoped_ptr<DictionaryValue>(filter.DeepCopy()))));

    if (added)
      AddFilterToEvent(event_name, extension_id, &filter);
  }
}

void EventRouter::RemoveFilteredEventListener(
    const std::string& event_name,
    content::RenderProcessHost* process,
    const std::string& extension_id,
    const base::DictionaryValue& filter,
    bool remove_lazy_listener) {
  EventListener listener(event_name, extension_id, process,
                         scoped_ptr<DictionaryValue>(filter.DeepCopy()));

  listeners_.RemoveListener(&listener);

  if (remove_lazy_listener) {
    listener.process = NULL;
    bool removed = listeners_.RemoveListener(&listener);

    if (removed)
      RemoveFilterFromEvent(event_name, extension_id, &filter);
  }
}

bool EventRouter::HasEventListener(const std::string& event_name) {
  return listeners_.HasListenerForEvent(event_name);
}

bool EventRouter::ExtensionHasEventListener(const std::string& extension_id,
                                            const std::string& event_name) {
  return listeners_.HasListenerForExtension(extension_id, event_name);
}

bool EventRouter::HasEventListenerImpl(const ListenerMap& listener_map,
                                       const std::string& extension_id,
                                       const std::string& event_name) {
  ListenerMap::const_iterator it = listener_map.find(event_name);
  if (it == listener_map.end())
    return false;

  const std::set<ListenerProcess>& listeners = it->second;
  if (extension_id.empty())
    return !listeners.empty();

  for (std::set<ListenerProcess>::const_iterator listener = listeners.begin();
       listener != listeners.end(); ++listener) {
    if (listener->extension_id == extension_id)
      return true;
  }
  return false;
}

std::set<std::string> EventRouter::GetRegisteredEvents(
    const std::string& extension_id) {
  std::set<std::string> events;
  const ListValue* events_value = NULL;
  ExtensionPrefs* prefs = ExtensionPrefs::Get(profile_);

  if (!prefs ||
      !prefs->ReadPrefAsList(extension_id, kRegisteredEvents, &events_value)) {
    return events;
  }

  for (size_t i = 0; i < events_value->GetSize(); ++i) {
    std::string event;
    if (events_value->GetString(i, &event))
      events.insert(event);
  }
  return events;
}

void EventRouter::SetRegisteredEvents(const std::string& extension_id,
                                      const std::set<std::string>& events) {
  ListValue* events_value = new ListValue;
  for (std::set<std::string>::const_iterator iter = events.begin();
       iter != events.end(); ++iter) {
    events_value->Append(new StringValue(*iter));
  }
  ExtensionPrefs::Get(profile_)->UpdateExtensionPref(
      extension_id,
      kRegisteredEvents,
      events_value);
}

bool EventRouter::CheckRegisteredEventsUpToDate() {
  // If we're running inside a test, then assume prefs are all up-to-date.
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kTestType))
    return true;

  base::Version version;
  PrefService* pref_service = profile_->GetPrefs();
  if (pref_service->HasPrefPath(ExtensionPrefs::kExtensionsLastChromeVersion)) {
    std::string version_str =
        pref_service->GetString(ExtensionPrefs::kExtensionsLastChromeVersion);
    version = Version(version_str);
  }

  chrome::VersionInfo current_version_info;
  std::string current_version = current_version_info.Version();
  pref_service->SetString(ExtensionPrefs::kExtensionsLastChromeVersion,
                          current_version);

  // If there was no version string in prefs, assume we're out of date.
  if (!version.IsValid() || version.IsOlderThan(current_version))
    return false;

  return true;
}

void EventRouter::AddFilterToEvent(const std::string& event_name,
                                   const std::string& extension_id,
                                   const DictionaryValue* filter) {
  ExtensionPrefs::ScopedDictionaryUpdate update(
      ExtensionPrefs::Get(profile_),
      extension_id,
      kFilteredEvents);
  DictionaryValue* filtered_events = update.Get();
  if (!filtered_events)
    filtered_events = update.Create();

  ListValue* filter_list = NULL;
  if (!filtered_events->GetList(event_name, &filter_list)) {
    filter_list = new ListValue;
    filtered_events->SetWithoutPathExpansion(event_name, filter_list);
  }

  filter_list->Append(filter->DeepCopy());
}

void EventRouter::RemoveFilterFromEvent(const std::string& event_name,
                                        const std::string& extension_id,
                                        const DictionaryValue* filter) {
  ExtensionPrefs::ScopedDictionaryUpdate update(
      ExtensionPrefs::Get(profile_),
      extension_id,
      kFilteredEvents);
  DictionaryValue* filtered_events = update.Get();
  ListValue* filter_list = NULL;
  if (!filtered_events ||
      !filtered_events->GetListWithoutPathExpansion(event_name, &filter_list)) {
    return;
  }

  for (size_t i = 0; i < filter_list->GetSize(); i++) {
    DictionaryValue* filter = NULL;
    CHECK(filter_list->GetDictionary(i, &filter));
    if (filter->Equals(filter)) {
      filter_list->Remove(i, NULL);
      break;
    }
  }
}

const DictionaryValue* EventRouter::GetFilteredEvents(
    const std::string& extension_id) {
  const DictionaryValue* events = NULL;
  ExtensionPrefs::Get(profile_)->ReadPrefAsDictionary(
      extension_id, kFilteredEvents, &events);
  return events;
}

void EventRouter::BroadcastEvent(scoped_ptr<Event> event) {
  DispatchEventImpl(std::string(), linked_ptr<Event>(event.release()));
}

void EventRouter::DispatchEventToExtension(const std::string& extension_id,
                                           scoped_ptr<Event> event) {
  DCHECK(!extension_id.empty());
  DispatchEventImpl(extension_id, linked_ptr<Event>(event.release()));
}

void EventRouter::DispatchEventWithLazyListener(const std::string& extension_id,
                                                scoped_ptr<Event> event) {
  DCHECK(!extension_id.empty());
  std::string event_name = event->event_name;
  bool has_listener = ExtensionHasEventListener(extension_id, event_name);
  if (!has_listener)
    AddLazyEventListener(event_name, extension_id);
  DispatchEventToExtension(extension_id, event.Pass());
  if (!has_listener)
    RemoveLazyEventListener(event_name, extension_id);
}

void EventRouter::DispatchEventImpl(const std::string& restrict_to_extension_id,
                                    const linked_ptr<Event>& event) {
  // We don't expect to get events from a completely different profile.
  DCHECK(!event->restrict_to_profile ||
         profile_->IsSameProfile(event->restrict_to_profile));

  std::set<const EventListener*> listeners(
      listeners_.GetEventListeners(*event));

  std::set<EventDispatchIdentifier> already_dispatched;

  // We dispatch events for lazy background pages first because attempting to do
  // so will cause those that are being suspended to cancel that suspension.
  // As canceling a suspension entails sending an event to the affected
  // background page, and as that event needs to be delivered before we dispatch
  // the event we are dispatching here, we dispatch to the lazy listeners here
  // first.
  for (std::set<const EventListener*>::iterator it = listeners.begin();
       it != listeners.end(); it++) {
    const EventListener* listener = *it;
    if (restrict_to_extension_id.empty() ||
        restrict_to_extension_id == listener->extension_id) {
      if (!listener->process) {
        DispatchLazyEvent(listener->extension_id, event, &already_dispatched);
      }
    }
  }

  for (std::set<const EventListener*>::iterator it = listeners.begin();
       it != listeners.end(); it++) {
    const EventListener* listener = *it;
    if (restrict_to_extension_id.empty() ||
        restrict_to_extension_id == listener->extension_id) {
      if (listener->process) {
        EventDispatchIdentifier dispatch_id(
            listener->process->GetBrowserContext(), listener->extension_id);
        if (!ContainsKey(already_dispatched, dispatch_id)) {
          DispatchEventToProcess(listener->extension_id, listener->process,
              event);
        }
      }
    }
  }
}

void EventRouter::DispatchLazyEvent(
    const std::string& extension_id,
    const linked_ptr<Event>& event,
    std::set<EventDispatchIdentifier>* already_dispatched) {
  ExtensionService* service =
      ExtensionSystem::Get(profile_)->extension_service();
  // Check both the original and the incognito profile to see if we
  // should load a lazy bg page to handle the event. The latter case
  // occurs in the case of split-mode extensions.
  const Extension* extension = service->extensions()->GetByID(extension_id);
  if (extension) {
    if (MaybeLoadLazyBackgroundPageToDispatchEvent(
          profile_, extension, event)) {
      already_dispatched->insert(std::make_pair(profile_, extension_id));
    }

    if (profile_->HasOffTheRecordProfile() &&
        IncognitoInfo::IsSplitMode(extension)) {
      if (MaybeLoadLazyBackgroundPageToDispatchEvent(
          profile_->GetOffTheRecordProfile(), extension, event)) {
        already_dispatched->insert(
            std::make_pair(profile_->GetOffTheRecordProfile(), extension_id));
      }
    }
  }
}

void EventRouter::DispatchEventToProcess(const std::string& extension_id,
                                         content::RenderProcessHost* process,
                                         const linked_ptr<Event>& event) {
  ExtensionService* service =
      ExtensionSystem::Get(profile_)->extension_service();
  const Extension* extension = service->extensions()->GetByID(extension_id);

  // The extension could have been removed, but we do not unregister it until
  // the extension process is unloaded.
  if (!extension)
    return;

  Profile* listener_profile = Profile::FromBrowserContext(
      process->GetBrowserContext());
  ProcessMap* process_map = ExtensionSystem::Get(listener_profile)->
      extension_service()->process_map();
  // If the event is privileged, only send to extension processes. Otherwise,
  // it's OK to send to normal renderers (e.g., for content scripts).
  if (ExtensionAPI::GetSharedInstance()->IsPrivileged(event->event_name) &&
      !process_map->Contains(extension->id(), process->GetID())) {
    return;
  }

  // If the event is restricted to a URL, only dispatch if the extension has
  // permission for it (or if the event originated from itself).
  if (!event->event_url.is_empty() &&
      event->event_url.host() != extension->id() &&
      !extension->GetActivePermissions()->HasEffectiveAccessToURL(
          event->event_url)) {
    return;
  }

  if (!CanDispatchEventToProfile(listener_profile, extension, event))
    return;

  if (!event->will_dispatch_callback.is_null()) {
    event->will_dispatch_callback.Run(listener_profile, extension,
                                      event->event_args.get());
  }

  DispatchExtensionMessage(process, listener_profile, extension->id(),
                           event->event_name, event->event_args.get(),
                           event->user_gesture, event->filter_info);
  IncrementInFlightEvents(listener_profile, extension);
}

bool EventRouter::CanDispatchEventToProfile(Profile* profile,
                                            const Extension* extension,
                                            const linked_ptr<Event>& event) {
  // Is this event from a different profile than the renderer (ie, an
  // incognito tab event sent to a normal process, or vice versa).
  bool cross_incognito =
      event->restrict_to_profile && profile != event->restrict_to_profile;
  if (cross_incognito &&
      !ExtensionSystem::Get(profile)->extension_service()->
          CanCrossIncognito(extension)) {
    return false;
  }

  return true;
}

bool EventRouter::MaybeLoadLazyBackgroundPageToDispatchEvent(
    Profile* profile,
    const Extension* extension,
    const linked_ptr<Event>& event) {
  if (!CanDispatchEventToProfile(profile, extension, event))
    return false;

  LazyBackgroundTaskQueue* queue =
      ExtensionSystem::Get(profile)->lazy_background_task_queue();
  if (queue->ShouldEnqueueTask(profile, extension)) {
    linked_ptr<Event> dispatched_event(event);

    // If there's a dispatch callback, call it now (rather than dispatch time)
    // to avoid lifetime issues. Use a separate copy of the event args, so they
    // last until the event is dispatched.
    if (!event->will_dispatch_callback.is_null()) {
      dispatched_event.reset(event->DeepCopy());
      dispatched_event->will_dispatch_callback.Run(
          profile, extension, dispatched_event->event_args.get());
      // Ensure we don't call it again at dispatch time.
      dispatched_event->will_dispatch_callback.Reset();
    }

    queue->AddPendingTask(profile, extension->id(),
                          base::Bind(&EventRouter::DispatchPendingEvent,
                                     base::Unretained(this), dispatched_event));
    return true;
  }

  return false;
}

// static
void EventRouter::IncrementInFlightEventsOnUI(
    void* profile_id,
    const std::string& extension_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  Profile* profile = reinterpret_cast<Profile*>(profile_id);
  if (!g_browser_process->profile_manager()->IsValidProfile(profile))
    return;
  extensions::EventRouter* event_router =
      extensions::ExtensionSystem::Get(profile)->event_router();
  if (!event_router)
    return;
  ExtensionService* extension_service =
      extensions::ExtensionSystem::Get(profile)->extension_service();
  const Extension* extension =
      extension_service->extensions()->GetByID(extension_id);
  if (!extension)
    return;
  event_router->IncrementInFlightEvents(profile, extension);
}

void EventRouter::IncrementInFlightEvents(Profile* profile,
                                          const Extension* extension) {
  // Only increment in-flight events if the lazy background page is active,
  // because that's the only time we'll get an ACK.
  if (BackgroundInfo::HasLazyBackgroundPage(extension)) {
    ExtensionProcessManager* pm =
        ExtensionSystem::Get(profile)->process_manager();
    ExtensionHost* host = pm->GetBackgroundHostForExtension(extension->id());
    if (host)
      pm->IncrementLazyKeepaliveCount(extension);
  }
}

void EventRouter::OnEventAck(Profile* profile,
                             const std::string& extension_id) {
  ExtensionProcessManager* pm =
      ExtensionSystem::Get(profile)->process_manager();
  ExtensionHost* host = pm->GetBackgroundHostForExtension(extension_id);
  // The event ACK is routed to the background host, so this should never be
  // NULL.
  CHECK(host);
  // TODO(mpcomplete): We should never get this message unless
  // HasLazyBackgroundPage is true. Find out why we're getting it anyway.
  if (host->extension() &&
      BackgroundInfo::HasLazyBackgroundPage(host->extension()))
    pm->DecrementLazyKeepaliveCount(host->extension());
}

void EventRouter::DispatchPendingEvent(const linked_ptr<Event>& event,
                                       ExtensionHost* host) {
  if (!host)
    return;

  if (listeners_.HasProcessListener(host->render_process_host(),
                                    host->extension()->id())) {
    DispatchEventToProcess(host->extension()->id(),
                           host->render_process_host(), event);
  }
}

void EventRouter::Observe(int type,
                          const content::NotificationSource& source,
                          const content::NotificationDetails& details) {
  switch (type) {
    case content::NOTIFICATION_RENDERER_PROCESS_TERMINATED:
    case content::NOTIFICATION_RENDERER_PROCESS_CLOSED: {
      content::RenderProcessHost* renderer =
          content::Source<content::RenderProcessHost>(source).ptr();
      // Remove all event listeners associated with this renderer.
      listeners_.RemoveListenersForProcess(renderer);
      break;
    }
    case chrome::NOTIFICATION_EXTENSIONS_READY: {
      // We're done restarting Chrome after an update.
      dispatch_chrome_updated_event_ = false;
      break;
    }
    case chrome::NOTIFICATION_EXTENSION_ENABLED: {
      // If the extension has a lazy background page, make sure it gets loaded
      // to register the events the extension is interested in.
      const Extension* extension =
          content::Details<const Extension>(details).ptr();
      if (BackgroundInfo::HasLazyBackgroundPage(extension)) {
        LazyBackgroundTaskQueue* queue =
            ExtensionSystem::Get(profile_)->lazy_background_task_queue();
        queue->AddPendingTask(profile_, extension->id(),
                              base::Bind(&DoNothing));
      }
      break;
    }
    case chrome::NOTIFICATION_EXTENSION_LOADED: {
      // Add all registered lazy listeners to our cache.
      const Extension* extension =
          content::Details<const Extension>(details).ptr();
      std::set<std::string> registered_events =
          GetRegisteredEvents(extension->id());
      listeners_.LoadUnfilteredLazyListeners(extension->id(),
                                             registered_events);
      const DictionaryValue* filtered_events =
          GetFilteredEvents(extension->id());
      if (filtered_events)
        listeners_.LoadFilteredLazyListeners(extension->id(), *filtered_events);

      if (dispatch_chrome_updated_event_) {
        base::MessageLoop::current()->PostTask(FROM_HERE,
            base::Bind(&DispatchOnInstalledEvent, profile_, extension->id(),
                       Version(), true));
      }
      break;
    }
    case chrome::NOTIFICATION_EXTENSION_UNLOADED: {
      // Remove all registered lazy listeners from our cache.
      UnloadedExtensionInfo* unloaded =
          content::Details<UnloadedExtensionInfo>(details).ptr();
      listeners_.RemoveLazyListenersForExtension(unloaded->extension->id());
      break;
    }
    case chrome::NOTIFICATION_EXTENSION_INSTALLED: {
      // Dispatch the onInstalled event.
      const Extension* extension =
          content::Details<const InstalledExtensionInfo>(details)->extension;

      // Get the previous version, if this is an upgrade.
      ExtensionService* service =
          ExtensionSystem::Get(profile_)->extension_service();
      const Extension* old = service->GetExtensionById(extension->id(), true);
      Version old_version;
      if (old)
        old_version = *old->version();

      base::MessageLoop::current()->PostTask(FROM_HERE,
          base::Bind(&DispatchOnInstalledEvent, profile_, extension->id(),
                     old_version, false));
      break;
    }
    default:
      NOTREACHED();
      return;
  }
}

Event::Event(const std::string& event_name,
             scoped_ptr<base::ListValue> event_args)
    : event_name(event_name),
      event_args(event_args.Pass()),
      restrict_to_profile(NULL),
      user_gesture(EventRouter::USER_GESTURE_UNKNOWN) {
  DCHECK(this->event_args.get());
}

Event::Event(const std::string& event_name,
             scoped_ptr<base::ListValue> event_args,
             Profile* restrict_to_profile)
    : event_name(event_name),
      event_args(event_args.Pass()),
      restrict_to_profile(restrict_to_profile),
      user_gesture(EventRouter::USER_GESTURE_UNKNOWN) {
  DCHECK(this->event_args.get());
}

Event::Event(const std::string& event_name,
             scoped_ptr<ListValue> event_args,
             Profile* restrict_to_profile,
             const GURL& event_url,
             EventRouter::UserGestureState user_gesture,
             const EventFilteringInfo& filter_info)
    : event_name(event_name),
      event_args(event_args.Pass()),
      restrict_to_profile(restrict_to_profile),
      event_url(event_url),
      user_gesture(user_gesture),
      filter_info(filter_info) {
  DCHECK(this->event_args.get());
}

Event::~Event() {}

Event* Event::DeepCopy() {
  Event* copy = new Event(event_name,
                          scoped_ptr<base::ListValue>(event_args->DeepCopy()),
                          restrict_to_profile,
                          event_url,
                          user_gesture,
                          filter_info);
  copy->will_dispatch_callback = will_dispatch_callback;
  return copy;
}

EventListenerInfo::EventListenerInfo(const std::string& event_name,
                                     const std::string& extension_id)
    : event_name(event_name),
      extension_id(extension_id) {}

}  // namespace extensions
