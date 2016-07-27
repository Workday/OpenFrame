// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/bluetooth/bluetooth_remote_gatt_descriptor_bluez.h"

#include "base/bind.h"
#include "base/logging.h"
#include "base/strings/stringprintf.h"
#include "device/bluetooth/bluetooth_remote_gatt_characteristic_bluez.h"
#include "device/bluetooth/bluetooth_remote_gatt_service_bluez.h"
#include "device/bluetooth/dbus/bluetooth_gatt_descriptor_client.h"
#include "device/bluetooth/dbus/bluez_dbus_manager.h"

namespace bluez {

namespace {

// Stream operator for logging vector<uint8>.
std::ostream& operator<<(std::ostream& out, const std::vector<uint8> bytes) {
  out << "[";
  for (std::vector<uint8>::const_iterator iter = bytes.begin();
       iter != bytes.end(); ++iter) {
    out << base::StringPrintf("%02X", *iter);
  }
  return out << "]";
}

}  // namespace

BluetoothRemoteGattDescriptorBlueZ::BluetoothRemoteGattDescriptorBlueZ(
    BluetoothRemoteGattCharacteristicBlueZ* characteristic,
    const dbus::ObjectPath& object_path)
    : object_path_(object_path),
      characteristic_(characteristic),
      weak_ptr_factory_(this) {
  VLOG(1) << "Creating remote GATT descriptor with identifier: "
          << GetIdentifier() << ", UUID: " << GetUUID().canonical_value();
}

BluetoothRemoteGattDescriptorBlueZ::~BluetoothRemoteGattDescriptorBlueZ() {}

std::string BluetoothRemoteGattDescriptorBlueZ::GetIdentifier() const {
  return object_path_.value();
}

device::BluetoothUUID BluetoothRemoteGattDescriptorBlueZ::GetUUID() const {
  bluez::BluetoothGattDescriptorClient::Properties* properties =
      bluez::BluezDBusManager::Get()
          ->GetBluetoothGattDescriptorClient()
          ->GetProperties(object_path_);
  DCHECK(properties);
  return device::BluetoothUUID(properties->uuid.value());
}

bool BluetoothRemoteGattDescriptorBlueZ::IsLocal() const {
  return false;
}

const std::vector<uint8>& BluetoothRemoteGattDescriptorBlueZ::GetValue() const {
  bluez::BluetoothGattDescriptorClient::Properties* properties =
      bluez::BluezDBusManager::Get()
          ->GetBluetoothGattDescriptorClient()
          ->GetProperties(object_path_);

  DCHECK(properties);

  return properties->value.value();
}

device::BluetoothGattCharacteristic*
BluetoothRemoteGattDescriptorBlueZ::GetCharacteristic() const {
  return characteristic_;
}

device::BluetoothGattCharacteristic::Permissions
BluetoothRemoteGattDescriptorBlueZ::GetPermissions() const {
  // TODO(armansito): Once BlueZ defines the permissions, return the correct
  // values here.
  return device::BluetoothGattCharacteristic::PERMISSION_NONE;
}

void BluetoothRemoteGattDescriptorBlueZ::ReadRemoteDescriptor(
    const ValueCallback& callback,
    const ErrorCallback& error_callback) {
  VLOG(1) << "Sending GATT characteristic descriptor read request to "
          << "descriptor: " << GetIdentifier()
          << ", UUID: " << GetUUID().canonical_value();

  bluez::BluezDBusManager::Get()->GetBluetoothGattDescriptorClient()->ReadValue(
      object_path_, callback,
      base::Bind(&BluetoothRemoteGattDescriptorBlueZ::OnError,
                 weak_ptr_factory_.GetWeakPtr(), error_callback));
}

void BluetoothRemoteGattDescriptorBlueZ::WriteRemoteDescriptor(
    const std::vector<uint8>& new_value,
    const base::Closure& callback,
    const ErrorCallback& error_callback) {
  VLOG(1) << "Sending GATT characteristic descriptor write request to "
          << "characteristic: " << GetIdentifier()
          << ", UUID: " << GetUUID().canonical_value()
          << ", with value: " << new_value << ".";

  bluez::BluezDBusManager::Get()
      ->GetBluetoothGattDescriptorClient()
      ->WriteValue(object_path_, new_value, callback,
                   base::Bind(&BluetoothRemoteGattDescriptorBlueZ::OnError,
                              weak_ptr_factory_.GetWeakPtr(), error_callback));
}

void BluetoothRemoteGattDescriptorBlueZ::OnError(
    const ErrorCallback& error_callback,
    const std::string& error_name,
    const std::string& error_message) {
  VLOG(1) << "Operation failed: " << error_name
          << ", message: " << error_message;

  error_callback.Run(
      BluetoothRemoteGattServiceBlueZ::DBusErrorToServiceError(error_name));
}

}  // namespace bluez
