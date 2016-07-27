// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_PROTOCOL_SESSION_H_
#define REMOTING_PROTOCOL_SESSION_H_

#include <string>

#include "remoting/protocol/errors.h"
#include "remoting/protocol/session_config.h"

namespace remoting {
namespace protocol {

class StreamChannelFactory;
class Transport;
struct TransportRoute;

// Generic interface for Chromotocol connection used by both client and host.
// Provides access to the connection channels, but doesn't depend on the
// protocol used for each channel.
class Session {
 public:
  enum State {
    // Created, but not connecting yet.
    INITIALIZING,

    // Sent session-initiate, but haven't received session-accept.
    CONNECTING,

    // Received session-initiate, but haven't sent session-accept.
    ACCEPTING,

    // Session has been accepted and is pending authentication.
    ACCEPTED,

    // Session has started authenticating.
    AUTHENTICATING,

    // Session has been connected and authenticated.
    AUTHENTICATED,

    // Session has been connected.
    CONNECTED,

    // Session has been closed.
    CLOSED,

    // Connection has failed.
    FAILED,
  };

  class EventHandler {
   public:
    EventHandler() {}
    virtual ~EventHandler() {}

    // Called after session state has changed. It is safe to destroy
    // the session from within the handler if |state| is AUTHENTICATING
    // or CLOSED or FAILED.
    virtual void OnSessionStateChange(State state) = 0;

    // Called whenever route for the channel specified with
    // |channel_name| changes. Session must not be destroyed by the
    // handler of this event.
    virtual void OnSessionRouteChange(const std::string& channel_name,
                                      const TransportRoute& route) = 0;
  };

  Session() {}
  virtual ~Session() {}

  // Set event handler for this session. |event_handler| must outlive
  // this object.
  virtual void SetEventHandler(EventHandler* event_handler) = 0;

  // Returns error code for a failed session.
  virtual ErrorCode error() = 0;

  // JID of the other side.
  virtual const std::string& jid() = 0;

  // Protocol configuration. Can be called only after session has been accepted.
  // Returned pointer is valid until connection is closed.
  virtual const SessionConfig& config() = 0;

  // Returns Transport that can be used to create transport channels.
  virtual Transport* GetTransport() = 0;

  // Channel factory for QUIC-based channels. Returns nullptr when QUIC is
  // disabled for the session.
  virtual StreamChannelFactory* GetQuicChannelFactory() = 0;

  // Closes connection. Callbacks are guaranteed not to be called after this
  // method returns. |error| specifies the error code in case when the session
  // is being closed due to an error.
  virtual void Close(ErrorCode error) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(Session);
};

}  // namespace protocol
}  // namespace remoting

#endif  // REMOTING_PROTOCOL_SESSION_H_
