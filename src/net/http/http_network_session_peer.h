// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP_HTTP_NETWORK_SESSION_PEER_H_
#define NET_HTTP_HTTP_NETWORK_SESSION_PEER_H_

#include "base/memory/ref_counted.h"
#include "net/base/net_export.h"

namespace net {

class ClientSocketPoolManager;
class HttpNetworkSession;
class HttpStreamFactory;
class ProxyService;

class NET_EXPORT_PRIVATE HttpNetworkSessionPeer {
 public:
  explicit HttpNetworkSessionPeer(
      const scoped_refptr<HttpNetworkSession>& session);
  ~HttpNetworkSessionPeer();

  void SetClientSocketPoolManager(
      ClientSocketPoolManager* socket_pool_manager);

  void SetProxyService(ProxyService* proxy_service);

  void SetHttpStreamFactory(HttpStreamFactory* http_stream_factory);
  void SetWebSocketStreamFactory(HttpStreamFactory* websocket_stream_factory);

 private:
  const scoped_refptr<HttpNetworkSession> session_;

  DISALLOW_COPY_AND_ASSIGN(HttpNetworkSessionPeer);
};

}  // namespace net

#endif  // NET_HTTP_HTTP_NETWORK_SESSION_PEER_H_
