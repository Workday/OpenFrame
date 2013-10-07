// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/quic/quic_server_session.h"

#include "base/logging.h"
#include "net/quic/reliable_quic_stream.h"
#include "net/tools/quic/quic_spdy_server_stream.h"

namespace net {
namespace tools {

QuicServerSession::QuicServerSession(
    const QuicConfig& config,
    QuicConnection* connection,
    QuicSessionOwner* owner)
    : QuicSession(connection, config, true),
      owner_(owner) {
}

QuicServerSession::~QuicServerSession() {
}

void QuicServerSession::InitializeSession(
    const QuicCryptoServerConfig& crypto_config) {
  crypto_stream_.reset(CreateQuicCryptoServerStream(crypto_config));
}

QuicCryptoServerStream* QuicServerSession::CreateQuicCryptoServerStream(
    const QuicCryptoServerConfig& crypto_config) {
  return new QuicCryptoServerStream(crypto_config, this);
}

void QuicServerSession::ConnectionClose(QuicErrorCode error, bool from_peer) {
  QuicSession::ConnectionClose(error, from_peer);
  owner_->OnConnectionClose(connection()->guid(), error);
}

bool QuicServerSession::ShouldCreateIncomingReliableStream(QuicStreamId id) {
  if (id % 2 == 0) {
    DLOG(INFO) << "Invalid incoming even stream_id:" << id;
    connection()->SendConnectionClose(QUIC_INVALID_STREAM_ID);
    return false;
  }
  if (GetNumOpenStreams() >= get_max_open_streams()) {
    DLOG(INFO) << "Failed to create a new incoming stream with id:" << id
               << " Already " << GetNumOpenStreams() << " open.";
    connection()->SendConnectionClose(QUIC_TOO_MANY_OPEN_STREAMS);
    return false;
  }
  return true;
}

ReliableQuicStream* QuicServerSession::CreateIncomingReliableStream(
    QuicStreamId id) {
  if (!ShouldCreateIncomingReliableStream(id)) {
    return NULL;
  }

  return new QuicSpdyServerStream(id, this);
}

ReliableQuicStream* QuicServerSession::CreateOutgoingReliableStream() {
  DLOG(ERROR) << "Server push not yet supported";
  return NULL;
}

QuicCryptoServerStream* QuicServerSession::GetCryptoStream() {
  return crypto_stream_.get();
}

}  // namespace tools
}  // namespace net
