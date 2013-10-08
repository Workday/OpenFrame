// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_STORAGE_MONITOR_STORAGE_MONITOR_WIN_H_
#define CHROME_BROWSER_STORAGE_MONITOR_STORAGE_MONITOR_WIN_H_

#include "base/basictypes.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/storage_monitor/storage_monitor.h"

namespace base {
class FilePath;
}

namespace chrome {

namespace test {
class TestStorageMonitorWin;
}

class PortableDeviceWatcherWin;
class VolumeMountWatcherWin;

class StorageMonitorWin : public StorageMonitor {
 public:
  virtual ~StorageMonitorWin();

  // Must be called after the file thread is created.
  virtual void Init() OVERRIDE;

  // StorageMonitor:
  virtual bool GetStorageInfoForPath(const base::FilePath& path,
                                     StorageInfo* device_info) const OVERRIDE;
  virtual bool GetMTPStorageInfoFromDeviceId(
      const std::string& storage_device_id,
      base::string16* device_location,
      base::string16* storage_object_id) const OVERRIDE;

  virtual void EjectDevice(
      const std::string& device_id,
      base::Callback<void(EjectStatus)> callback) OVERRIDE;

 private:
  class PortableDeviceNotifications;
  friend class test::TestStorageMonitorWin;
  friend StorageMonitor* StorageMonitor::Create();

  // To support unit tests, this constructor takes |volume_mount_watcher| and
  // |portable_device_watcher| objects. These params are either constructed in
  // unit tests or in StorageMonitorWin Create() function.
  StorageMonitorWin(VolumeMountWatcherWin* volume_mount_watcher,
                    PortableDeviceWatcherWin* portable_device_watcher);

  // Gets the removable storage information given a |device_path|. On success,
  // returns true and fills in |info|.
  bool GetDeviceInfo(const base::FilePath& device_path,
                     StorageInfo* info) const;

  static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT message, WPARAM wparam,
                                       LPARAM lparam);

  LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam,
                           LPARAM lparam);

  void OnDeviceChange(UINT event_type, LPARAM data);

  // The window class of |window_|.
  ATOM window_class_;

  // The handle of the module that contains the window procedure of |window_|.
  HMODULE instance_;
  HWND window_;

  // The volume mount point watcher, used to manage the mounted devices.
  scoped_ptr<VolumeMountWatcherWin> volume_mount_watcher_;

  // The portable device watcher, used to manage media transfer protocol
  // devices.
  scoped_ptr<PortableDeviceWatcherWin> portable_device_watcher_;

  DISALLOW_COPY_AND_ASSIGN(StorageMonitorWin);
};

}  // namespace chrome

#endif  // CHROME_BROWSER_STORAGE_MONITOR_STORAGE_MONITOR_WIN_H_
