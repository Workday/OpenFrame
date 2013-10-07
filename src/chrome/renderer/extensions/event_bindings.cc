// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/extensions/event_bindings.h"

#include <vector>

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/lazy_instance.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "chrome/common/extensions/background_info.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_messages.h"
#include "chrome/common/extensions/extension_set.h"
#include "chrome/common/extensions/value_counter.h"
#include "chrome/common/url_constants.h"
#include "chrome/renderer/extensions/chrome_v8_context.h"
#include "chrome/renderer/extensions/chrome_v8_context_set.h"
#include "chrome/renderer/extensions/chrome_v8_extension.h"
#include "chrome/renderer/extensions/dispatcher.h"
#include "chrome/renderer/extensions/event_bindings.h"
#include "chrome/renderer/extensions/extension_helper.h"
#include "chrome/renderer/extensions/user_script_slave.h"
#include "content/public/renderer/render_thread.h"
#include "content/public/renderer/render_view.h"
#include "content/public/renderer/v8_value_converter.h"
#include "extensions/common/event_filter.h"
#include "extensions/common/view_type.h"
#include "grit/renderer_resources.h"
#include "third_party/WebKit/public/platform/WebURL.h"
#include "third_party/WebKit/public/platform/WebURLRequest.h"
#include "third_party/WebKit/public/web/WebDocument.h"
#include "third_party/WebKit/public/web/WebFrame.h"
#include "third_party/WebKit/public/web/WebView.h"
#include "url/gurl.h"
#include "v8/include/v8.h"

using WebKit::WebFrame;
using WebKit::WebURL;
using content::RenderThread;

namespace extensions {

namespace {

// A map of event names to the number of contexts listening to that event.
// We notify the browser about event listeners when we transition between 0
// and 1.
typedef std::map<std::string, int> EventListenerCounts;

// A map of extension IDs to listener counts for that extension.
base::LazyInstance<std::map<std::string, EventListenerCounts> >
    g_listener_counts = LAZY_INSTANCE_INITIALIZER;

// A map of event names to a (filter -> count) map. The map is used to keep
// track of which filters are in effect for which events.
// We notify the browser about filtered event listeners when we transition
// between 0 and 1.
typedef std::map<std::string, linked_ptr<ValueCounter> >
    FilteredEventListenerCounts;

// A map of extension IDs to filtered listener counts for that extension.
base::LazyInstance<std::map<std::string, FilteredEventListenerCounts> >
    g_filtered_listener_counts = LAZY_INSTANCE_INITIALIZER;

base::LazyInstance<EventFilter> g_event_filter = LAZY_INSTANCE_INITIALIZER;

// TODO(koz): Merge this into EventBindings.
class ExtensionImpl : public ChromeV8Extension {
 public:
  explicit ExtensionImpl(Dispatcher* dispatcher, ChromeV8Context* context)
      : ChromeV8Extension(dispatcher, context) {
    RouteFunction("AttachEvent",
        base::Bind(&ExtensionImpl::AttachEvent, base::Unretained(this)));
    RouteFunction("DetachEvent",
        base::Bind(&ExtensionImpl::DetachEvent, base::Unretained(this)));
    RouteFunction("AttachFilteredEvent",
        base::Bind(&ExtensionImpl::AttachFilteredEvent,
                   base::Unretained(this)));
    RouteFunction("DetachFilteredEvent",
        base::Bind(&ExtensionImpl::DetachFilteredEvent,
                   base::Unretained(this)));
    RouteFunction("MatchAgainstEventFilter",
        base::Bind(&ExtensionImpl::MatchAgainstEventFilter,
                   base::Unretained(this)));
  }

  virtual ~ExtensionImpl() {}

  // Attach an event name to an object.
  void AttachEvent(const v8::FunctionCallbackInfo<v8::Value>& args) {
    CHECK_EQ(1, args.Length());
    CHECK(args[0]->IsString());

    std::string event_name = *v8::String::AsciiValue(args[0]->ToString());

    if (!dispatcher_->CheckContextAccessToExtensionAPI(event_name, context()))
      return;

    std::string extension_id = context()->GetExtensionID();
    EventListenerCounts& listener_counts =
        g_listener_counts.Get()[extension_id];
    if (++listener_counts[event_name] == 1) {
      content::RenderThread::Get()->Send(
          new ExtensionHostMsg_AddListener(extension_id, event_name));
    }

    // This is called the first time the page has added a listener. Since
    // the background page is the only lazy page, we know this is the first
    // time this listener has been registered.
    if (IsLazyBackgroundPage(GetRenderView(), context()->extension())) {
      content::RenderThread::Get()->Send(
          new ExtensionHostMsg_AddLazyListener(extension_id, event_name));
    }
  }

