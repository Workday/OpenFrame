// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/protocol/ice_transport.h"

#include "base/bind.h"
#include "remoting/protocol/channel_authenticator.h"
#include "remoting/protocol/channel_multiplexer.h"
#include "remoting/protocol/pseudotcp_channel_factory.h"
#include "remoting/protocol/secure_channel_factory.h"
#include "remoting/protocol/stream_channel_factory.h"

namespace remoting {
namespace protocol {

// Delay after candidate creation before sending transport-info message to
// accumulate multiple candidates. This is an optimization to reduce number of
// transport-info messages.
const int kTransportInfoSendDelayMs = 20;

// Name of the multiplexed channel.
static const char kMuxChannelName[] = "mux";

IceTransport::IceTransport(cricket::PortAllocator* port_allocator,
                           const NetworkSettings& network_settings,
                           TransportRole role)
    : port_allocator_(port_allocator),
      network_settings_(network_settings),
      role_(role),
      weak_factory_(this) {}

IceTransport::~IceTransport() {
  channel_multiplexer_.reset();
  DCHECK(channels_.empty());
}

base::Closure IceTransport::GetCanStartClosure() {
  return base::Bind(&IceTransport::OnCanStart,
                    weak_factory_.GetWeakPtr());
}

void IceTransport::Start(Transport::EventHandler* event_handler,
                         Authenticator* authenticator) {
  DCHECK(event_handler);
  DCHECK(!event_handler_);

  event_handler_ = event_handler;
  pseudotcp_channel_factory_.reset(new PseudoTcpChannelFactory(this));
  secure_channel_factory_.reset(new SecureChannelFactory(
      pseudotcp_channel_factory_.get(), authenticator));
}

bool IceTransport::ProcessTransportInfo(buzz::XmlElement* transport_info_xml) {
  IceTransportInfo transport_info;
  if (!transport_info.ParseXml(transport_info_xml))
    return false;

  for (auto it = transport_info.ice_credentials.begin();
       it != transport_info.ice_credentials.end(); ++it) {
    ChannelsMap::iterator channel = channels_.find(it->channel);
    if (channel != channels_.end()) {
      channel->second->SetRemoteCredentials(it->ufrag, it->password);
    } else {
      // Transport info was received before the channel was created.
      // This could happen due to messages being reordered on the wire.
      pending_remote_ice_credentials_.push_back(*it);
    }
  }

  for (auto it = transport_info.candidates.begin();
       it != transport_info.candidates.end(); ++it) {
    ChannelsMap::iterator channel = channels_.find(it->name);
    if (channel != channels_.end()) {
      channel->second->AddRemoteCandidate(it->candidate);
    } else {
      // Transport info was received before the channel was created.
      // This could happen due to messages being reordered on the wire.
      pending_remote_candidates_.push_back(*it);
    }
  }

  return true;
}

DatagramChannelFactory* IceTransport::GetDatagramChannelFactory() {
  return this;
}

StreamChannelFactory* IceTransport::GetStreamChannelFactory() {
  return secure_channel_factory_.get();
}

StreamChannelFactory* IceTransport::GetMultiplexedChannelFactory() {
  if (!channel_multiplexer_.get()) {
    channel_multiplexer_.reset(
        new ChannelMultiplexer(GetStreamChannelFactory(), kMuxChannelName));
  }
  return channel_multiplexer_.get();
}

void IceTransport::OnCanStart() {
  DCHECK(!can_start_);

  can_start_ = true;
  for (ChannelsMap::iterator it = channels_.begin(); it != channels_.end();
       ++it) {
    it->second->OnCanStart();
  }
}

void IceTransport::CreateChannel(const std::string& name,
                                 const ChannelCreatedCallback& callback) {
  DCHECK(!channels_[name]);

  scoped_ptr<IceTransportChannel> channel(
      new IceTransportChannel(port_allocator_, network_settings_, role_));
  if (can_start_)
    channel->OnCanStart();
  channel->Connect(name, this, callback);
  AddPendingRemoteTransportInfo(channel.get());
  channels_[name] = channel.release();
}

void IceTransport::CancelChannelCreation(const std::string& name) {
  ChannelsMap::iterator it = channels_.find(name);
  if (it != channels_.end()) {
    DCHECK(!it->second->is_connected());
    delete it->second;
    DCHECK(channels_.find(name) == channels_.end());
  }
}

void IceTransport::AddPendingRemoteTransportInfo(IceTransportChannel* channel) {
  std::list<IceTransportInfo::IceCredentials>::iterator credentials =
      pending_remote_ice_credentials_.begin();
  while (credentials != pending_remote_ice_credentials_.end()) {
    if (credentials->channel == channel->name()) {
      channel->SetRemoteCredentials(credentials->ufrag, credentials->password);
      credentials = pending_remote_ice_credentials_.erase(credentials);
    } else {
      ++credentials;
    }
  }

  std::list<IceTransportInfo::NamedCandidate>::iterator candidate =
      pending_remote_candidates_.begin();
  while (candidate != pending_remote_candidates_.end()) {
    if (candidate->name == channel->name()) {
      channel->AddRemoteCandidate(candidate->candidate);
      candidate = pending_remote_candidates_.erase(candidate);
    } else {
      ++candidate;
    }
  }
}

void IceTransport::OnTransportIceCredentials(IceTransportChannel* channel,
                                             const std::string& ufrag,
                                             const std::string& password) {
  EnsurePendingTransportInfoMessage();
  pending_transport_info_message_->ice_credentials.push_back(
      IceTransportInfo::IceCredentials(channel->name(), ufrag, password));
}

void IceTransport::OnTransportCandidate(IceTransportChannel* channel,
                                        const cricket::Candidate& candidate) {
  EnsurePendingTransportInfoMessage();
  pending_transport_info_message_->candidates.push_back(
      IceTransportInfo::NamedCandidate(channel->name(), candidate));
}

void IceTransport::OnTransportRouteChange(IceTransportChannel* channel,
                                          const TransportRoute& route) {
  if (event_handler_)
    event_handler_->OnTransportRouteChange(channel->name(), route);
}

void IceTransport::OnTransportFailed(IceTransportChannel* channel) {
  event_handler_->OnTransportError(CHANNEL_CONNECTION_ERROR);
}

void IceTransport::OnTransportDeleted(IceTransportChannel* channel) {
  ChannelsMap::iterator it = channels_.find(channel->name());
  DCHECK_EQ(it->second, channel);
  channels_.erase(it);
}

void IceTransport::EnsurePendingTransportInfoMessage() {
  // |transport_info_timer_| must be running iff
  // |pending_transport_info_message_| exists.
  DCHECK_EQ(pending_transport_info_message_ != nullptr,
            transport_info_timer_.IsRunning());

  if (!pending_transport_info_message_) {
    pending_transport_info_message_.reset(new IceTransportInfo());
    // Delay sending the new candidates in case we get more candidates
    // that we can send in one message.
    transport_info_timer_.Start(
        FROM_HERE, base::TimeDelta::FromMilliseconds(kTransportInfoSendDelayMs),
        this, &IceTransport::SendTransportInfo);
  }
}

void IceTransport::SendTransportInfo() {
  DCHECK(pending_transport_info_message_);
  event_handler_->OnOutgoingTransportInfo(
      pending_transport_info_message_->ToXml());
  pending_transport_info_message_.reset();
}

}  // namespace protocol
}  // namespace remoting
