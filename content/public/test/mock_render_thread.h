// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_TEST_MOCK_RENDER_THREAD_H_
#define CONTENT_PUBLIC_TEST_MOCK_RENDER_THREAD_H_

#include "base/memory/shared_memory.h"
#include "base/strings/string16.h"
#include "content/public/renderer/render_thread.h"
#include "ipc/ipc_test_sink.h"
#include "third_party/WebKit/public/web/WebPopupType.h"

struct ViewHostMsg_CreateWindow_Params;

namespace IPC {
class MessageReplyDeserializer;
}

namespace content {

// This class is a very simple mock of RenderThread. It simulates an IPC channel
// which supports only three messages:
// ViewHostMsg_CreateWidget : sync message sent by the Widget.
// ViewHostMsg_CreateWindow : sync message sent by the Widget.
// ViewMsg_Close : async, send to the Widget.
class MockRenderThread : public RenderThread {
 public:
  MockRenderThread();
  virtual ~MockRenderThread();

  // Provides access to the messages that have been received by this thread.
  IPC::TestSink& sink() { return sink_; }

  // Helpers for embedders to know when content IPC messages are received, since
  // they don't have access to content IPC files.
  void VerifyRunJavaScriptMessageSend(const string16& expected_alert_message);

  // RenderThread implementation:
  virtual bool Send(IPC::Message* msg) OVERRIDE;
  virtual base::MessageLoop* GetMessageLoop() OVERRIDE;
  virtual IPC::SyncChannel* GetChannel() OVERRIDE;
  virtual std::string GetLocale() OVERRIDE;
  virtual IPC::SyncMessageFilter* GetSyncMessageFilter() OVERRIDE;
  virtual scoped_refptr<base::MessageLoopProxy> GetIOMessageLoopProxy()
      OVERRIDE;
  virtual void AddRoute(int32 routing_id, IPC::Listener* listener) OVERRIDE;
  virtual void RemoveRoute(int32 routing_id) OVERRIDE;
  virtual int GenerateRoutingID() OVERRIDE;
  virtual void AddFilter(IPC::ChannelProxy::MessageFilter* filter) OVERRIDE;
  virtual void RemoveFilter(IPC::ChannelProxy::MessageFilter* filter) OVERRIDE;
  virtual void SetOutgoingMessageFilter(
      IPC::ChannelProxy::OutgoingMessageFilter* filter) OVERRIDE;
  virtual void AddObserver(RenderProcessObserver* observer) OVERRIDE;
  virtual void RemoveObserver(RenderProcessObserver* observer) OVERRIDE;
  virtual void SetResourceDispatcherDelegate(
      ResourceDispatcherDelegate* delegate) OVERRIDE;
  virtual void WidgetHidden() OVERRIDE;
  virtual void WidgetRestored() OVERRIDE;
  virtual void EnsureWebKitInitialized() OVERRIDE;
  virtual void RecordUserMetrics(const std::string& action) OVERRIDE;
  virtual scoped_ptr<base::SharedMemory> HostAllocateSharedMemoryBuffer(
      size_t buffer_size) OVERRIDE;
  virtual void RegisterExtension(v8::Extension* extension) OVERRIDE;
  virtual void ScheduleIdleHandler(int64 initial_delay_ms) OVERRIDE;
  virtual void IdleHandler() OVERRIDE;
  virtual int64 GetIdleNotificationDelayInMs() const OVERRIDE;
  virtual void SetIdleNotificationDelayInMs(
      int64 idle_notification_delay_in_ms) OVERRIDE;
  virtual void ToggleWebKitSharedTimer(bool suspend) OVERRIDE;
  virtual void UpdateHistograms(int sequence_number) OVERRIDE;
  virtual int PostTaskToAllWebWorkers(const base::Closure& closure) OVERRIDE;
  virtual bool ResolveProxy(const GURL& url, std::string* proxy_list) OVERRIDE;
#if defined(OS_WIN)
  virtual void PreCacheFont(const LOGFONT& log_font) OVERRIDE;
  virtual void ReleaseCachedFonts() OVERRIDE;
#endif

  //////////////////////////////////////////////////////////////////////////
  // The following functions are called by the test itself.

  void set_routing_id(int32 id) {
    routing_id_ = id;
  }

  void set_surface_id(int32 id) {
    surface_id_ = id;
  }

  int32 opener_id() const {
    return opener_id_;
  }

  bool has_widget() const {
    return (widget_ != NULL);
  }

  void set_new_window_routing_id(int32 id) {
    new_window_routing_id_ = id;
  }

  // Simulates the Widget receiving a close message. This should result
  // on releasing the internal reference counts and destroying the internal
  // state.
  void SendCloseMessage();

 protected:
  // This function operates as a regular IPC listener. Subclasses
  // overriding this should first delegate to this implementation.
  virtual bool OnMessageReceived(const IPC::Message& msg);

  // The Widget expects to be returned valid route_id.
  void OnCreateWidget(int opener_id,
                      WebKit::WebPopupType popup_type,
                      int* route_id,
                      int* surface_id);

  // The View expects to be returned a valid route_id different from its own.
  // We do not keep track of the newly created widget in MockRenderThread,
  // so it must be cleaned up on its own.
  void OnCreateWindow(
    const ViewHostMsg_CreateWindow_Params& params,
    int* route_id,
    int* main_frame_route_id,
    int* surface_id,
    int64* cloned_session_storage_namespace_id);

#if defined(OS_WIN)
  void OnDuplicateSection(base::SharedMemoryHandle renderer_handle,
                          base::SharedMemoryHandle* browser_handle);
#endif

  IPC::TestSink sink_;

  // Routing id what will be assigned to the Widget.
  int32 routing_id_;

  // Surface id what will be assigned to the Widget.
  int32 surface_id_;

  // Opener id reported by the Widget.
  int32 opener_id_;

  // We only keep track of one Widget, we learn its pointer when it
  // adds a new route.  We do not keep track of Widgets created with
  // OnCreateWindow.
  IPC::Listener* widget_;

  // Routing id that will be assigned to a CreateWindow Widget.
  int32 new_window_routing_id_;
  int32 new_window_main_frame_routing_id_;

  // The last known good deserializer for sync messages.
  scoped_ptr<IPC::MessageReplyDeserializer> reply_deserializer_;

  // A list of message filters added to this thread.
  std::vector<scoped_refptr<IPC::ChannelProxy::MessageFilter> > filters_;
};

}  // namespace content

#endif  // CONTENT_PUBLIC_TEST_MOCK_RENDER_THREAD_H_
