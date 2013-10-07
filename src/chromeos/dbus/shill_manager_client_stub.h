// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_DBUS_SHILL_MANAGER_CLIENT_STUB_H_
#define CHROMEOS_DBUS_SHILL_MANAGER_CLIENT_STUB_H_

#include <string>

#include "base/basictypes.h"
#include "base/callback.h"
#include "chromeos/dbus/shill_manager_client.h"

namespace chromeos {

// A stub implementation of ShillManagerClient. This works in close coordination
// with ShillServiceClientStub. ShillDeviceClientStub, and
// ShillProfileClientStub, and is not intended to be used independently.
class ShillManagerClientStub : public ShillManagerClient,
                               public ShillManagerClient::TestInterface {
 public:
  ShillManagerClientStub();
  virtual ~ShillManagerClientStub();

  // ShillManagerClient overrides.
  virtual void AddPropertyChangedObserver(
      ShillPropertyChangedObserver* observer) OVERRIDE;
  virtual void RemovePropertyChangedObserver(
      ShillPropertyChangedObserver* observer) OVERRIDE;
  virtual void GetProperties(const DictionaryValueCallback& callback) OVERRIDE;
  virtual void GetNetworksForGeolocation(
      const DictionaryValueCallback& callback) OVERRIDE;
  virtual void SetProperty(const std::string& name,
                           const base::Value& value,
                           const base::Closure& callback,
                           const ErrorCallback& error_callback) OVERRIDE;
  virtual void RequestScan(const std::string& type,
                           const base::Closure& callback,
                           const ErrorCallback& error_callback) OVERRIDE;
  virtual void EnableTechnology(
      const std::string& type,
      const base::Closure& callback,
      const ErrorCallback& error_callback) OVERRIDE;
  virtual void DisableTechnology(
      const std::string& type,
      const base::Closure& callback,
      const ErrorCallback& error_callback) OVERRIDE;
  virtual void ConfigureService(
      const base::DictionaryValue& properties,
      const ObjectPathCallback& callback,
      const ErrorCallback& error_callback) OVERRIDE;
  virtual void ConfigureServiceForProfile(
      const dbus::ObjectPath& profile_path,
      const base::DictionaryValue& properties,
      const ObjectPathCallback& callback,
      const ErrorCallback& error_callback) OVERRIDE;
  virtual void GetService(
      const base::DictionaryValue& properties,
      const ObjectPathCallback& callback,
      const ErrorCallback& error_callback) OVERRIDE;
  virtual void VerifyDestination(const VerificationProperties& properties,
                                 const BooleanCallback& callback,
                                 const ErrorCallback& error_callback) OVERRIDE;
  virtual void VerifyAndEncryptCredentials(
      const VerificationProperties& properties,
      const std::string& service_path,
      const StringCallback& callback,
      const ErrorCallback& error_callback) OVERRIDE;
  virtual void VerifyAndEncryptData(
      const VerificationProperties& properties,
      const std::string& data,
      const StringCallback& callback,
      const ErrorCallback& error_callback) OVERRIDE;
  virtual void ConnectToBestServices(
      const base::Closure& callback,
      const ErrorCallback& error_callback) OVERRIDE;
  virtual ShillManagerClient::TestInterface* GetTestInterface() OVERRIDE;

  // ShillManagerClient::TestInterface overrides.
  virtual void AddDevice(const std::string& device_path) OVERRIDE;
  virtual void RemoveDevice(const std::string& device_path) OVERRIDE;
  virtual void ClearDevices() OVERRIDE;
  virtual void AddTechnology(const std::string& type, bool enabled) OVERRIDE;
  virtual void RemoveTechnology(const std::string& type) OVERRIDE;
  virtual void SetTechnologyInitializing(const std::string& type,
                                         bool initializing) OVERRIDE;
  virtual void AddGeoNetwork(const std::string& technology,
                             const base::DictionaryValue& network) OVERRIDE;
  virtual void AddProfile(const std::string& profile_path) OVERRIDE;
  virtual void ClearProperties() OVERRIDE;
  virtual void AddManagerService(const std::string& service_path,
                                 bool add_to_visible_list,
                                 bool add_to_watch_list) OVERRIDE;
  virtual void RemoveManagerService(const std::string& service_path) OVERRIDE;
  virtual void ClearManagerServices() OVERRIDE;
  virtual void SortManagerServices() OVERRIDE;

 private:
  void AddServiceToWatchList(const std::string& service_path);
  void SetDefaultProperties();
  void PassStubProperties(const DictionaryValueCallback& callback) const;
  void PassStubGeoNetworks(const DictionaryValueCallback& callback) const;
  void CallNotifyObserversPropertyChanged(const std::string& property,
                                          int delay_ms);
  void NotifyObserversPropertyChanged(const std::string& property);
  base::ListValue* GetListProperty(const std::string& property);
  bool TechnologyEnabled(const std::string& type) const;
  void SetTechnologyEnabled(const std::string& type,
                            const base::Closure& callback,
                            bool enabled);
  base::ListValue* GetEnabledServiceList(const std::string& property) const;
  void ScanCompleted(const std::string& device_path,
                     const base::Closure& callback);

  // Dictionary of property name -> property value
  base::DictionaryValue stub_properties_;
  // Dictionary of technology -> list of property dictionaries
  base::DictionaryValue stub_geo_networks_;

  ObserverList<ShillPropertyChangedObserver> observer_list_;

  // Note: This should remain the last member so it'll be destroyed and
  // invalidate its weak pointers before any other members are destroyed.
  base::WeakPtrFactory<ShillManagerClientStub> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(ShillManagerClientStub);
};

}  // namespace chromeos

#endif  // CHROMEOS_DBUS_SHILL_MANAGER_CLIENT_STUB_H_
