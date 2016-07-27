// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/devtools/device/cast_device_provider.h"

#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/thread_task_runner_handle.h"
#include "net/base/host_port_pair.h"
#include "net/base/ip_address_number.h"

using local_discovery::ServiceDiscoverySharedClient;

namespace {

const int kCastInspectPort = 9222;
const char kCastServiceType[] = "_googlecast._tcp.local";
const char kUnknownCastDevice[] = "Unknown Cast Device";

typedef std::map<std::string, std::string> ServiceTxtRecordMap;

// Parses TXT record strings into a map. TXT key-value strings are assumed to
// follow the form "$key(=$value)?", where $key must contain at least one
// character, and $value may be empty.
scoped_ptr<ServiceTxtRecordMap> ParseServiceTxtRecord(
    const std::vector<std::string>& record) {
  scoped_ptr<ServiceTxtRecordMap> record_map(new ServiceTxtRecordMap());
  for (const auto& key_value_str : record) {
    int index = key_value_str.find("=", 0);
    if (index < 0 && key_value_str != "") {
      // Some strings may only define a key (no '=' in the key/value string).
      // The chosen behavior is to assume the value is the empty string.
      record_map->insert(std::make_pair(key_value_str, ""));
    } else {
      std::string key = key_value_str.substr(0, index);
      std::string value = key_value_str.substr(index + 1);
      record_map->insert(std::make_pair(key, value));
    }
  }
  return record_map.Pass();
}

AndroidDeviceManager::DeviceInfo ServiceDescriptionToDeviceInfo(
    const ServiceDescription& service_description) {
  scoped_ptr<ServiceTxtRecordMap> record_map =
      ParseServiceTxtRecord(service_description.metadata);

  AndroidDeviceManager::DeviceInfo device_info;
  device_info.connected = true;
  const auto& search = record_map->find("md");
  if (search != record_map->end() && search->second != "")
    device_info.model = search->second;
  else
    device_info.model = kUnknownCastDevice;

  AndroidDeviceManager::BrowserInfo browser_info;
  browser_info.socket_name = base::IntToString(kCastInspectPort);
  browser_info.display_name =
      base::SplitString(service_description.service_name, ".",
                        base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL)[0];

  browser_info.type = AndroidDeviceManager::BrowserInfo::kTypeChrome;
  device_info.browser_info.push_back(browser_info);
  return device_info;
}

}  // namespace

// The purpose of this class is to route lister delegate signals from
// ServiceDiscoveryDeviceLister (on the UI thread) to CastDeviceProvider (on the
// DevTools ADB thread). Cancellable callbacks are necessary since
// CastDeviceProvider and ServiceDiscoveryDeviceLister are destroyed on
// different threads in undefined order.
class CastDeviceProvider::DeviceListerDelegate
    : public ServiceDiscoveryDeviceLister::Delegate,
      public base::SupportsWeakPtr<DeviceListerDelegate> {
 public:
  DeviceListerDelegate(base::WeakPtr<CastDeviceProvider> provider,
                       scoped_refptr<base::SingleThreadTaskRunner> runner)
      : provider_(provider), runner_(runner) {}

  virtual ~DeviceListerDelegate() {}

  void StartDiscovery() {
    // This must be called on the UI thread; ServiceDiscoverySharedClient and
    // ServiceDiscoveryDeviceLister are thread protected.
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    if (device_lister_)
      return;
    service_discovery_client_ = ServiceDiscoverySharedClient::GetInstance();
    device_lister_.reset(new ServiceDiscoveryDeviceLister(
        this, service_discovery_client_.get(), kCastServiceType));
    device_lister_->Start();
    device_lister_->DiscoverNewDevices(true);
  }

  // ServiceDiscoveryDeviceLister::Delegate implementation:
  void OnDeviceChanged(bool added,
                       const ServiceDescription& service_description) override {
    runner_->PostTask(
        FROM_HERE, base::Bind(&CastDeviceProvider::OnDeviceChanged, provider_,
                              added, service_description));
  }

  void OnDeviceRemoved(const std::string& service_name) override {
    runner_->PostTask(
        FROM_HERE, base::Bind(&CastDeviceProvider::OnDeviceRemoved, provider_,
                              service_name));
  }

  void OnDeviceCacheFlushed() override {
    runner_->PostTask(
        FROM_HERE,
        base::Bind(&CastDeviceProvider::OnDeviceCacheFlushed, provider_));
  }

 private:
  // The device provider to notify of device changes.
  base::WeakPtr<CastDeviceProvider> provider_;
  // Runner for the thread the WeakPtr was created on (this is where device
  // messages will be posted).
  scoped_refptr<base::SingleThreadTaskRunner> runner_;
  scoped_refptr<ServiceDiscoverySharedClient> service_discovery_client_;
  scoped_ptr<ServiceDiscoveryDeviceLister> device_lister_;
};

