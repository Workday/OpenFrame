// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/dbus/fake_cros_disks_client.h"

#include "base/bind.h"
#include "base/message_loop/message_loop.h"

namespace chromeos {

FakeCrosDisksClient::FakeCrosDisksClient()
  : unmount_call_count_(0),
    unmount_success_(true),
    format_device_call_count_(0),
    format_device_success_(true) {
}

FakeCrosDisksClient::~FakeCrosDisksClient() {}

void FakeCrosDisksClient::Mount(const std::string& source_path,
                                const std::string& source_format,
                                const std::string& mount_label,
                                const base::Closure& callback,
                                const base::Closure& error_callback) {
}

void FakeCrosDisksClient::Unmount(const std::string& device_path,
                                  UnmountOptions options,
                                  const base::Closure& callback,
                                  const base::Closure& error_callback) {
  DCHECK(!callback.is_null());
  DCHECK(!error_callback.is_null());

  unmount_call_count_++;
  last_unmount_device_path_ = device_path;
  last_unmount_options_ = options;
  base::MessageLoopProxy::current()->PostTask(
      FROM_HERE, unmount_success_ ? callback : error_callback);
  if(!unmount_listener_.is_null())
    unmount_listener_.Run();
}

void FakeCrosDisksClient::EnumerateAutoMountableDevices(
    const EnumerateAutoMountableDevicesCallback& callback,
    const base::Closure& error_callback) {
}

void FakeCrosDisksClient::FormatDevice(const std::string& device_path,
                                       const std::string& filesystem,
                                       const FormatDeviceCallback& callback,
                                       const base::Closure& error_callback) {
  DCHECK(!callback.is_null());
  DCHECK(!error_callback.is_null());

  format_device_call_count_++;
  last_format_device_device_path_ = device_path;
  last_format_device_filesystem_ = filesystem;
  if (format_device_success_) {
    base::MessageLoopProxy::current()->PostTask(FROM_HERE,
                                                base::Bind(callback, true));
  } else {
    base::MessageLoopProxy::current()->PostTask(FROM_HERE, error_callback);
  }
}

void FakeCrosDisksClient::GetDeviceProperties(
    const std::string& device_path,
    const GetDevicePropertiesCallback& callback,
    const base::Closure& error_callback) {
}

void FakeCrosDisksClient::SetUpConnections(
    const MountEventHandler& mount_event_handler,
    const MountCompletedHandler& mount_completed_handler) {
  mount_event_handler_ = mount_event_handler;
  mount_completed_handler_ = mount_completed_handler;
}

bool FakeCrosDisksClient::SendMountEvent(MountEventType event,
                                         const std::string& path) {
  if (mount_event_handler_.is_null())
    return false;
  mount_event_handler_.Run(event, path);
  return true;
}

bool FakeCrosDisksClient::SendMountCompletedEvent(
    MountError error_code,
    const std::string& source_path,
    MountType mount_type,
    const std::string& mount_path) {
  if (mount_completed_handler_.is_null())
    return false;
  mount_completed_handler_.Run(error_code, source_path, mount_type, mount_path);
  return true;
}

}  // namespace chromeos
