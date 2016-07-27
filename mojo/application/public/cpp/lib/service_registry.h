// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_APPLICATION_PUBLIC_CPP_LIB_SERVICE_REGISTRY_H_
#define MOJO_APPLICATION_PUBLIC_CPP_LIB_SERVICE_REGISTRY_H_

#include <set>
#include <string>

#include "mojo/application/public/cpp/application_connection.h"
#include "mojo/application/public/cpp/lib/service_connector_registry.h"
#include "mojo/application/public/interfaces/service_provider.mojom.h"
#include "mojo/application/public/interfaces/shell.mojom.h"
#include "mojo/public/cpp/bindings/binding.h"

namespace mojo {
namespace internal {

// A ServiceRegistry represents each half of a connection between two
// applications, allowing customization of which services are published to the
// other.
class ServiceRegistry : public ServiceProvider, public ApplicationConnection {
 public:
  ServiceRegistry();
  // |allowed_interfaces| are the set of interfaces that the shell has allowed
  // an application to expose to another application. If this set contains only
  // the string value "*" all interfaces may be exposed.
  ServiceRegistry(const std::string& connection_url,
                  const std::string& remote_url,
                  ServiceProviderPtr remote_services,
                  InterfaceRequest<ServiceProvider> local_services,
                  const std::set<std::string>& allowed_interfaces);
  ~ServiceRegistry() override;

  Shell::ConnectToApplicationCallback GetConnectToApplicationCallback();

  // ApplicationConnection overrides.
  void SetServiceConnector(ServiceConnector* service_connector) override;
  bool SetServiceConnectorForName(ServiceConnector* service_connector,
                                  const std::string& interface_name) override;
  const std::string& GetConnectionURL() override;
  const std::string& GetRemoteApplicationURL() override;
  ServiceProvider* GetServiceProvider() override;
  ServiceProvider* GetLocalServiceProvider() override;
  void SetRemoteServiceProviderConnectionErrorHandler(
      const Closure& handler) override;
  bool GetContentHandlerID(uint32_t* target_id) override;
  void AddContentHandlerIDCallback(const Closure& callback) override;
  base::WeakPtr<ApplicationConnection> GetWeakPtr() override;

  void RemoveServiceConnectorForName(const std::string& interface_name);

 private:
  void OnGotContentHandlerID(uint32_t content_handler_id);

  // ServiceProvider method.
  void ConnectToService(const mojo::String& service_name,
                        ScopedMessagePipeHandle client_handle) override;

  const std::string connection_url_;
  const std::string remote_url_;
  Binding<ServiceProvider> local_binding_;
  ServiceProviderPtr remote_service_provider_;
  ServiceConnectorRegistry service_connector_registry_;
  const std::set<std::string> allowed_interfaces_;
  const bool allow_all_interfaces_;
  // The id of the content_handler is only available once the callback from
  // establishing the connection is made.
  uint32_t content_handler_id_;
  bool is_content_handler_id_valid_;
  std::vector<Closure> content_handler_id_callbacks_;
  base::WeakPtrFactory<ServiceRegistry> weak_factory_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(ServiceRegistry);
};

}  // namespace internal
}  // namespace mojo

#endif  // MOJO_APPLICATION_PUBLIC_CPP_LIB_SERVICE_REGISTRY_H_
