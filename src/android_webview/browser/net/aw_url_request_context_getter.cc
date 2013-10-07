// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "android_webview/browser/net/aw_url_request_context_getter.h"

#include <vector>

#include "android_webview/browser/aw_browser_context.h"
#include "android_webview/browser/aw_content_browser_client.h"
#include "android_webview/browser/aw_request_interceptor.h"
#include "android_webview/browser/net/aw_network_delegate.h"
#include "android_webview/browser/net/aw_url_request_job_factory.h"
#include "android_webview/browser/net/init_native_callback.h"
#include "android_webview/common/aw_switches.h"
#include "base/command_line.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/cookie_store_factory.h"
#include "content/public/common/content_client.h"
#include "content/public/common/url_constants.h"
#include "net/base/cache_type.h"
#include "net/cookies/cookie_store.h"
#include "net/http/http_cache.h"
#include "net/proxy/proxy_service.h"
#include "net/url_request/data_protocol_handler.h"
#include "net/url_request/file_protocol_handler.h"
#include "net/url_request/protocol_intercept_job_factory.h"
#include "net/url_request/url_request_context_builder.h"
#include "net/url_request/url_request_context.h"

using content::BrowserThread;

namespace android_webview {

AwURLRequestContextGetter::AwURLRequestContextGetter(
    AwBrowserContext* browser_context)
    : browser_context_(browser_context),
      proxy_config_service_(net::ProxyService::CreateSystemProxyConfigService(
          GetNetworkTaskRunner(),
          NULL /* Ignored on Android */)) {
  // CreateSystemProxyConfigService for Android must be called on main thread.
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // All network stack initialization is done on the synchronous Init call when
  // the IO thread is created.
  BrowserThread::SetDelegate(BrowserThread::IO, this);
}

AwURLRequestContextGetter::~AwURLRequestContextGetter() {
  BrowserThread::SetDelegate(BrowserThread::IO, NULL);
}

void AwURLRequestContextGetter::Init() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  cookie_store_ = content::CreatePersistentCookieStore(
      browser_context_->GetPath().Append(FILE_PATH_LITERAL("Cookies")),
      true,
      NULL,
      NULL);
  cookie_store_->GetCookieMonster()->SetPersistSessionCookies(true);

  // The CookieMonster must be passed here so it happens synchronously to
  // the main thread initialization (to avoid race condition in another
  // thread trying to access the CookieManager API).
  DidCreateCookieMonster(cookie_store_->GetCookieMonster());
}

void AwURLRequestContextGetter::InitAsync() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  net::URLRequestContextBuilder builder;
  builder.set_user_agent(content::GetUserAgent(GURL()));
  builder.set_network_delegate(new AwNetworkDelegate());
#if !defined(DISABLE_FTP_SUPPORT)
  builder.set_ftp_enabled(false);  // Android WebView does not support ftp yet.
#endif
  builder.set_proxy_config_service(proxy_config_service_.release());
  builder.set_accept_language(net::HttpUtil::GenerateAcceptLanguageHeader(
      AwContentBrowserClient::GetAcceptLangsImpl()));

  url_request_context_.reset(builder.Build());
  // TODO(mnaganov): Fix URLRequestContextBuilder to use proper threads.
  net::HttpNetworkSession::Params network_session_params;

  net::BackendType cache_type = net::CACHE_BACKEND_SIMPLE;
  if (CommandLine::ForCurrentProcess()->HasSwitch(
        switches::kDisableSimpleCache)) {
    cache_type = net::CACHE_BACKEND_BLOCKFILE;
  }
  PopulateNetworkSessionParams(&network_session_params);
  net::HttpCache* main_cache = new net::HttpCache(
      network_session_params,
      new net::HttpCache::DefaultBackend(
          net::DISK_CACHE,
          cache_type,
          browser_context_->GetPath().Append(FILE_PATH_LITERAL("Cache")),
          10 * 1024 * 1024,  // 10M
          BrowserThread::GetMessageLoopProxyForThread(BrowserThread::CACHE)));
  main_http_factory_.reset(main_cache);
  url_request_context_->set_http_transaction_factory(main_cache);
  url_request_context_->set_cookie_store(cookie_store_.get());
}

