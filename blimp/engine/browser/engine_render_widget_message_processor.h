// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BLIMP_ENGINE_BROWSER_ENGINE_RENDER_WIDGET_MESSAGE_PROCESSOR_H_
#define BLIMP_ENGINE_BROWSER_ENGINE_RENDER_WIDGET_MESSAGE_PROCESSOR_H_

#include <vector>

#include "base/containers/small_map.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "blimp/net/blimp_message_processor.h"
#include "blimp/net/input_message_processor.h"

namespace blink {
class WebInputEvent;
}

namespace blimp {

// Handles all incoming and outgoing protobuf message types tied to a specific
// RenderWidget. This includes BlimpMessage::INPUT, BlimpMessage::COMPOSITOR,
// and BlimpMessage::RENDER_WIDGET messages.  Delegates can be added to be
// notified of incoming messages. This class automatically handles dropping
// stale BlimpMessage::RENDER_WIDGET messages from the client after a
// RenderWidgetMessage::INITIALIZE message is sent.
class EngineRenderWidgetMessageProcessor : public BlimpMessageProcessor {
 public:
  // A delegate to be notified of specific RenderWidget related incoming events.
  class RenderWidgetMessageDelegate {
   public:
    // Called when the client is sending a WebInputEvent to the engine.
    virtual void OnWebInputEvent(scoped_ptr<blink::WebInputEvent> event) = 0;

    // Called when the client sent a CompositorMessage.  These messages should
    // be sent to the engine's render process so they can be processed by the
    // RemoteChannel of the compositor.
    virtual void OnCompositorMessageReceived(
        const std::vector<uint8_t>& message) = 0;
  };

  EngineRenderWidgetMessageProcessor(
      BlimpMessageProcessor* render_widget_message_processor,
      BlimpMessageProcessor* compositor_message_processor);
  ~EngineRenderWidgetMessageProcessor() override;

  // Notifies the client that the RenderWidget for a particular WebContents has
  // changed.  When this is sent all incoming messages from the client sent
  // before this message has been received will be dropped by this class.
  void OnRenderWidgetInitialized(const int tab_id);

  // Sends a CompositorMessage for |tab_id| to the client.
  void SendCompositorMessage(const int tab_id,
                             const std::vector<uint8_t>& message);

  // Sets a RenderWidgetMessageDelegate to be notified of all incoming
  // RenderWidget related messages for |tab_id| from the client.  There can only
  // be one RenderWidgetMessageDelegate per tab.
  void SetDelegate(const int tab_id, RenderWidgetMessageDelegate* delegate);
  void RemoveDelegate(const int tab_id);

  // BlimpMessageProcessor implementation.
  void ProcessMessage(scoped_ptr<BlimpMessage> message,
                      const net::CompletionCallback& callback) override;

 private:
  // Returns nullptr if no delegate is found.
  RenderWidgetMessageDelegate* FindDelegate(const int tab_id);

  // Returns 0 if no id is found.
  uint32_t GetRenderWidgetId(const int tab_id);

  typedef base::SmallMap<std::map<int, RenderWidgetMessageDelegate*> >
      DelegateMap;
  typedef base::SmallMap<std::map<int, uint32_t> > RenderWidgetIdMap;

  DelegateMap delegates_;
  RenderWidgetIdMap render_widget_ids_;

  InputMessageProcessor input_message_processor_;

  BlimpMessageProcessor* render_widget_message_processor_;
  BlimpMessageProcessor* compositor_message_processor_;

  DISALLOW_COPY_AND_ASSIGN(EngineRenderWidgetMessageProcessor);
};

}  // namespace blimp

#endif  // BLIMP_ENGINE_BROWSER_ENGINE_RENDER_WIDGET_MESSAGE_PROCESSOR_H_
