// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/bluetooth/bluetooth_device_win.h"

#include <string>

#include "base/basictypes.h"
#include "base/logging.h"
#include "base/memory/scoped_vector.h"
#include "base/strings/stringprintf.h"
#include "device/bluetooth/bluetooth_out_of_band_pairing_data.h"
#include "device/bluetooth/bluetooth_profile_win.h"
#include "device/bluetooth/bluetooth_service_record_win.h"
#include "device/bluetooth/bluetooth_socket_win.h"
#include "device/bluetooth/bluetooth_task_manager_win.h"

namespace {

const int kSdpBytesBufferSize = 1024;

}  // namespace

namespace device {

BluetoothDeviceWin::BluetoothDeviceWin(
    const BluetoothTaskManagerWin::DeviceState& state)
    : BluetoothDevice() {
  name_ = state.name;
  address_ = state.address;
  bluetooth_class_ = state.bluetooth_class;
  visible_ = state.visible;
  connected_ = state.connected;
  paired_ = state.authenticated;

  for (ScopedVector<BluetoothTaskManagerWin::ServiceRecordState>::const_iterator
       iter = state.service_record_states.begin();
       iter != state.service_record_states.end();
       ++iter) {
    uint8 sdp_bytes_buffer[kSdpBytesBufferSize];
    std::copy((*iter)->sdp_bytes.begin(),
              (*iter)->sdp_bytes.end(),
              sdp_bytes_buffer);
    BluetoothServiceRecord* service_record = new BluetoothServiceRecordWin(
        (*iter)->name,
        (*iter)->address,
        (*iter)->sdp_bytes.size(),
        sdp_bytes_buffer);
    service_record_list_.push_back(service_record);
    service_uuids_.push_back(service_record->uuid());
  }
}

BluetoothDeviceWin::~BluetoothDeviceWin() {
}

void BluetoothDeviceWin::SetVisible(bool visible) {
  visible_ = visible;
}

uint32 BluetoothDeviceWin::GetBluetoothClass() const {
  return bluetooth_class_;
}

std::string BluetoothDeviceWin::GetDeviceName() const {
  return name_;
}

std::string BluetoothDeviceWin::GetAddress() const {
  return address_;
}

uint16 BluetoothDeviceWin::GetVendorID() const {
  return 0;
}

uint16 BluetoothDeviceWin::GetProductID() const {
  return 0;
}

uint16 BluetoothDeviceWin::GetDeviceID() const {
  return 0;
}

bool BluetoothDeviceWin::IsPaired() const {
  return paired_;
}

bool BluetoothDeviceWin::IsConnected() const {
  return connected_;
}

bool BluetoothDeviceWin::IsConnectable() const {
  return false;
}

bool BluetoothDeviceWin::IsConnecting() const {
  return false;
}

BluetoothDevice::ServiceList BluetoothDeviceWin::GetServices() const {
  return service_uuids_;
}

void BluetoothDeviceWin::GetServiceRecords(
    const ServiceRecordsCallback& callback,
    const ErrorCallback& error_callback) {
  callback.Run(service_record_list_);
}

void BluetoothDeviceWin::ProvidesServiceWithName(
    const std::string& name,
    const ProvidesServiceCallback& callback) {
  for (ServiceRecordList::const_iterator iter = service_record_list_.begin();
       iter != service_record_list_.end();
       ++iter) {
    if ((*iter)->name() == name) {
      callback.Run(true);
      return;
    }
  }
  callback.Run(false);
}

bool BluetoothDeviceWin::ExpectingPinCode() const {
  NOTIMPLEMENTED();
  return false;
}

bool BluetoothDeviceWin::ExpectingPasskey() const {
  NOTIMPLEMENTED();
  return false;
}

bool BluetoothDeviceWin::ExpectingConfirmation() const {
  NOTIMPLEMENTED();
  return false;
}

void BluetoothDeviceWin::Connect(
    PairingDelegate* pairing_delegate,
    const base::Closure& callback,
    const ConnectErrorCallback& error_callback) {
  NOTIMPLEMENTED();
}

void BluetoothDeviceWin::SetPinCode(const std::string& pincode) {
  NOTIMPLEMENTED();
}

void BluetoothDeviceWin::SetPasskey(uint32 passkey) {
  NOTIMPLEMENTED();
}

void BluetoothDeviceWin::ConfirmPairing() {
  NOTIMPLEMENTED();
}

void BluetoothDeviceWin::RejectPairing() {
  NOTIMPLEMENTED();
}

void BluetoothDeviceWin::CancelPairing() {
  NOTIMPLEMENTED();
}

void BluetoothDeviceWin::Disconnect(
    const base::Closure& callback,
    const ErrorCallback& error_callback) {
  NOTIMPLEMENTED();
}

void BluetoothDeviceWin::Forget(const ErrorCallback& error_callback) {
  NOTIMPLEMENTED();
}

void BluetoothDeviceWin::ConnectToService(
    const std::string& service_uuid,
    const SocketCallback& callback) {
  for (ServiceRecordList::const_iterator iter = service_record_list_.begin();
       iter != service_record_list_.end();
       ++iter) {
    if ((*iter)->uuid() == service_uuid) {
      // If multiple service records are found, use the first one that works.
      scoped_refptr<BluetoothSocket> socket(
          BluetoothSocketWin::CreateBluetoothSocket(**iter));
      if (socket.get() != NULL) {
        callback.Run(socket);
        return;
      }
    }
  }
}

void BluetoothDeviceWin::ConnectToProfile(
    device::BluetoothProfile* profile,
    const base::Closure& callback,
    const ErrorCallback& error_callback) {
  if (static_cast<BluetoothProfileWin*>(profile)->Connect(this))
    callback.Run();
  else
    error_callback.Run();
}

void BluetoothDeviceWin::SetOutOfBandPairingData(
    const BluetoothOutOfBandPairingData& data,
    const base::Closure& callback,
    const ErrorCallback& error_callback) {
  NOTIMPLEMENTED();
}

void BluetoothDeviceWin::ClearOutOfBandPairingData(
    const base::Closure& callback,
    const ErrorCallback& error_callback) {
  NOTIMPLEMENTED();
}

const BluetoothServiceRecord* BluetoothDeviceWin::GetServiceRecord(
    const std::string& uuid) const {
  for (ServiceRecordList::const_iterator iter = service_record_list_.begin();
       iter != service_record_list_.end();
       ++iter) {
    if ((*iter)->uuid().compare(uuid) == 0)
      return *iter;
  }
  return NULL;
}

}  // namespace device
