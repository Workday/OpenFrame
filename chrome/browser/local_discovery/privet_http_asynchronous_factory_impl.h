// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_LOCAL_DISCOVERY_PRIVET_HTTP_ASYNCHRONOUS_FACTORY_IMPL_H_
#define CHROME_BROWSER_LOCAL_DISCOVERY_PRIVET_HTTP_ASYNCHRONOUS_FACTORY_IMPL_H_

#include "base/macros.h"
#include "chrome/browser/local_discovery/privet_http.h"
#include "chrome/browser/local_discovery/privet_http_asynchronous_factory.h"

namespace local_discovery {

class EndpointResolver;

class PrivetHTTPAsynchronousFactoryImpl : public PrivetHTTPAsynchronousFactory {
 public:
  explicit PrivetHTTPAsynchronousFactoryImpl(
      net::URLRequestContextGetter* request_context);
  ~PrivetHTTPAsynchronousFactoryImpl() override;

  scoped_ptr<PrivetHTTPResolution> CreatePrivetHTTP(
      const std::string& service_name) override;

 private:
  class ResolutionImpl : public PrivetHTTPResolution {
   public:
    ResolutionImpl(const std::string& service_name,
                   net::URLRequestContextGetter* request_context);
    ~ResolutionImpl() override;

    void Start(const ResultCallback& callback) override;

    void Start(const net::HostPortPair& address,
               const ResultCallback& callback) override;

    const std::string& GetName() override;

   private:
    void ResolveComplete(const ResultCallback& callback,
                         const net::IPEndPoint& endpoint);
    std::string name_;
    scoped_refptr<net::URLRequestContextGetter> request_context_;
    scoped_ptr<EndpointResolver> endpoint_resolver_;

    DISALLOW_COPY_AND_ASSIGN(ResolutionImpl);
  };

  scoped_refptr<net::URLRequestContextGetter> request_context_;

  DISALLOW_COPY_AND_ASSIGN(PrivetHTTPAsynchronousFactoryImpl);
};

}  // namespace local_discovery

#endif  // CHROME_BROWSER_LOCAL_DISCOVERY_PRIVET_HTTP_ASYNCHRONOUS_FACTORY_IMPL_H_
