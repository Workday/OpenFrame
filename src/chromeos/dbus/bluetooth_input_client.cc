// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/dbus/bluetooth_input_client.h"

#include <map>

#include "base/logging.h"
#include "base/stl_util.h"
#include "chromeos/dbus/fake_bluetooth_input_client.h"
#include "dbus/bus.h"
#include "dbus/message.h"
#include "dbus/object_manager.h"
#include "dbus/object_path.h"
#include "dbus/object_proxy.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace chromeos {

BluetoothInputClient::Properties::Properties(
    dbus::ObjectProxy* object_proxy,
    const std::string& interface_name,
    const PropertyChangedCallback& callback)
    : dbus::PropertySet(object_proxy, interface_name, callback) {
  RegisterProperty(bluetooth_input::kReconnectModeProperty, &reconnect_mode);
}

BluetoothInputClient::Properties::~Properties() {
}


// The BluetoothInputClient implementation used in production.
class BluetoothInputClientImpl
    : public BluetoothInputClient,
      public dbus::ObjectManager::Interface {
 public:
  explicit BluetoothInputClientImpl(dbus::Bus* bus)
      : bus_(bus),
        weak_ptr_factory_(this) {
    object_manager_ = bus_->GetObjectManager(
        bluetooth_object_manager::kBluetoothObjectManagerServiceName,
        dbus::ObjectPath(
            bluetooth_object_manager::kBluetoothObjectManagerServicePath));
    object_manager_->RegisterInterface(
        bluetooth_input::kBluetoothInputInterface, this);
  }

  virtual ~BluetoothInputClientImpl() {
    object_manager_->UnregisterInterface(
        bluetooth_input::kBluetoothInputInterface);
  }

  // BluetoothInputClient override.
  virtual void AddObserver(BluetoothInputClient::Observer* observer)
      OVERRIDE {
    DCHECK(observer);
    observers_.AddObserver(observer);
  }

  // BluetoothInputClient override.
  virtual void RemoveObserver(BluetoothInputClient::Observer* observer)
      OVERRIDE {
    DCHECK(observer);
    observers_.RemoveObserver(observer);
  }

  // dbus::ObjectManager::Interface override.
  virtual dbus::PropertySet* CreateProperties(
      dbus::ObjectProxy* object_proxy,
      const dbus::ObjectPath& object_path,
      const std::string& interface_name) OVERRIDE {
    Properties* properties = new Properties(
        object_proxy,
        interface_name,
        base::Bind(&BluetoothInputClientImpl::OnPropertyChanged,
                   weak_ptr_factory_.GetWeakPtr(),
                   object_path));
    return static_cast<dbus::PropertySet*>(properties);
  }

  // BluetoothInputClient override.
  virtual Properties* GetProperties(const dbus::ObjectPath& object_path)
      OVERRIDE {
    return static_cast<Properties*>(
        object_manager_->GetProperties(
            object_path,
            bluetooth_input::kBluetoothInputInterface));
  }

 private:
  // Called by dbus::ObjectManager when an object with the input interface
  // is created. Informs observers.
  virtual void ObjectAdded(const dbus::ObjectPath& object_path,
                           const std::string& interface_name) OVERRIDE {
    FOR_EACH_OBSERVER(BluetoothInputClient::Observer, observers_,
                      InputAdded(object_path));
  }

  // Called by dbus::ObjectManager when an object with the input interface
  // is removed. Informs observers.
  virtual void ObjectRemoved(const dbus::ObjectPath& object_path,
                             const std::string& interface_name) OVERRIDE {
    FOR_EACH_OBSERVER(BluetoothInputClient::Observer, observers_,
                      InputRemoved(object_path));
  }

  // Called by BluetoothPropertySet when a property value is changed,
  // either by result of a signal or response to a GetAll() or Get()
  // call. Informs observers.
  void OnPropertyChanged(const dbus::ObjectPath& object_path,
                         const std::string& property_name) {
    FOR_EACH_OBSERVER(BluetoothInputClient::Observer, observers_,
                      InputPropertyChanged(object_path, property_name));
  }

  dbus::Bus* bus_;
  dbus::ObjectManager* object_manager_;

  // List of observers interested in event notifications from us.
  ObserverList<BluetoothInputClient::Observer> observers_;

  // Weak pointer factory for generating 'this' pointers that might live longer
  // than we do.
  // Note: This should remain the last member so it'll be destroyed and
  // invalidate its weak pointers before any other members are destroyed.
  base::WeakPtrFactory<BluetoothInputClientImpl> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(BluetoothInputClientImpl);
};

BluetoothInputClient::BluetoothInputClient() {
}

BluetoothInputClient::~BluetoothInputClient() {
}

BluetoothInputClient* BluetoothInputClient::Create(
    DBusClientImplementationType type,
    dbus::Bus* bus) {
  if (type == REAL_DBUS_CLIENT_IMPLEMENTATION)
    return new BluetoothInputClientImpl(bus);
  DCHECK_EQ(STUB_DBUS_CLIENT_IMPLEMENTATION, type);
  return new FakeBluetoothInputClient();
}

}  // namespace chromeos
