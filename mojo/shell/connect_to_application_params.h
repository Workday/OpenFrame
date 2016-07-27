// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SHELL_CONNECT_TO_APPLICATION_PARAMS_H_
#define MOJO_SHELL_CONNECT_TO_APPLICATION_PARAMS_H_

#include <string>

#include "base/callback.h"
#include "mojo/application/public/interfaces/service_provider.mojom.h"
#include "mojo/application/public/interfaces/shell.mojom.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/services/network/public/interfaces/url_loader.mojom.h"
#include "mojo/shell/identity.h"
#include "url/gurl.h"

namespace mojo {
namespace shell {

class ApplicationInstance;

// This class represents a request for the application manager to connect to an
// application.
class ConnectToApplicationParams {
 public:
  ConnectToApplicationParams();
  ~ConnectToApplicationParams();

  // Sets |source_|. If |source| is null, |source_| is reset.
  void SetSource(ApplicationInstance* source);

  // The following methods set both |target_| and |target_url_request_|.
  void SetTarget(const Identity& target);
  void SetTargetURL(const GURL& target_url);
  void SetTargetURLRequest(URLRequestPtr request);
  void SetTargetURLRequest(URLRequestPtr request, const Identity& target);

  void set_source(const Identity& source) { source_ = source;  }
  const Identity& source() const { return source_; }
  const Identity& target() const { return target_; }

  const URLRequest* target_url_request() const {
    return target_url_request_.get();
  }
  // NOTE: This doesn't reset |target_|.
  URLRequestPtr TakeTargetURLRequest() { return target_url_request_.Pass(); }

  void set_services(InterfaceRequest<ServiceProvider> value) {
    services_ = value.Pass();
  }
  InterfaceRequest<ServiceProvider> TakeServices() { return services_.Pass(); }

  void set_exposed_services(ServiceProviderPtr value) {
    exposed_services_ = value.Pass();
  }
  ServiceProviderPtr TakeExposedServices() { return exposed_services_.Pass(); }

  void set_on_application_end(const base::Closure& value) {
    on_application_end_ = value;
  }
  const base::Closure& on_application_end() const {
    return on_application_end_;
  }

  void set_connect_callback(const Shell::ConnectToApplicationCallback& value) {
    connect_callback_ = value;
  }
  const Shell::ConnectToApplicationCallback& connect_callback() const {
    return connect_callback_;
  }

 private:
  // It may be null (i.e., is_null() returns true) which indicates that there is
  // no source (e.g., for the first application or in tests).
  Identity source_;
  // The identity of the application being connected to.
  Identity target_;
  // The URL request to fetch the application. It may contain more information
  // than |target_| (e.g., headers, request body). When it is taken, |target_|
  // remains unchanged.
  URLRequestPtr target_url_request_;

  InterfaceRequest<ServiceProvider> services_;
  ServiceProviderPtr exposed_services_;
  base::Closure on_application_end_;
  Shell::ConnectToApplicationCallback connect_callback_;

  DISALLOW_COPY_AND_ASSIGN(ConnectToApplicationParams);
};

}  // namespace shell
}  // namespace mojo

#endif  // MOJO_SHELL_CONNECT_TO_APPLICATION_PARAMS_H_
