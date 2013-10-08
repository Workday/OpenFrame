// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_STORAGE_MONITOR_MEDIA_TRANSFER_PROTOCOL_DEVICE_OBSERVER_LINUX_H_
#define CHROME_BROWSER_STORAGE_MONITOR_MEDIA_TRANSFER_PROTOCOL_DEVICE_OBSERVER_LINUX_H_

#include <map>
#include <string>

#include "base/strings/string16.h"
#include "chrome/browser/storage_monitor/storage_monitor.h"
#include "device/media_transfer_protocol/media_transfer_protocol_manager.h"

namespace base {
class FilePath;
}

namespace chrome {

// Gets the mtp device information given a |storage_name|. On success,
// fills in |id|, |name| and |location|.
typedef void (*GetStorageInfoFunc)(
    const std::string& storage_name,
    device::MediaTransferProtocolManager* mtp_manager,
    std::string* id,
    string16* name,
    std::string* location);

// Helper class to send MTP storage attachment and detachment events to
// StorageMonitor.
class MediaTransferProtocolDeviceObserverLinux
    : public device::MediaTransferProtocolManager::Observer {
 public:
  MediaTransferProtocolDeviceObserverLinux(
      StorageMonitor::Receiver* receiver,
      device::MediaTransferProtocolManager* mtp_manager);
  virtual ~MediaTransferProtocolDeviceObserverLinux();

  // Finds the storage that contains |path| and populates |storage_info|.
  // Returns false if unable to find the storage.
  bool GetStorageInfoForPath(const base::FilePath& path,
                             StorageInfo* storage_info) const;

 protected:
  // Only used in unit tests.
  MediaTransferProtocolDeviceObserverLinux(
      StorageMonitor::Receiver* receiver,
      device::MediaTransferProtocolManager* mtp_manager,
      GetStorageInfoFunc get_storage_info_func);

  // device::MediaTransferProtocolManager::Observer implementation.
  // Exposed for unit tests.
  virtual void StorageChanged(bool is_attached,
                              const std::string& storage_name) OVERRIDE;

 private:
  // Mapping of storage location and mtp storage info object.
  typedef std::map<std::string, StorageInfo> StorageLocationToInfoMap;

  // Enumerate existing mtp storage devices.
  void EnumerateStorages();

  // Pointer to the MTP manager. Not owned. Client must ensure the MTP
  // manager outlives this object.
  device::MediaTransferProtocolManager* mtp_manager_;

  // Map of all attached mtp devices.
  StorageLocationToInfoMap storage_map_;

  // Function handler to get storage information. This is useful to set a mock
  // handler for unit testing.
  GetStorageInfoFunc get_storage_info_func_;

  // The notifications object to use to signal newly attached devices.
  // Guaranteed to outlive this class.
  StorageMonitor::Receiver* const notifications_;

  DISALLOW_COPY_AND_ASSIGN(MediaTransferProtocolDeviceObserverLinux);
};

}  // namespace chrome

#endif  // CHROME_BROWSER_STORAGE_MONITOR_MEDIA_TRANSFER_PROTOCOL_DEVICE_OBSERVER_LINUX_H_
