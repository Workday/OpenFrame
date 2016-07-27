// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/bluetooth/bluetooth_remote_gatt_service_bluez.h"

#include "base/logging.h"
#include "base/strings/stringprintf.h"
#include "device/bluetooth/bluetooth_adapter_bluez.h"
#include "device/bluetooth/bluetooth_device_bluez.h"
#include "device/bluetooth/bluetooth_remote_gatt_characteristic_bluez.h"
#include "device/bluetooth/bluetooth_remote_gatt_descriptor_bluez.h"
#include "device/bluetooth/dbus/bluetooth_gatt_service_client.h"
#include "device/bluetooth/dbus/bluez_dbus_manager.h"

namespace bluez {

namespace {

// TODO(jamuraa) move these to cros_system_api later
const char kErrorFailed[] = "org.bluez.Error.Failed";
const char kErrorInProgress[] = "org.bluez.Error.InProgress";
const char kErrorInvalidValueLength[] = "org.bluez.Error.InvalidValueLength";
const char kErrorNotAuthorized[] = "org.bluez.Error.NotAuthorized";
const char kErrorNotPaired[] = "org.bluez.Error.NotPaired";
const char kErrorNotSupported[] = "org.bluez.Error.NotSupported";
const char kErrorNotPermitted[] = "org.bluez.Error.NotPermitted";

}  // namespace

BluetoothRemoteGattServiceBlueZ::BluetoothRemoteGattServiceBlueZ(
    BluetoothAdapterBlueZ* adapter,
    BluetoothDeviceBlueZ* device,
    const dbus::ObjectPath& object_path)
    : object_path_(object_path),
      adapter_(adapter),
      device_(device),
      discovery_complete_(false),
      weak_ptr_factory_(this) {
  VLOG(1) << "Creating remote GATT service with identifier: "
          << object_path.value() << ", UUID: " << GetUUID().canonical_value();
  DCHECK(adapter_);

  bluez::BluezDBusManager::Get()->GetBluetoothGattServiceClient()->AddObserver(
      this);
  bluez::BluezDBusManager::Get()
      ->GetBluetoothGattCharacteristicClient()
      ->AddObserver(this);

  // Add all known GATT characteristics.
  const std::vector<dbus::ObjectPath>& gatt_chars =
      bluez::BluezDBusManager::Get()
          ->GetBluetoothGattCharacteristicClient()
          ->GetCharacteristics();
  for (std::vector<dbus::ObjectPath>::const_iterator iter = gatt_chars.begin();
       iter != gatt_chars.end(); ++iter)
    GattCharacteristicAdded(*iter);
}

BluetoothRemoteGattServiceBlueZ::~BluetoothRemoteGattServiceBlueZ() {
  bluez::BluezDBusManager::Get()
      ->GetBluetoothGattServiceClient()
      ->RemoveObserver(this);
  bluez::BluezDBusManager::Get()
      ->GetBluetoothGattCharacteristicClient()
      ->RemoveObserver(this);

  // Clean up all the characteristics. Copy the characteristics list here and
  // clear the original so that when we send GattCharacteristicRemoved(),
  // GetCharacteristics() returns no characteristics.
  CharacteristicMap characteristics = characteristics_;
  characteristics_.clear();
  for (CharacteristicMap::iterator iter = characteristics.begin();
       iter != characteristics.end(); ++iter) {
    DCHECK(adapter_);
    adapter_->NotifyGattCharacteristicRemoved(iter->second);

    delete iter->second;
  }
}

std::string BluetoothRemoteGattServiceBlueZ::GetIdentifier() const {
  return object_path_.value();
}

device::BluetoothUUID BluetoothRemoteGattServiceBlueZ::GetUUID() const {
  bluez::BluetoothGattServiceClient::Properties* properties =
      bluez::BluezDBusManager::Get()
          ->GetBluetoothGattServiceClient()
          ->GetProperties(object_path_);
  DCHECK(properties);
  return device::BluetoothUUID(properties->uuid.value());
}

bool BluetoothRemoteGattServiceBlueZ::IsLocal() const {
  return false;
}

bool BluetoothRemoteGattServiceBlueZ::IsPrimary() const {
  bluez::BluetoothGattServiceClient::Properties* properties =
      bluez::BluezDBusManager::Get()
          ->GetBluetoothGattServiceClient()
          ->GetProperties(object_path_);
  DCHECK(properties);
  return properties->primary.value();
}

device::BluetoothDevice* BluetoothRemoteGattServiceBlueZ::GetDevice() const {
  return device_;
}

std::vector<device::BluetoothGattCharacteristic*>
BluetoothRemoteGattServiceBlueZ::GetCharacteristics() const {
  std::vector<device::BluetoothGattCharacteristic*> characteristics;
  for (CharacteristicMap::const_iterator iter = characteristics_.begin();
       iter != characteristics_.end(); ++iter) {
    characteristics.push_back(iter->second);
  }
  return characteristics;
}

std::vector<device::BluetoothGattService*>
BluetoothRemoteGattServiceBlueZ::GetIncludedServices() const {
  // TODO(armansito): Return the actual included services here.
  return std::vector<device::BluetoothGattService*>();
}

device::BluetoothGattCharacteristic*
BluetoothRemoteGattServiceBlueZ::GetCharacteristic(
    const std::string& identifier) const {
  CharacteristicMap::const_iterator iter =
      characteristics_.find(dbus::ObjectPath(identifier));
  if (iter == characteristics_.end())
    return NULL;
  return iter->second;
}

bool BluetoothRemoteGattServiceBlueZ::AddCharacteristic(
    device::BluetoothGattCharacteristic* characteristic) {
  VLOG(1) << "Characteristics cannot be added to a remote GATT service.";
  return false;
}

bool BluetoothRemoteGattServiceBlueZ::AddIncludedService(
    device::BluetoothGattService* service) {
  VLOG(1) << "Included services cannot be added to a remote GATT service.";
  return false;
}

void BluetoothRemoteGattServiceBlueZ::Register(
    const base::Closure& callback,
    const ErrorCallback& error_callback) {
  VLOG(1) << "A remote GATT service cannot be registered.";
  error_callback.Run();
}

void BluetoothRemoteGattServiceBlueZ::Unregister(
    const base::Closure& callback,
    const ErrorCallback& error_callback) {
  VLOG(1) << "A remote GATT service cannot be unregistered.";
  error_callback.Run();
}

// static
device::BluetoothGattService::GattErrorCode
BluetoothRemoteGattServiceBlueZ::DBusErrorToServiceError(
    std::string error_name) {
  device::BluetoothGattService::GattErrorCode code = GATT_ERROR_UNKNOWN;
  if (error_name == kErrorFailed) {
    code = GATT_ERROR_FAILED;
  } else if (error_name == kErrorInProgress) {
    code = GATT_ERROR_IN_PROGRESS;
  } else if (error_name == kErrorInvalidValueLength) {
    code = GATT_ERROR_INVALID_LENGTH;
  } else if (error_name == kErrorNotPermitted) {
    code = GATT_ERROR_NOT_PERMITTED;
  } else if (error_name == kErrorNotAuthorized) {
    code = GATT_ERROR_NOT_AUTHORIZED;
  } else if (error_name == kErrorNotPaired) {
    code = GATT_ERROR_NOT_PAIRED;
  } else if (error_name == kErrorNotSupported) {
    code = GATT_ERROR_NOT_SUPPORTED;
  }
  return code;
}

BluetoothAdapterBlueZ* BluetoothRemoteGattServiceBlueZ::GetAdapter() const {
  return adapter_;
}

void BluetoothRemoteGattServiceBlueZ::NotifyServiceChanged() {
  // Don't send service changed unless we know that all characteristics have
  // already been discovered. This is to prevent spammy events before sending
  // out the first Gatt
  if (!discovery_complete_)
    return;

  DCHECK(adapter_);
  adapter_->NotifyGattServiceChanged(this);
}

void BluetoothRemoteGattServiceBlueZ::NotifyDescriptorAddedOrRemoved(
    BluetoothRemoteGattCharacteristicBlueZ* characteristic,
    BluetoothRemoteGattDescriptorBlueZ* descriptor,
    bool added) {
  DCHECK(characteristic->GetService() == this);
  DCHECK(descriptor->GetCharacteristic() == characteristic);
  DCHECK(adapter_);

  if (added) {
    adapter_->NotifyGattDescriptorAdded(descriptor);
    return;
  }

  adapter_->NotifyGattDescriptorRemoved(descriptor);
}

void BluetoothRemoteGattServiceBlueZ::NotifyDescriptorValueChanged(
    BluetoothRemoteGattCharacteristicBlueZ* characteristic,
    BluetoothRemoteGattDescriptorBlueZ* descriptor,
    const std::vector<uint8>& value) {
  DCHECK(characteristic->GetService() == this);
  DCHECK(descriptor->GetCharacteristic() == characteristic);
  DCHECK(adapter_);
  adapter_->NotifyGattDescriptorValueChanged(descriptor, value);
}

void BluetoothRemoteGattServiceBlueZ::GattServicePropertyChanged(
    const dbus::ObjectPath& object_path,
    const std::string& property_name) {
  if (object_path != object_path_)
    return;

  VLOG(1) << "Service property changed: \"" << property_name << "\", "
          << object_path.value();
  bluez::BluetoothGattServiceClient::Properties* properties =
      bluez::BluezDBusManager::Get()
          ->GetBluetoothGattServiceClient()
          ->GetProperties(object_path);
  DCHECK(properties);

  if (property_name != properties->characteristics.name()) {
    NotifyServiceChanged();
    return;
  }

  if (discovery_complete_)
    return;

  VLOG(1) << "All characteristics were discovered for service: "
          << object_path.value();
  discovery_complete_ = true;
  DCHECK(adapter_);
  adapter_->NotifyGattDiscoveryComplete(this);
}

void BluetoothRemoteGattServiceBlueZ::GattCharacteristicAdded(
    const dbus::ObjectPath& object_path) {
  if (characteristics_.find(object_path) != characteristics_.end()) {
    VLOG(1) << "Remote GATT characteristic already exists: "
            << object_path.value();
    return;
  }

  bluez::BluetoothGattCharacteristicClient::Properties* properties =
      bluez::BluezDBusManager::Get()
          ->GetBluetoothGattCharacteristicClient()
          ->GetProperties(object_path);
  DCHECK(properties);
  if (properties->service.value() != object_path_) {
    VLOG(2) << "Remote GATT characteristic does not belong to this service.";
    return;
  }

  VLOG(1) << "Adding new remote GATT characteristic for GATT service: "
          << GetIdentifier() << ", UUID: " << GetUUID().canonical_value();

  BluetoothRemoteGattCharacteristicBlueZ* characteristic =
      new BluetoothRemoteGattCharacteristicBlueZ(this, object_path);
  characteristics_[object_path] = characteristic;
  DCHECK(characteristic->GetIdentifier() == object_path.value());
  DCHECK(characteristic->GetUUID().IsValid());

  DCHECK(adapter_);
  adapter_->NotifyGattCharacteristicAdded(characteristic);
}

void BluetoothRemoteGattServiceBlueZ::GattCharacteristicRemoved(
    const dbus::ObjectPath& object_path) {
  CharacteristicMap::iterator iter = characteristics_.find(object_path);
  if (iter == characteristics_.end()) {
    VLOG(2) << "Unknown GATT characteristic removed: " << object_path.value();
    return;
  }

  VLOG(1) << "Removing remote GATT characteristic from service: "
          << GetIdentifier() << ", UUID: " << GetUUID().canonical_value();

  BluetoothRemoteGattCharacteristicBlueZ* characteristic = iter->second;
  DCHECK(characteristic->object_path() == object_path);
  characteristics_.erase(iter);

  DCHECK(adapter_);
  adapter_->NotifyGattCharacteristicRemoved(characteristic);

  delete characteristic;
}

void BluetoothRemoteGattServiceBlueZ::GattCharacteristicPropertyChanged(
    const dbus::ObjectPath& object_path,
    const std::string& property_name) {
  CharacteristicMap::iterator iter = characteristics_.find(object_path);
  if (iter == characteristics_.end()) {
    VLOG(3) << "Properties of unknown characteristic changed";
    return;
  }

  // We may receive a property changed event in certain cases, e.g. when the
  // characteristic "Flags" property has been updated with values from the
  // "Characteristic Extended Properties" descriptor. In this case, kick off
  // a service changed observer event to let observers refresh the
  // characteristics.
  bluez::BluetoothGattCharacteristicClient::Properties* properties =
      bluez::BluezDBusManager::Get()
          ->GetBluetoothGattCharacteristicClient()
          ->GetProperties(object_path);

  DCHECK(properties);
  DCHECK(adapter_);

  if (property_name == properties->flags.name())
    NotifyServiceChanged();
  else if (property_name == properties->value.name())
    adapter_->NotifyGattCharacteristicValueChanged(iter->second,
                                                   properties->value.value());
}

}  // namespace bluez
