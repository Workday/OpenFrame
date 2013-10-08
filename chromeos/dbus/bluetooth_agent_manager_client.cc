// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/dbus/bluetooth_agent_manager_client.h"

#include "base/bind.h"
#include "base/logging.h"
#include "chromeos/dbus/fake_bluetooth_agent_manager_client.h"
#include "dbus/bus.h"
#include "dbus/message.h"
#include "dbus/object_path.h"
#include "dbus/object_proxy.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace chromeos {

const char BluetoothAgentManagerClient::kNoResponseError[] =
    "org.chromium.Error.NoResponse";

// The BluetoothAgentManagerClient implementation used in production.
class BluetoothAgentManagerClientImpl
    : public BluetoothAgentManagerClient {
 public:
  explicit BluetoothAgentManagerClientImpl(dbus::Bus* bus)
      : bus_(bus),
        weak_ptr_factory_(this) {
    DCHECK(bus_);
    object_proxy_ = bus_->GetObjectProxy(
        bluetooth_agent_manager::kBluetoothAgentManagerServiceName,
        dbus::ObjectPath(
            bluetooth_agent_manager::kBluetoothAgentManagerServicePath));
  }

  virtual ~BluetoothAgentManagerClientImpl() {
  }

  // BluetoothAgentManagerClient override.
  virtual void RegisterAgent(const dbus::ObjectPath& agent_path,
                             const std::string& capability,
                             const base::Closure& callback,
                             const ErrorCallback& error_callback) OVERRIDE {
    dbus::MethodCall method_call(
    bluetooth_agent_manager::kBluetoothAgentManagerInterface,
    bluetooth_agent_manager::kRegisterAgent);

    dbus::MessageWriter writer(&method_call);
    writer.AppendObjectPath(agent_path);
    writer.AppendString(capability);

    object_proxy_->CallMethodWithErrorCallback(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&BluetoothAgentManagerClientImpl::OnSuccess,
                   weak_ptr_factory_.GetWeakPtr(), callback),
        base::Bind(&BluetoothAgentManagerClientImpl::OnError,
                   weak_ptr_factory_.GetWeakPtr(), error_callback));
  }

  // BluetoothAgentManagerClient override.
  virtual void UnregisterAgent(const dbus::ObjectPath& agent_path,
                               const base::Closure& callback,
                               const ErrorCallback& error_callback) OVERRIDE {
    dbus::MethodCall method_call(
        bluetooth_agent_manager::kBluetoothAgentManagerInterface,
        bluetooth_agent_manager::kUnregisterAgent);

    dbus::MessageWriter writer(&method_call);
    writer.AppendObjectPath(agent_path);

    object_proxy_->CallMethodWithErrorCallback(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&BluetoothAgentManagerClientImpl::OnSuccess,
                   weak_ptr_factory_.GetWeakPtr(), callback),
        base::Bind(&BluetoothAgentManagerClientImpl::OnError,
                   weak_ptr_factory_.GetWeakPtr(), error_callback));
  }


  // BluetoothAgentManagerClient override.
  virtual void RequestDefaultAgent(const dbus::ObjectPath& agent_path,
                                   const base::Closure& callback,
                                   const ErrorCallback& error_callback)
      OVERRIDE {
    dbus::MethodCall method_call(
        bluetooth_agent_manager::kBluetoothAgentManagerInterface,
        bluetooth_agent_manager::kRequestDefaultAgent);

    dbus::MessageWriter writer(&method_call);
    writer.AppendObjectPath(agent_path);

    object_proxy_->CallMethodWithErrorCallback(
        &method_call,
        dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::Bind(&BluetoothAgentManagerClientImpl::OnSuccess,
                   weak_ptr_factory_.GetWeakPtr(), callback),
        base::Bind(&BluetoothAgentManagerClientImpl::OnError,
                   weak_ptr_factory_.GetWeakPtr(), error_callback));
  }

 private:
  // Called when a response for successful method call is received.
  void OnSuccess(const base::Closure& callback,
                 dbus::Response* response) {
    DCHECK(response);
    callback.Run();
  }

  // Called when a response for a failed method call is received.
  void OnError(const ErrorCallback& error_callback,
               dbus::ErrorResponse* response) {
    // Error response has optional error message argument.
    std::string error_name;
    std::string error_message;
    if (response) {
      dbus::MessageReader reader(response);
      error_name = response->GetErrorName();
      reader.PopString(&error_message);
    } else {
      error_name = kNoResponseError;
      error_message = "";
    }
    error_callback.Run(error_name, error_message);
  }

  dbus::Bus* bus_;
  dbus::ObjectProxy* object_proxy_;

  // Weak pointer factory for generating 'this' pointers that might live longer
  // than we do.
  // Note: This should remain the last member so it'll be destroyed and
  // invalidate its weak pointers before any other members are destroyed.
  base::WeakPtrFactory<BluetoothAgentManagerClientImpl>
      weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(BluetoothAgentManagerClientImpl);
};

BluetoothAgentManagerClient::BluetoothAgentManagerClient() {
}

BluetoothAgentManagerClient::~BluetoothAgentManagerClient() {
}

BluetoothAgentManagerClient* BluetoothAgentManagerClient::Create(
    DBusClientImplementationType type,
    dbus::Bus* bus) {
  if (type == REAL_DBUS_CLIENT_IMPLEMENTATION)
    return new BluetoothAgentManagerClientImpl(bus);
  DCHECK_EQ(STUB_DBUS_CLIENT_IMPLEMENTATION, type);
  return new FakeBluetoothAgentManagerClient();
}

}  // namespace chromeos
