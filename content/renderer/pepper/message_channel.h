// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_PEPPER_MESSAGE_CHANNEL_H_
#define CONTENT_RENDERER_PEPPER_MESSAGE_CHANNEL_H_

#include <deque>

#include "base/memory/weak_ptr.h"
#include "ppapi/shared_impl/resource.h"
#include "third_party/WebKit/public/web/WebSerializedScriptValue.h"
#include "third_party/npapi/bindings/npruntime.h"

struct PP_Var;

namespace content {

class PepperPluginInstanceImpl;

// MessageChannel implements bidirectional postMessage functionality, allowing
// calls from JavaScript to plugins and vice-versa. See
// PPB_Messaging::PostMessage and PPP_Messaging::HandleMessage for more
// information.
//
// Currently, only 1 MessageChannel can exist, to implement postMessage
// functionality for the instance interfaces.  In the future, when we create a
// MessagePort type in PPAPI, those may be implemented here as well with some
// refactoring.
//   - Separate message ports won't require the passthrough object.
//   - The message target won't be limited to instance, and should support
//     either plugin-provided or JS objects.
// TODO(dmichael):  Add support for separate MessagePorts.
class MessageChannel {
 public:
  // MessageChannelNPObject is a simple struct that adds a pointer back to a
  // MessageChannel instance.  This way, we can use an NPObject to allow
  // JavaScript interactions without forcing MessageChannel to inherit from
  // NPObject.
  struct MessageChannelNPObject : public NPObject {
    MessageChannelNPObject();
    ~MessageChannelNPObject();

    base::WeakPtr<MessageChannel> message_channel;
  };

  explicit MessageChannel(PepperPluginInstanceImpl* instance);
  ~MessageChannel();

  // Post a message to the onmessage handler for this channel's instance
  // asynchronously.
  void PostMessageToJavaScript(PP_Var message_data);
  // Post a message to the PPP_Instance HandleMessage function for this
  // channel's instance.
  void PostMessageToNative(PP_Var message_data);

  // Return the NPObject* to which we should forward any calls which aren't
  // related to postMessage.  Note that this can be NULL;  it only gets set if
  // there is a scriptable 'InstanceObject' associated with this channel's
  // instance.
  NPObject* passthrough_object() {
    return passthrough_object_;
  }
  void SetPassthroughObject(NPObject* passthrough);

  NPObject* np_object() { return np_object_; }

  PepperPluginInstanceImpl* instance() {
    return instance_;
  }

  // Messages sent to JavaScript are queued by default. After the DOM is
  // set up for the plugin, users of MessageChannel should call
  // StopQueueingJavaScriptMessages to start dispatching messages to JavaScript.
  void QueueJavaScriptMessages();
  void StopQueueingJavaScriptMessages();

 private:
  PepperPluginInstanceImpl* instance_;

  // We pass all non-postMessage calls through to the passthrough_object_.
  // This way, a plugin can use PPB_Class or PPP_Class_Deprecated and also
  // postMessage.  This is necessary to support backwards-compatibility, and
  // also trusted plugins for which we will continue to support synchronous
  // scripting.
  NPObject* passthrough_object_;

  // The NPObject we use to expose postMessage to JavaScript.
  MessageChannelNPObject* np_object_;

  // Post a message to the onmessage handler for this channel's instance
  // synchronously.  This is used by PostMessageToJavaScript.
  void PostMessageToJavaScriptImpl(
      const WebKit::WebSerializedScriptValue& message_data);
  // Post a message to the PPP_Instance HandleMessage function for this
  // channel's instance.  This is used by PostMessageToNative.
  void PostMessageToNativeImpl(PP_Var message_data);

  void DrainEarlyMessageQueue();

  // This is used to ensure pending tasks will not fire after this object is
  // destroyed.
  base::WeakPtrFactory<MessageChannel> weak_ptr_factory_;

  // TODO(teravest): Remove all the tricky DRAIN_CANCELLED logic once
  // PluginInstance::ResetAsProxied() is gone.
  std::deque<WebKit::WebSerializedScriptValue> early_message_queue_;
  enum EarlyMessageQueueState {
    QUEUE_MESSAGES,       // Queue JS messages.
    SEND_DIRECTLY,        // Post JS messages directly.
    DRAIN_PENDING,        // Drain queue, then transition to DIRECT.
    DRAIN_CANCELLED       // Preempt drain, go back to QUEUE.
  };
  EarlyMessageQueueState early_message_queue_state_;

  DISALLOW_COPY_AND_ASSIGN(MessageChannel);
};

}  // namespace content

#endif  // CONTENT_RENDERER_PEPPER_MESSAGE_CHANNEL_H_
