// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_CLIENT_CHROMOTING_JNI_INSTANCE_H_
#define REMOTING_CLIENT_CHROMOTING_JNI_INSTANCE_H_

#include <string>

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop/message_loop.h"
#include "remoting/client/chromoting_client.h"
#include "remoting/client/client_config.h"
#include "remoting/client/client_context.h"
#include "remoting/client/client_user_interface.h"
#include "remoting/client/frame_consumer_proxy.h"
#include "remoting/client/jni/jni_frame_consumer.h"
#include "remoting/jingle_glue/network_settings.h"
#include "remoting/jingle_glue/xmpp_signal_strategy.h"
#include "remoting/protocol/clipboard_stub.h"
#include "remoting/protocol/connection_to_host.h"
#include "remoting/protocol/cursor_shape_stub.h"

namespace remoting {
namespace protocol {
  class ClipboardEvent;
  class CursorShapeInfo;
}  // namespace protocol

// ClientUserInterface that indirectly makes and receives JNI calls.
class ChromotingJniInstance
  : public ClientUserInterface,
    public protocol::ClipboardStub,
    public protocol::CursorShapeStub,
    public base::RefCountedThreadSafe<ChromotingJniInstance> {
 public:
  // Initiates a connection with the specified host. Call from the UI thread.
  // The instance does not take ownership of |jni_runtime|. To connect with an
  // unpaired host, pass in |pairing_id| and |pairing_secret| as empty strings.
  ChromotingJniInstance(ChromotingJniRuntime* jni_runtime,
                        const char* username,
                        const char* auth_token,
                        const char* host_jid,
                        const char* host_id,
                        const char* host_pubkey,
                        const char* pairing_id,
                        const char* pairing_secret);

  // Terminates the current connection (if it hasn't already failed) and cleans
  // up. Must be called before destruction.
  void Cleanup();

  // Provides the user's PIN and resumes the host authentication attempt. Call
  // on the UI thread once the user has finished entering this PIN into the UI,
  // but only after the UI has been asked to provide a PIN (via FetchSecret()).
  void ProvideSecret(const std::string& pin, bool create_pair);

  // Schedules a redraw on the display thread. May be called from any thread.
  void RedrawDesktop();

  // Moves the host's cursor to the specified coordinates, optionally with some
  // mouse button depressed. If |button| is BUTTON_UNDEFINED, no click is made.
  void PerformMouseAction(int x,
                          int y,
                          protocol::MouseEvent_MouseButton button,
                          bool button_down);

  // Sends the provided keyboard scan code to the host.
  void PerformKeyboardAction(int key_code, bool key_down);

  // ClientUserInterface implementation.
  virtual void OnConnectionState(
      protocol::ConnectionToHost::State state,
      protocol::ErrorCode error) OVERRIDE;
  virtual void OnConnectionReady(bool ready) OVERRIDE;
  virtual void SetCapabilities(const std::string& capabilities) OVERRIDE;
  virtual void SetPairingResponse(
      const protocol::PairingResponse& response) OVERRIDE;
  virtual void DeliverHostMessage(
      const protocol::ExtensionMessage& message) OVERRIDE;
  virtual protocol::ClipboardStub* GetClipboardStub() OVERRIDE;
  virtual protocol::CursorShapeStub* GetCursorShapeStub() OVERRIDE;
  virtual scoped_ptr<protocol::ThirdPartyClientAuthenticator::TokenFetcher>
      GetTokenFetcher(const std::string& host_public_key) OVERRIDE;

  // CursorShapeStub implementation.
  virtual void InjectClipboardEvent(
      const protocol::ClipboardEvent& event) OVERRIDE;

  // ClipboardStub implementation.
  virtual void SetCursorShape(const protocol::CursorShapeInfo& shape) OVERRIDE;

 private:
  // This object is ref-counted, so it cleans itself up.
  virtual ~ChromotingJniInstance();

  void ConnectToHostOnDisplayThread();
  void ConnectToHostOnNetworkThread();
  void DisconnectFromHostOnNetworkThread();

  // Notifies the user interface that the user needs to enter a PIN. The
  // current authentication attempt is put on hold until |callback| is invoked.
  // May be called on any thread.
  void FetchSecret(bool pairable,
                   const protocol::SecretFetchedCallback& callback);

  // Used to obtain task runner references and make calls to Java methods.
  ChromotingJniRuntime* jni_runtime_;

  // This group of variables is to be used on the display thread.
  scoped_refptr<FrameConsumerProxy> frame_consumer_;
  scoped_ptr<JniFrameConsumer> view_;
  scoped_ptr<base::WeakPtrFactory<JniFrameConsumer> > view_weak_factory_;

  // This group of variables is to be used on the network thread.
  scoped_ptr<ClientConfig> client_config_;
  scoped_ptr<ClientContext> client_context_;
  scoped_ptr<protocol::ConnectionToHost> connection_;
  scoped_ptr<ChromotingClient> client_;
  scoped_ptr<XmppSignalStrategy::XmppServerConfig> signaling_config_;
  scoped_ptr<XmppSignalStrategy> signaling_;  // Must outlive client_
  scoped_ptr<NetworkSettings> network_settings_;

  // Pass this the user's PIN once we have it. To be assigned and accessed on
  // the UI thread, but must be posted to the network thread to call it.
  protocol::SecretFetchedCallback pin_callback_;

  // These strings describe the current connection, and are not reused. They
  // are initialized in ConnectionToHost(), but thereafter are only to be used
  // on the network thread. (This is safe because ConnectionToHost()'s finishes
  // using them on the UI thread before they are ever touched from network.)
  std::string username_;
  std::string auth_token_;
  std::string host_jid_;
  std::string host_id_;
  std::string host_pubkey_;
  std::string pairing_id_;
  std::string pairing_secret_;

  // Indicates whether to establish a new pairing with this host. This is
  // modified in ProvideSecret(), but thereafter to be used only from the
  // network thread. (This is safe because ProvideSecret() is invoked at most
  // once per run, and always before any reference to this flag.)
  bool create_pairing_;

  friend class base::RefCountedThreadSafe<ChromotingJniInstance>;

  DISALLOW_COPY_AND_ASSIGN(ChromotingJniInstance);
};

}  // namespace remoting

#endif
