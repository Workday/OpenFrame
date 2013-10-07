// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/dbus/shill_manager_client.h"

#include "base/bind.h"
#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "base/values.h"
#include "chromeos/dbus/shill_manager_client_stub.h"
#include "chromeos/dbus/shill_property_changed_observer.h"
#include "dbus/bus.h"
#include "dbus/message.h"
#include "dbus/object_path.h"
#include "dbus/object_proxy.h"
#include "dbus/values_util.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace chromeos {

namespace {

// The ShillManagerClient implementation.
class ShillManagerClientImpl : public ShillManagerClient {
 public:
  explicit ShillManagerClientImpl(dbus::Bus* bus)
      : proxy_(bus->GetObjectProxy(
          flimflam::kFlimflamServiceName,
          dbus::ObjectPath(flimflam::kFlimflamServicePath))),
        helper_(bus, proxy_) {
    helper_.MonitorPropertyChanged(flimflam::kFlimflamManagerInterface);
  }

  ////////////////////////////////////
  // ShillManagerClient overrides.
  virtual void AddPropertyChangedObserver(
      ShillPropertyChangedObserver* observer) OVERRIDE {
    helper_.AddPropertyChangedObserver(observer);
  }

  virtual void RemovePropertyChangedObserver(
      ShillPropertyChangedObserver* observer) OVERRIDE {
    helper_.RemovePropertyChangedObserver(observer);
  }

  virtual void GetProperties(const DictionaryValueCallback& callback) OVERRIDE {
    dbus::MethodCall method_call(flimflam::kFlimflamManagerInterface,
                                 flimflam::kGetPropertiesFunction);
    helper_.CallDictionaryValueMethod(&method_call, callback);
  }

  virtual void GetNetworksForGeolocation(
      const DictionaryValueCallback& callback) OVERRIDE {
    dbus::MethodCall method_call(flimflam::kFlimflamManagerInterface,
                                 shill::kGetNetworksForGeolocation);
    helper_.CallDictionaryValueMethod(&method_call, callback);
  }

  virtual void SetProperty(const std::string& name,
                           const base::Value& value,
                           const base::Closure& callback,
                           const ErrorCallback& error_callback) OVERRIDE {
    dbus::MethodCall method_call(flimflam::kFlimflamManagerInterface,
                                 flimflam::kSetPropertyFunction);
    dbus::MessageWriter writer(&method_call);
    writer.AppendString(name);
    ShillClientHelper::AppendValueDataAsVariant(&writer, value);
    helper_.CallVoidMethodWithErrorCallback(&method_call,
                                            callback,
                                            error_callback);
  }

  virtual void RequestScan(const std::string& type,
                           const base::Closure& callback,
                           const ErrorCallback& error_callback) OVERRIDE {
    dbus::MethodCall method_call(flimflam::kFlimflamManagerInterface,
                                 flimflam::kRequestScanFunction);
    dbus::MessageWriter writer(&method_call);
    writer.AppendString(type);
    helper_.CallVoidMethodWithErrorCallback(&method_call,
                                            callback,
                                            error_callback);
  }

  virtual void EnableTechnology(
      const std::string& type,
      const base::Closure& callback,
      const ErrorCallback& error_callback) OVERRIDE {
    dbus::MethodCall method_call(flimflam::kFlimflamManagerInterface,
                                 flimflam::kEnableTechnologyFunction);
    dbus::MessageWriter writer(&method_call);
    writer.AppendString(type);
    helper_.CallVoidMethodWithErrorCallback(&method_call,
                                            callback,
                                            error_callback);
  }

  virtual void DisableTechnology(
      const std::string& type,
      const base::Closure& callback,
      const ErrorCallback& error_callback) OVERRIDE {
    dbus::MethodCall method_call(flimflam::kFlimflamManagerInterface,
                                 flimflam::kDisableTechnologyFunction);
    dbus::MessageWriter writer(&method_call);
    writer.AppendString(type);
    helper_.CallVoidMethodWithErrorCallback(&method_call,
                                            callback,
                                            error_callback);
  }

  virtual void ConfigureService(
      const base::DictionaryValue& properties,
      const ObjectPathCallback& callback,
      const ErrorCallback& error_callback) OVERRIDE {
    dbus::MethodCall method_call(flimflam::kFlimflamManagerInterface,
                                 flimflam::kConfigureServiceFunction);
    dbus::MessageWriter writer(&method_call);
    ShillClientHelper::AppendServicePropertiesDictionary(&writer, properties);
    helper_.CallObjectPathMethodWithErrorCallback(&method_call,
                                                  callback,
                                                  error_callback);
  }

