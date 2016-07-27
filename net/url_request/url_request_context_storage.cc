// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/url_request/url_request_context_storage.h"

#include "base/logging.h"
#include "net/base/network_delegate.h"
#include "net/base/sdch_manager.h"
#include "net/cert/cert_verifier.h"
#include "net/cookies/cookie_store.h"
#include "net/dns/host_resolver.h"
#include "net/ftp/ftp_transaction_factory.h"
#include "net/http/http_auth_handler_factory.h"
#include "net/http/http_server_properties.h"
#include "net/http/http_transaction_factory.h"
#include "net/log/net_log.h"
#include "net/proxy/proxy_service.h"
#include "net/ssl/channel_id_service.h"
#include "net/url_request/http_user_agent_settings.h"
#include "net/url_request/url_request_backoff_manager.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_job_factory.h"
#include "net/url_request/url_request_throttler_manager.h"

namespace net {

URLRequestContextStorage::URLRequestContextStorage(URLRequestContext* context)
    : context_(context) {
  DCHECK(context);
}

URLRequestContextStorage::~URLRequestContextStorage() {}

void URLRequestContextStorage::set_net_log(scoped_ptr<NetLog> net_log) {
  context_->set_net_log(net_log.get());
  net_log_ = net_log.Pass();
}

void URLRequestContextStorage::set_host_resolver(
    scoped_ptr<HostResolver> host_resolver) {
  context_->set_host_resolver(host_resolver.get());
  host_resolver_ = host_resolver.Pass();
}

void URLRequestContextStorage::set_cert_verifier(
    scoped_ptr<CertVerifier> cert_verifier) {
  context_->set_cert_verifier(cert_verifier.get());
  cert_verifier_ = cert_verifier.Pass();
}

void URLRequestContextStorage::set_channel_id_service(
    scoped_ptr<ChannelIDService> channel_id_service) {
  context_->set_channel_id_service(channel_id_service.get());
  channel_id_service_ = channel_id_service.Pass();
}

void URLRequestContextStorage::set_http_auth_handler_factory(
    scoped_ptr<HttpAuthHandlerFactory> http_auth_handler_factory) {
  context_->set_http_auth_handler_factory(http_auth_handler_factory.get());
  http_auth_handler_factory_ = http_auth_handler_factory.Pass();
}

void URLRequestContextStorage::set_proxy_service(
    scoped_ptr<ProxyService> proxy_service) {
  context_->set_proxy_service(proxy_service.get());
  proxy_service_ = proxy_service.Pass();
}

void URLRequestContextStorage::set_ssl_config_service(
    SSLConfigService* ssl_config_service) {
  context_->set_ssl_config_service(ssl_config_service);
  ssl_config_service_ = ssl_config_service;
}

void URLRequestContextStorage::set_network_delegate(
    scoped_ptr<NetworkDelegate> network_delegate) {
  context_->set_network_delegate(network_delegate.get());
  network_delegate_ = network_delegate.Pass();
}

void URLRequestContextStorage::set_http_server_properties(
    scoped_ptr<HttpServerProperties> http_server_properties) {
  http_server_properties_ = http_server_properties.Pass();
  context_->set_http_server_properties(http_server_properties_->GetWeakPtr());
}

void URLRequestContextStorage::set_cookie_store(CookieStore* cookie_store) {
  context_->set_cookie_store(cookie_store);
  cookie_store_ = cookie_store;
}

void URLRequestContextStorage::set_transport_security_state(
    scoped_ptr<TransportSecurityState> transport_security_state) {
  context_->set_transport_security_state(transport_security_state.get());
  transport_security_state_ = transport_security_state.Pass();
}

void URLRequestContextStorage::set_http_network_session(
    scoped_ptr<HttpNetworkSession> http_network_session) {
  http_network_session_ = http_network_session.Pass();
}

void URLRequestContextStorage::set_http_transaction_factory(
    scoped_ptr<HttpTransactionFactory> http_transaction_factory) {
  context_->set_http_transaction_factory(http_transaction_factory.get());
  http_transaction_factory_ = http_transaction_factory.Pass();
}

void URLRequestContextStorage::set_job_factory(
    scoped_ptr<URLRequestJobFactory> job_factory) {
  context_->set_job_factory(job_factory.get());
  job_factory_ = job_factory.Pass();
}

void URLRequestContextStorage::set_throttler_manager(
    scoped_ptr<URLRequestThrottlerManager> throttler_manager) {
  context_->set_throttler_manager(throttler_manager.get());
  throttler_manager_ = throttler_manager.Pass();
}

void URLRequestContextStorage::set_backoff_manager(
    scoped_ptr<URLRequestBackoffManager> backoff_manager) {
  context_->set_backoff_manager(backoff_manager.get());
  backoff_manager_ = backoff_manager.Pass();
}

void URLRequestContextStorage::set_http_user_agent_settings(
    scoped_ptr<HttpUserAgentSettings> http_user_agent_settings) {
  context_->set_http_user_agent_settings(http_user_agent_settings.get());
  http_user_agent_settings_ = http_user_agent_settings.Pass();
}

void URLRequestContextStorage::set_sdch_manager(
    scoped_ptr<SdchManager> sdch_manager) {
  context_->set_sdch_manager(sdch_manager.get());
  sdch_manager_ = sdch_manager.Pass();
}

}  // namespace net
