// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_PROTOCOL_WEBRTC_TRANSPORT_H_
#define REMOTING_PROTOCOL_WEBRTC_TRANSPORT_H_

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/scoped_vector.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/thread.h"
#include "base/threading/thread_checker.h"
#include "base/timer/timer.h"
#include "remoting/protocol/transport.h"
#include "remoting/protocol/webrtc_data_stream_adapter.h"
#include "remoting/signaling/signal_strategy.h"
#include "third_party/libjingle/source/talk/app/webrtc/peerconnectioninterface.h"

namespace webrtc {
class FakeAudioDeviceModule;
}  // namespace webrtc

namespace remoting {
namespace protocol {

class WebrtcTransport : public Transport,
                        public webrtc::PeerConnectionObserver {
 public:
  WebrtcTransport(
      rtc::scoped_refptr<webrtc::PortAllocatorFactoryInterface>
          port_allocator_factory,
      TransportRole role,
      scoped_refptr<base::SingleThreadTaskRunner> worker_task_runner);
  ~WebrtcTransport() override;

  // Transport interface.
  void Start(EventHandler* event_handler,
             Authenticator* authenticator) override;
  bool ProcessTransportInfo(buzz::XmlElement* transport_info) override;
  DatagramChannelFactory* GetDatagramChannelFactory() override;
  StreamChannelFactory* GetStreamChannelFactory() override;
  StreamChannelFactory* GetMultiplexedChannelFactory() override;

 private:
  void DoStart(rtc::Thread* worker_thread);
  void OnLocalSessionDescriptionCreated(
      scoped_ptr<webrtc::SessionDescriptionInterface> description,
      const std::string& error);
  void OnLocalDescriptionSet(bool success, const std::string& error);
  void OnRemoteDescriptionSet(bool success, const std::string& error);

  // webrtc::PeerConnectionObserver interface.
  void OnSignalingChange(
      webrtc::PeerConnectionInterface::SignalingState new_state) override;
  void OnAddStream(webrtc::MediaStreamInterface* stream) override;
  void OnRemoveStream(webrtc::MediaStreamInterface* stream) override;
  void OnDataChannel(webrtc::DataChannelInterface* data_channel) override;
  void OnRenegotiationNeeded() override;
  void OnIceConnectionChange(
      webrtc::PeerConnectionInterface::IceConnectionState new_state) override;
  void OnIceGatheringChange(
      webrtc::PeerConnectionInterface::IceGatheringState new_state) override;
  void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override;

  void EnsurePendingTransportInfoMessage();
  void SendTransportInfo();
  void AddPendingCandidatesIfPossible();

  void Close(ErrorCode error);

  base::ThreadChecker thread_checker_;

  rtc::scoped_refptr<webrtc::PortAllocatorFactoryInterface>
      port_allocator_factory_;
  TransportRole role_;
  EventHandler* event_handler_ = nullptr;
  scoped_refptr<base::SingleThreadTaskRunner> worker_task_runner_;

  scoped_ptr<webrtc::FakeAudioDeviceModule> fake_audio_device_module_;

  rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>
      peer_connection_factory_;
  rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection_;

  scoped_ptr<buzz::XmlElement> pending_transport_info_message_;
  base::OneShotTimer transport_info_timer_;

  ScopedVector<webrtc::IceCandidateInterface> pending_incoming_candidates_;

  std::list<rtc::scoped_refptr<webrtc::MediaStreamInterface>>
      unclaimed_streams_;

  WebrtcDataStreamAdapter data_stream_adapter_;

  base::WeakPtrFactory<WebrtcTransport> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(WebrtcTransport);
};

class WebrtcTransportFactory : public TransportFactory {
 public:
  WebrtcTransportFactory(
      SignalStrategy* signal_strategy,
      rtc::scoped_refptr<webrtc::PortAllocatorFactoryInterface>
          port_allocator_factory,
      TransportRole role);
  ~WebrtcTransportFactory() override;

  // TransportFactory interface.
  scoped_ptr<Transport> CreateTransport() override;

 private:
  SignalStrategy* signal_strategy_;
  rtc::scoped_refptr<webrtc::PortAllocatorFactoryInterface>
      port_allocator_factory_;
  TransportRole role_;

  base::Thread worker_thread_;

  DISALLOW_COPY_AND_ASSIGN(WebrtcTransportFactory);
};

}  // namespace protocol
}  // namespace remoting

#endif  // REMOTING_PROTOCOL_WEBRTC_TRANSPORT_H_
