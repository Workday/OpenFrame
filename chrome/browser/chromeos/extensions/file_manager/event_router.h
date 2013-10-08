// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_EXTENSIONS_FILE_MANAGER_EVENT_ROUTER_H_
#define CHROME_BROWSER_CHROMEOS_EXTENSIONS_FILE_MANAGER_EVENT_ROUTER_H_

#include <map>
#include <string>

#include "base/files/file_path_watcher.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/chromeos/drive/drive_integration_service.h"
#include "chrome/browser/chromeos/drive/file_system_observer.h"
#include "chrome/browser/chromeos/drive/job_list.h"
#include "chrome/browser/chromeos/extensions/file_manager/file_watcher.h"
#include "chrome/browser/drive/drive_service_interface.h"
#include "chromeos/disks/disk_mount_manager.h"
#include "chromeos/network/network_state_handler_observer.h"

class PrefChangeRegistrar;
class Profile;

namespace chromeos {
class NetworkState;
}

namespace file_manager {

class DesktopNotifications;
class MountedDiskMonitor;

// Monitors changes in disk mounts, network connection state and preferences
// affecting File Manager. Dispatches appropriate File Browser events.
class EventRouter
    : public chromeos::disks::DiskMountManager::Observer,
      public chromeos::NetworkStateHandlerObserver,
      public drive::DriveIntegrationServiceObserver,
      public drive::FileSystemObserver,
      public drive::JobListObserver,
      public drive::DriveServiceObserver {
 public:
  explicit EventRouter(Profile* profile);
  virtual ~EventRouter();

  void Shutdown();

  // Starts observing file system change events.
  void ObserveFileSystemEvents();

  typedef base::Callback<void(bool success)> BoolCallback;

  // Adds a file watch at |local_path|, associated with |virtual_path|, for
  // an extension with |extension_id|.
  //
  // |callback| will be called with true on success, or false on failure.
  // |callback| must not be null.
  void AddFileWatch(const base::FilePath& local_path,
                    const base::FilePath& virtual_path,
                    const std::string& extension_id,
                    const BoolCallback& callback);

  // Removes a file watch at |local_path| for an extension with |extension_id|.
  void RemoveFileWatch(const base::FilePath& local_path,
                       const std::string& extension_id);

  // CrosDisksClient::Observer overrides.
  virtual void OnDiskEvent(
      chromeos::disks::DiskMountManager::DiskEvent event,
      const chromeos::disks::DiskMountManager::Disk* disk) OVERRIDE;
  virtual void OnDeviceEvent(
      chromeos::disks::DiskMountManager::DeviceEvent event,
      const std::string& device_path) OVERRIDE;
  virtual void OnMountEvent(
      chromeos::disks::DiskMountManager::MountEvent event,
      chromeos::MountError error_code,
      const chromeos::disks::DiskMountManager::MountPointInfo& mount_info)
      OVERRIDE;
  virtual void OnFormatEvent(
      chromeos::disks::DiskMountManager::FormatEvent event,
      chromeos::FormatError error_code,
      const std::string& device_path) OVERRIDE;

  // chromeos::NetworkStateHandlerObserver overrides.
  virtual void NetworkManagerChanged() OVERRIDE;
  virtual void DefaultNetworkChanged(
      const chromeos::NetworkState* network) OVERRIDE;

  // drive::JobListObserver overrides.
  virtual void OnJobAdded(const drive::JobInfo& job_info) OVERRIDE;
  virtual void OnJobUpdated(const drive::JobInfo& job_info) OVERRIDE;
  virtual void OnJobDone(const drive::JobInfo& job_info,
                         drive::FileError error) OVERRIDE;

  // drive::DriveServiceObserver overrides.
  virtual void OnRefreshTokenInvalid() OVERRIDE;

  // drive::FileSystemObserver overrides.
  virtual void OnDirectoryChanged(
      const base::FilePath& directory_path) OVERRIDE;

  // drive::DriveIntegrationServiceObserver overrides.
  virtual void OnFileSystemMounted() OVERRIDE;
  virtual void OnFileSystemBeingUnmounted() OVERRIDE;

 private:
  typedef std::map<base::FilePath, FileWatcher*> WatcherMap;

  // USB mount event handlers.
  void OnDiskAdded(const chromeos::disks::DiskMountManager::Disk* disk);
  void OnDiskRemoved(const chromeos::disks::DiskMountManager::Disk* disk);
  void OnDiskMounted(const chromeos::disks::DiskMountManager::Disk* disk);
  void OnDiskUnmounted(const chromeos::disks::DiskMountManager::Disk* disk);
  void OnDeviceAdded(const std::string& device_path);
  void OnDeviceRemoved(const std::string& device_path);
  void OnDeviceScanned(const std::string& device_path);
  void OnFormatStarted(const std::string& device_path, bool success);
  void OnFormatCompleted(const std::string& device_path, bool success);

  // Called on change to kExternalStorageDisabled pref.
  void OnExternalStorageDisabledChanged();

  // Called when prefs related to file manager change.
  void OnFileManagerPrefsChanged();

  // Process file watch notifications.
  void HandleFileWatchNotification(const base::FilePath& path,
                                   bool got_error);

  // Sends directory change event.
  void DispatchDirectoryChangeEvent(
      const base::FilePath& path,
      bool error,
      const std::vector<std::string>& extension_ids);

  void DispatchMountEvent(
      chromeos::disks::DiskMountManager::MountEvent event,
      chromeos::MountError error_code,
      const chromeos::disks::DiskMountManager::MountPointInfo& mount_info);

  // If needed, opens a file manager window for the removable device mounted at
  // |mount_path|. Disk.mount_path() is empty, since it is being filled out
  // after calling notifying observers by DiskMountManager.
  void ShowRemovableDeviceInFileManager(
      const chromeos::disks::DiskMountManager::Disk& disk,
      const base::FilePath& mount_path);

  // Sends onFileTranferUpdated to extensions if needed. If |always| is true,
  // it sends the event always. Otherwise, it sends the event if enough time has
  // passed from the previous event so as not to make extension busy.
  void SendDriveFileTransferEvent(bool always);

  // Manages the list of currently active Drive file transfer jobs.
  struct DriveJobInfoWithStatus {
    DriveJobInfoWithStatus();
    DriveJobInfoWithStatus(const drive::JobInfo& info,
                           const std::string& status);
    drive::JobInfo job_info;
    std::string status;
  };
  std::map<drive::JobID, DriveJobInfoWithStatus> drive_jobs_;
  base::Time last_file_transfer_event_;

  WatcherMap file_watchers_;
  scoped_ptr<DesktopNotifications> notifications_;
  scoped_ptr<PrefChangeRegistrar> pref_change_registrar_;
  scoped_ptr<MountedDiskMonitor> mounted_disk_monitor_;
  Profile* profile_;

  // Note: This should remain the last member so it'll be destroyed and
  // invalidate the weak pointers before any other members are destroyed.
  base::WeakPtrFactory<EventRouter> weak_factory_;
  DISALLOW_COPY_AND_ASSIGN(EventRouter);
};

}  // namespace file_manager

#endif  // CHROME_BROWSER_CHROMEOS_EXTENSIONS_FILE_MANAGER_EVENT_ROUTER_H_
