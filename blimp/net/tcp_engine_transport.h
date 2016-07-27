// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BLIMP_NET_TCP_ENGINE_TRANSPORT_H_
#define BLIMP_NET_TCP_ENGINE_TRANSPORT_H_

#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "blimp/net/blimp_net_export.h"
#include "blimp/net/blimp_transport.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"

namespace net {
class NetLog;
class ServerSocket;
class StreamSocket;
}  // namespace net

namespace blimp {

class BlimpConnection;

// BlimpTransport which listens for a TCP connection at |address|.
class BLIMP_NET_EXPORT TCPEngineTransport : public BlimpTransport {
 public:
  // Caller retains the ownership of |net_log|.
  TCPEngineTransport(const net::IPEndPoint& address, net::NetLog* net_log);
  ~TCPEngineTransport() override;

  // BlimpTransport implementation.
  void Connect(const net::CompletionCallback& callback) override;
  scoped_ptr<BlimpConnection> TakeConnection() override;
  const std::string GetName() const override;

  int GetLocalAddressForTesting(net::IPEndPoint* address) const;

 private:
  void OnTCPConnectAccepted(int result);

  const net::IPEndPoint address_;
  net::NetLog* net_log_;
  scoped_ptr<net::ServerSocket> server_socket_;
  scoped_ptr<net::StreamSocket> accepted_socket_;
  net::CompletionCallback connect_callback_;

  DISALLOW_COPY_AND_ASSIGN(TCPEngineTransport);
};

}  // namespace blimp

#endif  // BLIMP_NET_TCP_ENGINE_TRANSPORT_H_
