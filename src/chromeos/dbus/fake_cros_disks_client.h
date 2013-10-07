// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_DBUS_FAKE_CROS_DISKS_CLIENT_H_
#define CHROMEOS_DBUS_FAKE_CROS_DISKS_CLIENT_H_

#include <string>

#include "base/callback.h"
#include "chromeos/dbus/cros_disks_client.h"

namespace chromeos {

// A fake implementation of CrosDiskeClient. This class provides a fake behavior
// and the user of this class can raise a fake mouse events.
class FakeCrosDisksClient : public CrosDisksClient {
 public:
  FakeCrosDisksClient();
  virtual ~FakeCrosDisksClient();

  // CrosDisksClient overrides.
  virtual void Mount(const std::string& source_path,
                     const std::string& source_format,
                     const std::string& mount_label,
                     const base::Closure& callback,
                     const base::Closure& error_callback) OVERRIDE;
  virtual void Unmount(const std::string& device_path,
                       UnmountOptions options,
                       const base::Closure& callback,
                       const base::Closure& error_callback) OVERRIDE;
  virtual void EnumerateAutoMountableDevices(
      const EnumerateAutoMountableDevicesCallback& callback,
      const base::Closure& error_callback) OVERRIDE;
  virtual void FormatDevice(const std::string& device_path,
                            const std::string& filesystem,
                            const FormatDeviceCallback& callback,
                            const base::Closure& error_callback) OVERRIDE;
  virtual void GetDeviceProperties(
      const std::string& device_path,
      const GetDevicePropertiesCallback& callback,
      const base::Closure& error_callback) OVERRIDE;
  virtual void SetUpConnections(
      const MountEventHandler& mount_event_handler,
      const MountCompletedHandler& mount_completed_handler) OVERRIDE;

  // Used in tests to simulate signals sent by cros disks layer.
  // Invokes handlers set in |SetUpConnections|.
  bool SendMountEvent(MountEventType event, const std::string& path);
  bool SendMountCompletedEvent(MountError error_code,
                               const std::string& source_path,
                               MountType mount_type,
                               const std::string& mount_path);

  // Returns how many times Unmount() was called.
  int unmount_call_count() const {
    return unmount_call_count_;
  }

  // Returns the |device_path| parameter from the last invocation of Unmount().
  const std::string& last_unmount_device_path() const {
    return last_unmount_device_path_;
  }

  // Returns the |options| parameter from the last invocation of Unmount().
  UnmountOptions last_unmount_options() const {
    return last_unmount_options_;
  }

  // Makes the subsequent Unmount() calls fail. Unmount() succeeds by default.
  void MakeUnmountFail() {
    unmount_success_ = false;
  }

  // Sets a listener callbackif the following Unmount() call is success or not.
  // Unmount() calls the corresponding callback given as a parameter.
  void set_unmount_listener(base::Closure listener) {
    unmount_listener_ = listener;
  }

  // Returns how many times FormatDevice() was called.
  int format_device_call_count() const {
    return format_device_call_count_;
  }

  // Returns the |device_path| parameter from the last invocation of
  // FormatDevice().
  const std::string& last_format_device_device_path() const {
    return last_format_device_device_path_;
  }

  // Returns the |filesystem| parameter from the last invocation of
  // FormatDevice().
  const std::string& last_format_device_filesystem() const {
    return last_format_device_filesystem_;
  }

  // Makes the subsequent FormatDevice() calls fail. FormatDevice() succeeds by
  // default.
  void MakeFormatDeviceFail() {
    format_device_success_ = false;
  }

 private:
  MountEventHandler mount_event_handler_;
  MountCompletedHandler mount_completed_handler_;

  int unmount_call_count_;
  std::string last_unmount_device_path_;
  UnmountOptions last_unmount_options_;
  bool unmount_success_;
  base::Closure unmount_listener_;
  int format_device_call_count_;
  std::string last_format_device_device_path_;
  std::string last_format_device_filesystem_;
  bool format_device_success_;
};

}  // namespace chromeos

#endif  // CHROMEOS_DBUS_FAKE_CROS_DISKS_CLIENT_H_