void AwURLRequestContextGetter::PopulateNetworkSessionParams(
    net::HttpNetworkSession::Params* params) {
  net::URLRequestContext* context = url_request_context_.get();
  params->host_resolver = context->host_resolver();
  params->cert_verifier = context->cert_verifier();
  params->server_bound_cert_service = context->server_bound_cert_service();
  params->transport_security_state = context->transport_security_state();
  params->proxy_service = context->proxy_service();
  params->ssl_config_service = context->ssl_config_service();
  params->http_auth_handler_factory = context->http_auth_handler_factory();
  params->network_delegate = context->network_delegate();
  params->http_server_properties = context->http_server_properties();
  params->net_log = context->net_log();
}

net::URLRequestContext* AwURLRequestContextGetter::GetURLRequestContext() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  if (!job_factory_) {
    scoped_ptr<AwURLRequestJobFactory> job_factory(new AwURLRequestJobFactory);
    bool set_protocol = job_factory->SetProtocolHandler(
        chrome::kFileScheme, new net::FileProtocolHandler());
    DCHECK(set_protocol);
    set_protocol = job_factory->SetProtocolHandler(
        chrome::kDataScheme, new net::DataProtocolHandler());
    DCHECK(set_protocol);
    set_protocol = job_factory->SetProtocolHandler(
        chrome::kBlobScheme, protocol_handlers_[chrome::kBlobScheme].release());
    DCHECK(set_protocol);
    set_protocol = job_factory->SetProtocolHandler(
        chrome::kFileSystemScheme,
        protocol_handlers_[chrome::kFileSystemScheme].release());
    DCHECK(set_protocol);
    set_protocol = job_factory->SetProtocolHandler(
        chrome::kChromeUIScheme,
        protocol_handlers_[chrome::kChromeUIScheme].release());
    DCHECK(set_protocol);
    set_protocol = job_factory->SetProtocolHandler(
        chrome::kChromeDevToolsScheme,
        protocol_handlers_[chrome::kChromeDevToolsScheme].release());
    DCHECK(set_protocol);
    protocol_handlers_.clear();

    // Create a chain of URLRequestJobFactories. The handlers will be invoked
    // in the order in which they appear in the protocol_handlers vector.
    typedef std::vector<net::URLRequestJobFactory::ProtocolHandler*>
        ProtocolHandlerVector;
    ProtocolHandlerVector protocol_interceptors;

    // Note that even though the content:// scheme handler is created here,
    // it cannot be used by child processes until access to it is granted via
    // ChildProcessSecurityPolicy::GrantScheme(). This is done in
    // AwContentBrowserClient.
    protocol_interceptors.push_back(
        CreateAndroidContentProtocolHandler().release());
    protocol_interceptors.push_back(
        CreateAndroidAssetFileProtocolHandler().release());
    // The AwRequestInterceptor must come after the content and asset file job
    // factories. This for WebViewClassic compatibility where it was not
    // possible to intercept resource loads to resolvable content:// and
    // file:// URIs.
    // This logical dependency is also the reason why the Content
    // ProtocolHandler has to be added as a ProtocolInterceptJobFactory rather
    // than via SetProtocolHandler.
    protocol_interceptors.push_back(new AwRequestInterceptor());

    // The chain of responsibility will execute the handlers in reverse to the
    // order in which the elements of the chain are created.
    job_factory_ = job_factory.PassAs<net::URLRequestJobFactory>();
    for (ProtocolHandlerVector::reverse_iterator
             i = protocol_interceptors.rbegin();
         i != protocol_interceptors.rend();
         ++i) {
      job_factory_.reset(new net::ProtocolInterceptJobFactory(
          job_factory_.Pass(), make_scoped_ptr(*i)));
    }

    url_request_context_->set_job_factory(job_factory_.get());
  }

  return url_request_context_.get();
}

scoped_refptr<base::SingleThreadTaskRunner>
AwURLRequestContextGetter::GetNetworkTaskRunner() const {
  return BrowserThread::GetMessageLoopProxyForThread(BrowserThread::IO);
}

void AwURLRequestContextGetter::SetProtocolHandlers(
    content::ProtocolHandlerMap* protocol_handlers) {
  std::swap(protocol_handlers_, *protocol_handlers);
}

}  // namespace android_webview
