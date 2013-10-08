// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/protocol/client_event_dispatcher.h"

#include "base/message_loop/message_loop_proxy.h"
#include "base/time/time.h"
#include "net/socket/stream_socket.h"
#include "remoting/base/constants.h"
#include "remoting/proto/event.pb.h"
#include "remoting/proto/internal.pb.h"
#include "remoting/protocol/util.h"

namespace remoting {
namespace protocol {

ClientEventDispatcher::ClientEventDispatcher()
    : ChannelDispatcherBase(kEventChannelName) {
}

ClientEventDispatcher::~ClientEventDispatcher() {
  writer_.Close();
}

void ClientEventDispatcher::OnInitialized() {
  // TODO(garykac): Set write failed callback.
  writer_.Init(channel(),
                BufferedSocketWriter::WriteFailedCallback());
}

void ClientEventDispatcher::InjectKeyEvent(const KeyEvent& event) {
  DCHECK(event.has_usb_keycode());
  DCHECK(event.has_pressed());
  EventMessage message;
  message.set_sequence_number(base::Time::Now().ToInternalValue());
  message.mutable_key_event()->CopyFrom(event);
  writer_.Write(SerializeAndFrameMessage(message), base::Closure());
}

void ClientEventDispatcher::InjectMouseEvent(const MouseEvent& event) {
  EventMessage message;
  message.set_sequence_number(base::Time::Now().ToInternalValue());
  message.mutable_mouse_event()->CopyFrom(event);
  writer_.Write(SerializeAndFrameMessage(message), base::Closure());
}

}  // namespace protocol
}  // namespace remoting