  void DetachEvent(const v8::FunctionCallbackInfo<v8::Value>& args) {
    CHECK_EQ(2, args.Length());
    CHECK(args[0]->IsString());
    CHECK(args[1]->IsBoolean());

    std::string event_name = *v8::String::AsciiValue(args[0]);
    bool is_manual = args[1]->BooleanValue();

    std::string extension_id = context()->GetExtensionID();
    EventListenerCounts& listener_counts =
        g_listener_counts.Get()[extension_id];

    if (--listener_counts[event_name] == 0) {
      content::RenderThread::Get()->Send(
          new ExtensionHostMsg_RemoveListener(extension_id, event_name));
    }

    // DetachEvent is called when the last listener for the context is
    // removed. If the context is the background page, and it removes the
    // last listener manually, then we assume that it is no longer interested
    // in being awakened for this event.
    if (is_manual && IsLazyBackgroundPage(GetRenderView(),
                                          context()->extension())) {
      content::RenderThread::Get()->Send(
          new ExtensionHostMsg_RemoveLazyListener(extension_id, event_name));
    }
  }

  // MatcherID AttachFilteredEvent(string event_name, object filter)
  // event_name - Name of the event to attach.
  // filter - Which instances of the named event are we interested in.
  // returns the id assigned to the listener, which will be returned from calls
  // to MatchAgainstEventFilter where this listener matches.
  void AttachFilteredEvent(const v8::FunctionCallbackInfo<v8::Value>& args) {
    CHECK_EQ(2, args.Length());
    CHECK(args[0]->IsString());
    CHECK(args[1]->IsObject());

    std::string event_name = *v8::String::AsciiValue(args[0]);

    // This method throws an exception if it returns false.
    if (!dispatcher_->CheckContextAccessToExtensionAPI(event_name, context()))
      return;

    std::string extension_id = context()->GetExtensionID();
    if (extension_id.empty()) {
      args.GetReturnValue().Set(static_cast<int32_t>(-1));
      return;
    }

    scoped_ptr<base::DictionaryValue> filter;
    scoped_ptr<content::V8ValueConverter> converter(
        content::V8ValueConverter::create());

    base::DictionaryValue* filter_dict = NULL;
    base::Value* filter_value =
        converter->FromV8Value(args[1]->ToObject(), context()->v8_context());
    if (!filter_value) {
      args.GetReturnValue().Set(static_cast<int32_t>(-1));
      return;
    }
    if (!filter_value->GetAsDictionary(&filter_dict)) {
      delete filter_value;
      args.GetReturnValue().Set(static_cast<int32_t>(-1));
      return;
    }

    filter.reset(filter_dict);
    EventFilter& event_filter = g_event_filter.Get();
    int id = event_filter.AddEventMatcher(event_name, ParseEventMatcher(
        filter.get()));

    // Only send IPCs the first time a filter gets added.
    if (AddFilter(event_name, extension_id, filter.get())) {
      bool lazy = IsLazyBackgroundPage(GetRenderView(), context()->extension());
      content::RenderThread::Get()->Send(
          new ExtensionHostMsg_AddFilteredListener(extension_id, event_name,
                                                   *filter, lazy));
    }

    args.GetReturnValue().Set(static_cast<int32_t>(id));
  }

  // Add a filter to |event_name| in |extension_id|, returning true if it
  // was the first filter for that event in that extension.
  static bool AddFilter(const std::string& event_name,
                        const std::string& extension_id,
                        base::DictionaryValue* filter) {
    FilteredEventListenerCounts& counts =
        g_filtered_listener_counts.Get()[extension_id];
    FilteredEventListenerCounts::iterator it = counts.find(event_name);
    if (it == counts.end())
      counts[event_name].reset(new ValueCounter);

    int result = counts[event_name]->Add(*filter);
    return 1 == result;
  }