CastDeviceProvider::CastDeviceProvider() : weak_factory_(this) {}

CastDeviceProvider::~CastDeviceProvider() {}

void CastDeviceProvider::QueryDevices(const SerialsCallback& callback) {
  if (!lister_delegate_) {
    lister_delegate_.reset(new DeviceListerDelegate(
        weak_factory_.GetWeakPtr(), base::ThreadTaskRunnerHandle::Get()));
    content::BrowserThread::PostTask(
        content::BrowserThread::UI, FROM_HERE,
        base::Bind(&DeviceListerDelegate::StartDiscovery,
                   lister_delegate_->AsWeakPtr()));
  }
  std::set<net::HostPortPair> targets;
  for (const auto& device_entry : device_info_map_)
    targets.insert(net::HostPortPair(device_entry.first, kCastInspectPort));
  tcp_provider_ = new TCPDeviceProvider(targets);
  tcp_provider_->QueryDevices(callback);
}

void CastDeviceProvider::QueryDeviceInfo(const std::string& serial,
                                         const DeviceInfoCallback& callback) {
  auto it_device = device_info_map_.find(serial);
  if (it_device == device_info_map_.end())
    return;
  callback.Run(it_device->second);
}

void CastDeviceProvider::OpenSocket(const std::string& serial,
                                    const std::string& socket_name,
                                    const SocketCallback& callback) {
  tcp_provider_->OpenSocket(serial, socket_name, callback);
}

void CastDeviceProvider::OnDeviceChanged(
    bool added,
    const ServiceDescription& service_description) {
  VLOG(1) << "Device " << (added ? "added: " : "changed: ")
          << service_description.service_name;
  if (service_description.service_type() != kCastServiceType)
    return;
  net::IPAddressNumber ip_address = service_description.ip_address;
  if (ip_address.size() != net::kIPv4AddressSize &&
      ip_address.size() != net::kIPv6AddressSize) {
    // An invalid IP address is not queryable.
    return;
  }
  std::string name = service_description.service_name;
  std::string host = net::IPAddressToString(ip_address);
  service_hostname_map_[name] = host;
  device_info_map_[host] = ServiceDescriptionToDeviceInfo(service_description);
}

void CastDeviceProvider::OnDeviceRemoved(const std::string& service_name) {
  VLOG(1) << "Device removed: " << service_name;
  auto it_hostname = service_hostname_map_.find(service_name);
  if (it_hostname == service_hostname_map_.end())
    return;
  std::string hostname = it_hostname->second;
  service_hostname_map_.erase(it_hostname);
  auto it_device = device_info_map_.find(hostname);
  if (it_device == device_info_map_.end())
    return;
  device_info_map_.erase(it_device);
}

void CastDeviceProvider::OnDeviceCacheFlushed() {
  VLOG(1) << "Device cache flushed";
  service_hostname_map_.clear();
  device_info_map_.clear();
}
