// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP_HTTP_NETWORK_SESSION_H_
#define NET_HTTP_HTTP_NETWORK_SESSION_H_

#include <set>
#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/non_thread_safe.h"
#include "net/base/host_port_pair.h"
#include "net/base/net_export.h"
#include "net/dns/host_resolver.h"
#include "net/http/http_auth_cache.h"
#include "net/http/http_stream_factory.h"
#include "net/quic/quic_stream_factory.h"
#include "net/socket/next_proto.h"
#include "net/spdy/spdy_session_pool.h"
#include "net/ssl/ssl_client_auth_cache.h"

namespace base {
class Value;
}

namespace net {

class CertPolicyEnforcer;
class CertVerifier;
class ChannelIDService;
class ClientSocketFactory;
class ClientSocketPoolManager;
class CTVerifier;
class HostResolver;
class HttpAuthHandlerFactory;
class HttpNetworkSessionPeer;
class HttpProxyClientSocketPool;
class HttpResponseBodyDrainer;
class HttpServerProperties;
class NetLog;
class NetworkDelegate;
class ProxyDelegate;
class ProxyService;
class QuicClock;
class QuicCryptoClientStreamFactory;
class QuicServerInfoFactory;
class SocketPerformanceWatcherFactory;
class SOCKSClientSocketPool;
class SSLClientSocketPool;
class SSLConfigService;
class TransportClientSocketPool;
class TransportSecurityState;

// This class holds session objects used by HttpNetworkTransaction objects.
class NET_EXPORT HttpNetworkSession
    : NON_EXPORTED_BASE(public base::NonThreadSafe) {
 public:
  struct NET_EXPORT Params {
    Params();
    ~Params();

    ClientSocketFactory* client_socket_factory;
    HostResolver* host_resolver;
    CertVerifier* cert_verifier;
    CertPolicyEnforcer* cert_policy_enforcer;
    ChannelIDService* channel_id_service;
    TransportSecurityState* transport_security_state;
    CTVerifier* cert_transparency_verifier;
    ProxyService* proxy_service;
    std::string ssl_session_cache_shard;
    SSLConfigService* ssl_config_service;
    HttpAuthHandlerFactory* http_auth_handler_factory;
    NetworkDelegate* network_delegate;
    base::WeakPtr<HttpServerProperties> http_server_properties;
    NetLog* net_log;
    HostMappingRules* host_mapping_rules;
    SocketPerformanceWatcherFactory* socket_performance_watcher_factory;
    bool ignore_certificate_errors;
    uint16 testing_fixed_http_port;
    uint16 testing_fixed_https_port;
    bool enable_tcp_fast_open_for_ssl;

    // Compress SPDY headers.
    bool enable_spdy_compression;
    // Use SPDY ping frames to test for connection health after idle.
    bool enable_spdy_ping_based_connection_checking;
    NextProto spdy_default_protocol;
    // The protocols supported by NPN (next protocol negotiation) during the
    // SSL handshake as well as by HTTP Alternate-Protocol.
    // TODO(mmenke):  This is currently empty by default, and alternate
    //                protocols are disabled.  We should use some reasonable
    //                defaults.
    NextProtoVector next_protos;
    size_t spdy_session_max_recv_window_size;
    size_t spdy_stream_max_recv_window_size;
    size_t spdy_initial_max_concurrent_streams;
    // Source of time for SPDY connections.
    SpdySessionPool::TimeFunc time_func;
    // This SPDY proxy is allowed to push resources from origins that are
    // different from those of their associated streams.
    std::string trusted_spdy_proxy;
    // URLs to exclude from forced SPDY.
    std::set<HostPortPair> forced_spdy_exclusions;
    // Process Alt-Svc headers.
    bool use_alternative_services;
    // Only honor alternative service entries which have a higher probability
    // than this value.
    double alternative_service_probability_threshold;

    // Enables NPN support.  Note that ALPN is always enabled.
    bool enable_npn;

    // Enables QUIC support.
    bool enable_quic;
    // Enables QUIC for proxies.
    bool enable_quic_for_proxies;
    // Instruct QUIC to use consistent ephemeral ports when talking to
    // the same server.
    bool enable_quic_port_selection;
    // Disables QUIC's 0-RTT behavior.
    bool quic_always_require_handshake_confirmation;
    // Disables QUIC connection pooling.
    bool quic_disable_connection_pooling;
    // If not zero, the task to load QUIC server configs from the disk cache
    // will timeout after this value multiplied by the smoothed RTT for the
    // server.
    float quic_load_server_info_timeout_srtt_multiplier;
    // Causes QUIC to race reading the server config from disk with
    // sending an inchoate CHLO.
    bool quic_enable_connection_racing;
    // Use non-blocking IO for UDP sockets.
    bool quic_enable_non_blocking_io;
    // Disables using the disk cache to store QUIC server configs.
    bool quic_disable_disk_cache;
    // Prefer AES-GCM to ChaCha20 even if no hardware support is present.
    bool quic_prefer_aes;
    // Specifies the maximum number of connections with high packet loss in
    // a row after which QUIC will be disabled.
    int quic_max_number_of_lossy_connections;
    // Specifies packet loss rate in fraction after which a connection is
    // closed and is considered as a lossy connection.
    float quic_packet_loss_threshold;
    // Size in bytes of the QUIC DUP socket receive buffer.
    int quic_socket_receive_buffer_size;
    // Delay starting a TCP connection when QUIC believes it can speak
    // 0-RTT to a server.
    bool quic_delay_tcp_race;
    // Store server configs in HttpServerProperties, instead of the disk cache.
    bool quic_store_server_configs_in_properties;
    // If not empty, QUIC will be used for all connections to this origin.
    HostPortPair origin_to_force_quic_on;
    // Source of time for QUIC connections. Will be owned by QuicStreamFactory.
    QuicClock* quic_clock;
    // Source of entropy for QUIC connections.
    QuicRandom* quic_random;
    // Limit on the size of QUIC packets.
    size_t quic_max_packet_length;
    // User agent description to send in the QUIC handshake.
    std::string quic_user_agent_id;
    bool enable_user_alternate_protocol_ports;
    // Optional factory to use for creating QuicCryptoClientStreams.
    QuicCryptoClientStreamFactory* quic_crypto_client_stream_factory;
    // Versions of QUIC which may be used.
    QuicVersionVector quic_supported_versions;
    int quic_max_recent_disabled_reasons;
    int quic_threshold_public_resets_post_handshake;
    int quic_threshold_timeouts_streams_open;
    // Set of QUIC tags to send in the handshake's connection options.
    QuicTagVector quic_connection_options;
    // If true, all QUIC sessions are closed when any local IP address changes.
    bool quic_close_sessions_on_ip_change;
    // Specifes QUIC idle connection state lifetime.
    int quic_idle_connection_timeout_seconds;
    ProxyDelegate* proxy_delegate;
  };

  enum SocketPoolType {
    NORMAL_SOCKET_POOL,
    WEBSOCKET_SOCKET_POOL,
    NUM_SOCKET_POOL_TYPES
  };

  explicit HttpNetworkSession(const Params& params);
  ~HttpNetworkSession();

  HttpAuthCache* http_auth_cache() { return &http_auth_cache_; }
  SSLClientAuthCache* ssl_client_auth_cache() {
    return &ssl_client_auth_cache_;
  }

  void AddResponseDrainer(HttpResponseBodyDrainer* drainer);

  void RemoveResponseDrainer(HttpResponseBodyDrainer* drainer);

  TransportClientSocketPool* GetTransportSocketPool(SocketPoolType pool_type);
  SSLClientSocketPool* GetSSLSocketPool(SocketPoolType pool_type);
  SOCKSClientSocketPool* GetSocketPoolForSOCKSProxy(
      SocketPoolType pool_type,
      const HostPortPair& socks_proxy);
  HttpProxyClientSocketPool* GetSocketPoolForHTTPProxy(
      SocketPoolType pool_type,
      const HostPortPair& http_proxy);
  SSLClientSocketPool* GetSocketPoolForSSLWithProxy(
      SocketPoolType pool_type,
      const HostPortPair& proxy_server);

  CertVerifier* cert_verifier() { return cert_verifier_; }
  ProxyService* proxy_service() { return proxy_service_; }
  SSLConfigService* ssl_config_service() { return ssl_config_service_.get(); }
  SpdySessionPool* spdy_session_pool() { return &spdy_session_pool_; }
  QuicStreamFactory* quic_stream_factory() { return &quic_stream_factory_; }
  HttpAuthHandlerFactory* http_auth_handler_factory() {
    return http_auth_handler_factory_;
  }
  NetworkDelegate* network_delegate() {
    return network_delegate_;
  }
  base::WeakPtr<HttpServerProperties> http_server_properties() {
    return http_server_properties_;
  }
  HttpStreamFactory* http_stream_factory() {
    return http_stream_factory_.get();
  }
  HttpStreamFactory* http_stream_factory_for_websocket() {
    return http_stream_factory_for_websocket_.get();
  }
  NetLog* net_log() {
    return net_log_;
  }

  // Creates a Value summary of the state of the socket pools.
  scoped_ptr<base::Value> SocketPoolInfoToValue() const;

  // Creates a Value summary of the state of the SPDY sessions.
  scoped_ptr<base::Value> SpdySessionPoolInfoToValue() const;

  // Creates a Value summary of the state of the QUIC sessions and
  // configuration.
  scoped_ptr<base::Value> QuicInfoToValue() const;

  void CloseAllConnections();
  void CloseIdleConnections();

  // Returns the original Params used to construct this session.
  const Params& params() const { return params_; }

  bool IsProtocolEnabled(AlternateProtocol protocol) const;

  // Populates |*alpn_protos| with protocols to be used with ALPN.
  void GetAlpnProtos(NextProtoVector* alpn_protos) const;

  // Populates |*npn_protos| with protocols to be used with NPN.
  void GetNpnProtos(NextProtoVector* npn_protos) const;

  // Convenience function for searching through |params_| for
  // |forced_spdy_exclusions|.
  bool HasSpdyExclusion(HostPortPair host_port_pair) const;

 private:
  friend class HttpNetworkSessionPeer;

  ClientSocketPoolManager* GetSocketPoolManager(SocketPoolType pool_type);

  NetLog* const net_log_;
  NetworkDelegate* const network_delegate_;
  const base::WeakPtr<HttpServerProperties> http_server_properties_;
  CertVerifier* const cert_verifier_;
  HttpAuthHandlerFactory* const http_auth_handler_factory_;

  // Not const since it's modified by HttpNetworkSessionPeer for testing.
  ProxyService* proxy_service_;
  const scoped_refptr<SSLConfigService> ssl_config_service_;

  HttpAuthCache http_auth_cache_;
  SSLClientAuthCache ssl_client_auth_cache_;
  scoped_ptr<ClientSocketPoolManager> normal_socket_pool_manager_;
  scoped_ptr<ClientSocketPoolManager> websocket_socket_pool_manager_;
  QuicStreamFactory quic_stream_factory_;
  SpdySessionPool spdy_session_pool_;
  scoped_ptr<HttpStreamFactory> http_stream_factory_;
  scoped_ptr<HttpStreamFactory> http_stream_factory_for_websocket_;
  std::set<HttpResponseBodyDrainer*> response_drainers_;

  NextProtoVector next_protos_;
  bool enabled_protocols_[NUM_VALID_ALTERNATE_PROTOCOLS];

  Params params_;
};

}  // namespace net

#endif  // NET_HTTP_HTTP_NETWORK_SESSION_H_