  // Remove a filter from |event_name| in |extension_id|, returning true if it
  // was the last filter for that event in that extension.
  static bool RemoveFilter(const std::string& event_name,
                           const std::string& extension_id,
                           base::DictionaryValue* filter) {
    FilteredEventListenerCounts& counts =
        g_filtered_listener_counts.Get()[extension_id];
    FilteredEventListenerCounts::iterator it = counts.find(event_name);
    if (it == counts.end())
      return false;
    return 0 == it->second->Remove(*filter);
  }

  // void DetachFilteredEvent(int id, bool manual)
  // id     - Id of the event to detach.
  // manual - false if this is part of the extension unload process where all
  //          listeners are automatically detached.
  void DetachFilteredEvent(const v8::FunctionCallbackInfo<v8::Value>& args) {
    CHECK_EQ(2, args.Length());
    CHECK(args[0]->IsInt32());
    CHECK(args[1]->IsBoolean());
    bool is_manual = args[1]->BooleanValue();

    std::string extension_id = context()->GetExtensionID();
    if (extension_id.empty())
      return;

    int matcher_id = args[0]->Int32Value();
    EventFilter& event_filter = g_event_filter.Get();
    EventMatcher* event_matcher =
        event_filter.GetEventMatcher(matcher_id);

    const std::string& event_name = event_filter.GetEventName(matcher_id);

    // Only send IPCs the last time a filter gets removed.
    if (RemoveFilter(event_name, extension_id, event_matcher->value())) {
      bool lazy = is_manual && IsLazyBackgroundPage(GetRenderView(),
                                                    context()->extension());
      content::RenderThread::Get()->Send(
          new ExtensionHostMsg_RemoveFilteredListener(extension_id, event_name,
                                                      *event_matcher->value(),
                                                      lazy));
    }

    event_filter.RemoveEventMatcher(matcher_id);
  }

  void MatchAgainstEventFilter(
      const v8::FunctionCallbackInfo<v8::Value>& args) {
    typedef std::set<EventFilter::MatcherID> MatcherIDs;
    EventFilter& event_filter = g_event_filter.Get();
    std::string event_name = *v8::String::AsciiValue(args[0]->ToString());
    EventFilteringInfo info = ParseFromObject(args[1]->ToObject());
    // Only match events routed to this context's RenderView or ones that don't
    // have a routingId in their filter.
    MatcherIDs matched_event_filters = event_filter.MatchEvent(
        event_name, info, context()->GetRenderView()->GetRoutingID());
    v8::Handle<v8::Array> array(v8::Array::New(matched_event_filters.size()));
    int i = 0;
    for (MatcherIDs::iterator it = matched_event_filters.begin();
         it != matched_event_filters.end(); ++it) {
      array->Set(v8::Integer::New(i++), v8::Integer::New(*it));
    }
    args.GetReturnValue().Set(array);
  }

  static EventFilteringInfo ParseFromObject(v8::Handle<v8::Object> object) {
    EventFilteringInfo info;
    v8::Handle<v8::String> url(v8::String::New("url"));
    if (object->Has(url)) {
      v8::Handle<v8::Value> url_value(object->Get(url));
      info.SetURL(GURL(*v8::String::AsciiValue(url_value)));
    }
    v8::Handle<v8::String> instance_id(v8::String::New("instanceId"));
    if (object->Has(instance_id)) {
      v8::Handle<v8::Value> instance_id_value(object->Get(instance_id));
      info.SetInstanceID(instance_id_value->IntegerValue());
    }
    return info;
  }

 private:
  static bool IsLazyBackgroundPage(content::RenderView* render_view,
                                   const Extension* extension) {
    if (!render_view)
      return false;
    ExtensionHelper* helper = ExtensionHelper::Get(render_view);
    return (extension && BackgroundInfo::HasLazyBackgroundPage(extension) &&
            helper->view_type() == VIEW_TYPE_EXTENSION_BACKGROUND_PAGE);
  }

  scoped_ptr<EventMatcher> ParseEventMatcher(
      base::DictionaryValue* filter_dict) {
    return scoped_ptr<EventMatcher>(new EventMatcher(
        scoped_ptr<base::DictionaryValue>(filter_dict->DeepCopy()),
            context()->GetRenderView()->GetRoutingID()));
  }
};

}  // namespace

// static
ChromeV8Extension* EventBindings::Create(Dispatcher* dispatcher,
                                         ChromeV8Context* context) {
  return new ExtensionImpl(dispatcher, context);
}

}  // namespace extensions
