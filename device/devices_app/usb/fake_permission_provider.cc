// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/devices_app/usb/fake_permission_provider.h"

namespace device {
namespace usb {

FakePermissionProvider::FakePermissionProvider() {}

FakePermissionProvider::~FakePermissionProvider() {}

void FakePermissionProvider::HasDevicePermission(
    mojo::Array<DeviceInfoPtr> requested_devices,
    const HasDevicePermissionCallback& callback) {
  mojo::Array<mojo::String> allowed_guids(requested_devices.size());
  for (size_t i = 0; i < requested_devices.size(); ++i)
    allowed_guids[i] = requested_devices[i]->guid;
  callback.Run(allowed_guids.Pass());
}

void FakePermissionProvider::HasConfigurationPermission(
    uint8_t requested_configuration,
    device::usb::DeviceInfoPtr device,
    const HasInterfacePermissionCallback& callback) {
  callback.Run(true);
}
void FakePermissionProvider::HasInterfacePermission(
    uint8_t requested_interface,
    uint8_t configuration_value,
    device::usb::DeviceInfoPtr device,
    const HasInterfacePermissionCallback& callback) {
  callback.Run(true);
}

void FakePermissionProvider::Bind(
    mojo::InterfaceRequest<PermissionProvider> request) {
  bindings_.AddBinding(this, request.Pass());
}

}  // namespace usb
}  // namespace device