  virtual void ConfigureServiceForProfile(
      const dbus::ObjectPath& profile_path,
      const base::DictionaryValue& properties,
      const ObjectPathCallback& callback,
      const ErrorCallback& error_callback) OVERRIDE {
    dbus::MethodCall method_call(flimflam::kFlimflamManagerInterface,
                                 shill::kConfigureServiceForProfileFunction);
    dbus::MessageWriter writer(&method_call);
    writer.AppendObjectPath(dbus::ObjectPath(profile_path));
    ShillClientHelper::AppendServicePropertiesDictionary(&writer, properties);
    helper_.CallObjectPathMethodWithErrorCallback(&method_call,
                                                  callback,
                                                  error_callback);
  }

  virtual void GetService(
      const base::DictionaryValue& properties,
      const ObjectPathCallback& callback,
      const ErrorCallback& error_callback) OVERRIDE {
    dbus::MethodCall method_call(flimflam::kFlimflamManagerInterface,
                                 flimflam::kGetServiceFunction);
    dbus::MessageWriter writer(&method_call);
    ShillClientHelper::AppendServicePropertiesDictionary(&writer, properties);
    helper_.CallObjectPathMethodWithErrorCallback(&method_call,
                                                  callback,
                                                  error_callback);
  }

  virtual void VerifyDestination(const VerificationProperties& properties,
                                 const BooleanCallback& callback,
                                 const ErrorCallback& error_callback) OVERRIDE {
    dbus::MethodCall method_call(flimflam::kFlimflamManagerInterface,
                                 shill::kVerifyDestinationFunction);
    dbus::MessageWriter writer(&method_call);
    writer.AppendString(properties.certificate);
    writer.AppendString(properties.public_key);
    writer.AppendString(properties.nonce);
    writer.AppendString(properties.signed_data);
    writer.AppendString(properties.device_serial);
    writer.AppendString(properties.device_ssid);
    writer.AppendString(properties.device_bssid);
    helper_.CallBooleanMethodWithErrorCallback(
        &method_call, callback, error_callback);
  }

  virtual void VerifyAndEncryptCredentials(
      const VerificationProperties& properties,
      const std::string& service_path,
      const StringCallback& callback,
      const ErrorCallback& error_callback) OVERRIDE {
    dbus::MethodCall method_call(flimflam::kFlimflamManagerInterface,
                                 shill::kVerifyAndEncryptCredentialsFunction);
    dbus::MessageWriter writer(&method_call);
    writer.AppendString(properties.certificate);
    writer.AppendString(properties.public_key);
    writer.AppendString(properties.nonce);
    writer.AppendString(properties.signed_data);
    writer.AppendString(properties.device_serial);
    writer.AppendString(properties.device_ssid);
    writer.AppendString(properties.device_bssid);
    writer.AppendObjectPath(dbus::ObjectPath(service_path));
    helper_.CallStringMethodWithErrorCallback(
        &method_call, callback, error_callback);
  }

  virtual void VerifyAndEncryptData(
      const VerificationProperties& properties,
      const std::string& data,
      const StringCallback& callback,
      const ErrorCallback& error_callback) OVERRIDE {
    dbus::MethodCall method_call(flimflam::kFlimflamManagerInterface,
                                 shill::kVerifyAndEncryptDataFunction);
    dbus::MessageWriter writer(&method_call);
    writer.AppendString(properties.certificate);
    writer.AppendString(properties.public_key);
    writer.AppendString(properties.nonce);
    writer.AppendString(properties.signed_data);
    writer.AppendString(properties.device_serial);
    writer.AppendString(properties.device_ssid);
    writer.AppendString(properties.device_bssid);
    writer.AppendString(data);
    helper_.CallStringMethodWithErrorCallback(
        &method_call, callback, error_callback);
  }

  virtual void ConnectToBestServices(
      const base::Closure& callback,
      const ErrorCallback& error_callback) OVERRIDE {
    dbus::MethodCall method_call(flimflam::kFlimflamManagerInterface,
                                 shill::kConnectToBestServicesFunction);
    helper_.CallVoidMethodWithErrorCallback(&method_call,
                                            callback,
                                            error_callback);
  }

  virtual TestInterface* GetTestInterface() OVERRIDE {
    return NULL;
  }

 private:
  dbus::ObjectProxy* proxy_;
  ShillClientHelper helper_;

  DISALLOW_COPY_AND_ASSIGN(ShillManagerClientImpl);
};

}  // namespace

ShillManagerClient::ShillManagerClient() {}

ShillManagerClient::~ShillManagerClient() {}

// static
ShillManagerClient* ShillManagerClient::Create(
    DBusClientImplementationType type,
    dbus::Bus* bus) {
  if (type == REAL_DBUS_CLIENT_IMPLEMENTATION)
    return new ShillManagerClientImpl(bus);
  DCHECK_EQ(STUB_DBUS_CLIENT_IMPLEMENTATION, type);
  return new ShillManagerClientStub();
}

// ShillManagerClient::VerificationProperties implementation.
ShillManagerClient::VerificationProperties::VerificationProperties() {
}

ShillManagerClient::VerificationProperties::~VerificationProperties() {
}

}  // namespace chromeos
