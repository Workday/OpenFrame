// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blimp/net/tcp_engine_transport.h"

#include "base/callback.h"
#include "base/callback_helpers.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "blimp/net/stream_socket_connection.h"
#include "net/socket/stream_socket.h"
#include "net/socket/tcp_server_socket.h"

namespace blimp {

TCPEngineTransport::TCPEngineTransport(const net::IPEndPoint& address,
                                       net::NetLog* net_log)
    : address_(address), net_log_(net_log) {}

TCPEngineTransport::~TCPEngineTransport() {}

void TCPEngineTransport::Connect(const net::CompletionCallback& callback) {
  DCHECK(!accepted_socket_);
  DCHECK(!callback.is_null());

  if (!server_socket_) {
    server_socket_.reset(
        new net::TCPServerSocket(net_log_, net::NetLog::Source()));
    int result = server_socket_->Listen(address_, 5);
    if (result != net::OK) {
      server_socket_.reset();
      base::MessageLoop::current()->PostTask(FROM_HERE,
                                             base::Bind(callback, result));
      return;
    }
  }

  net::CompletionCallback accept_callback = base::Bind(
      &TCPEngineTransport::OnTCPConnectAccepted, base::Unretained(this));

  int result = server_socket_->Accept(&accepted_socket_, accept_callback);
  if (result == net::ERR_IO_PENDING) {
    connect_callback_ = callback;
    return;
  }

  if (result != net::OK) {
    // TODO(haibinlu): investigate when we can keep using this server socket.
    server_socket_.reset();
  }

  base::MessageLoop::current()->PostTask(FROM_HERE,
                                         base::Bind(callback, result));
}

scoped_ptr<BlimpConnection> TCPEngineTransport::TakeConnection() {
  DCHECK(connect_callback_.is_null());
  DCHECK(accepted_socket_);
  return make_scoped_ptr(
      new StreamSocketConnection(std::move(accepted_socket_)));
}

const std::string TCPEngineTransport::GetName() const {
  return "TCP";
}

int TCPEngineTransport::GetLocalAddressForTesting(
    net::IPEndPoint* address) const {
  DCHECK(server_socket_);
  return server_socket_->GetLocalAddress(address);
}

void TCPEngineTransport::OnTCPConnectAccepted(int result) {
  DCHECK_NE(net::ERR_IO_PENDING, result);
  DCHECK(accepted_socket_);
  if (result != net::OK) {
    accepted_socket_.reset();
  }
  base::ResetAndReturn(&connect_callback_).Run(result);
}

}  // namespace blimp
