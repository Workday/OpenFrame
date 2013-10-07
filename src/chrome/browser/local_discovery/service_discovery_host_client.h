// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_LOCAL_DISCOVERY_SERVICE_DISCOVERY_HOST_CLIENT_H_
#define CHROME_BROWSER_LOCAL_DISCOVERY_SERVICE_DISCOVERY_HOST_CLIENT_H_

#include <map>
#include <string>

#include "base/threading/non_thread_safe.h"
#include "chrome/common/local_discovery/service_discovery_client.h"
#include "content/public/browser/utility_process_host_client.h"

namespace base {
class TaskRunner;
}

namespace content {
class UtilityProcessHost;
}

namespace local_discovery {

// Implementation of ServiceDiscoveryClient that delegates all functionality to
// utility process.
class ServiceDiscoveryHostClient : public base::NonThreadSafe,
                                   public ServiceDiscoveryClient,
                                   public content::UtilityProcessHostClient {
 public:
  ServiceDiscoveryHostClient();

  // Starts utility process with ServiceDiscoveryClient.
  void Start();

  // Shutdowns utility process.
  void Shutdown();

  // ServiceDiscoveryClient implementation.
  virtual scoped_ptr<ServiceWatcher> CreateServiceWatcher(
      const std::string& service_type,
      const ServiceWatcher::UpdatedCallback& callback) OVERRIDE;
  virtual scoped_ptr<ServiceResolver> CreateServiceResolver(
      const std::string& service_name,
      const ServiceResolver::ResolveCompleteCallback& callback) OVERRIDE;
  virtual scoped_ptr<LocalDomainResolver> CreateLocalDomainResolver(
      const std::string& domain,
      net::AddressFamily address_family,
      const LocalDomainResolver::IPAddressCallback& callback) OVERRIDE;

  // UtilityProcessHostClient implementation.
  virtual bool OnMessageReceived(const IPC::Message& message) OVERRIDE;

 protected:
  virtual ~ServiceDiscoveryHostClient();

 private:
  class ServiceWatcherProxy;
  class ServiceResolverProxy;
  class LocalDomainResolverProxy;

  typedef std::map<uint64, ServiceWatcher::UpdatedCallback> WatcherCallbacks;
  typedef std::map<uint64, ServiceResolver::ResolveCompleteCallback>
      ResolverCallbacks;
  typedef std::map<uint64, LocalDomainResolver::IPAddressCallback>
      DomainResolverCallbacks;

  void StartOnIOThread();
  void ShutdownOnIOThread();

  void Send(IPC::Message* msg);
  void SendOnIOThread(IPC::Message* msg);

  uint64 RegisterWatcherCallback(
      const ServiceWatcher::UpdatedCallback& callback);
  uint64 RegisterResolverCallback(
      const ServiceResolver::ResolveCompleteCallback& callback);
  uint64 RegisterLocalDomainResolverCallback(
      const LocalDomainResolver::IPAddressCallback& callback);

  void UnregisterWatcherCallback(uint64 id);
  void UnregisterResolverCallback(uint64 id);
  void UnregisterLocalDomainResolverCallback(uint64 id);

  // IPC Message handlers.
  void OnWatcherCallback(uint64 id,
                         ServiceWatcher::UpdateType update,
                         const std::string& service_name);
  void OnResolverCallback(uint64 id,
                          ServiceResolver::RequestStatus status,
                          const ServiceDescription& description);
  void OnLocalDomainResolverCallback(uint64 id,
                                     bool success,
                                     const net::IPAddressNumber& address);


  // Runs watcher callback on owning thread.
  void RunWatcherCallback(uint64 id,
                          ServiceWatcher::UpdateType update,
                          const std::string& service_name);
  // Runs resolver callback on owning thread.
  void RunResolverCallback(uint64 id,
                           ServiceResolver::RequestStatus status,
                           const ServiceDescription& description);
  // Runs local domain resolver callback on owning thread.
  void RunLocalDomainResolverCallback(uint64 id,
                                      bool success,
                                      const net::IPAddressNumber& address);


  base::WeakPtr<content::UtilityProcessHost> utility_host_;

  // Incrementing counter to assign ID to watchers and resolvers.
  uint64 current_id_;
  WatcherCallbacks service_watcher_callbacks_;
  ResolverCallbacks service_resolver_callbacks_;
  DomainResolverCallbacks domain_resolver_callbacks_;
  scoped_refptr<base::TaskRunner> callback_runner_;

  DISALLOW_COPY_AND_ASSIGN(ServiceDiscoveryHostClient);
};

}  // namespace local_discovery

#endif  // CHROME_BROWSER_LOCAL_DISCOVERY_SERVICE_DISCOVERY_HOST_CLIENT_H_
