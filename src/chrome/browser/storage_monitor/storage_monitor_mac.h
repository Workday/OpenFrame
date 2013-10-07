// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_STORAGE_MONITOR_STORAGE_MONITOR_MAC_H_
#define CHROME_BROWSER_STORAGE_MONITOR_STORAGE_MONITOR_MAC_H_

#include <DiskArbitration/DiskArbitration.h>
#include <map>

#include "base/mac/scoped_cftyperef.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/storage_monitor/storage_monitor.h"

namespace chrome {

class ImageCaptureDeviceManager;

// This class posts notifications to listeners when a new disk
// is attached, removed, or changed.
class StorageMonitorMac : public StorageMonitor,
                          public base::SupportsWeakPtr<StorageMonitorMac> {
 public:
  enum UpdateType {
    UPDATE_DEVICE_ADDED,
    UPDATE_DEVICE_CHANGED,
    UPDATE_DEVICE_REMOVED,
  };

  // Should only be called by browser start up code.  Use GetInstance() instead.
  StorageMonitorMac();

  virtual ~StorageMonitorMac();

  virtual void Init() OVERRIDE;

  void UpdateDisk(const std::string& bsd_name,
                  const StorageInfo& info,
                  UpdateType update_type);

  virtual bool GetStorageInfoForPath(
      const base::FilePath& path,
      StorageInfo* device_info) const OVERRIDE;

  virtual void EjectDevice(
      const std::string& device_id,
      base::Callback<void(EjectStatus)> callback) OVERRIDE;

 private:
  static void DiskAppearedCallback(DADiskRef disk, void* context);
  static void DiskDisappearedCallback(DADiskRef disk, void* context);
  static void DiskDescriptionChangedCallback(DADiskRef disk,
                                             CFArrayRef keys,
                                             void *context);
  void GetDiskInfoAndUpdate(DADiskRef disk, UpdateType update_type);

  bool ShouldPostNotificationForDisk(const StorageInfo& info) const;
  bool FindDiskWithMountPoint(const base::FilePath& mount_point,
                              StorageInfo* info) const;

  base::ScopedCFTypeRef<DASessionRef> session_;
  // Maps disk bsd names to disk info objects. This map tracks all mountable
  // devices on the system, though only notifications for removable devices are
  // posted.
  std::map<std::string, StorageInfo> disk_info_map_;

  int pending_disk_updates_;

  scoped_ptr<chrome::ImageCaptureDeviceManager> image_capture_device_manager_;

  DISALLOW_COPY_AND_ASSIGN(StorageMonitorMac);
};

}  // namespace chrome

#endif  // CHROME_BROWSER_STORAGE_MONITOR_STORAGE_MONITOR_MAC_H_
