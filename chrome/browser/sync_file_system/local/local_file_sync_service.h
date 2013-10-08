// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_FILE_SYSTEM_LOCAL_LOCAL_FILE_SYNC_SERVICE_H_
#define CHROME_BROWSER_SYNC_FILE_SYSTEM_LOCAL_LOCAL_FILE_SYNC_SERVICE_H_

#include <map>
#include <set>
#include <string>

#include "base/basictypes.h"
#include "base/callback.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "chrome/browser/sync_file_system/local/local_origin_change_observer.h"
#include "chrome/browser/sync_file_system/remote_change_processor.h"
#include "chrome/browser/sync_file_system/sync_callbacks.h"
#include "chrome/browser/sync_file_system/sync_status_code.h"

class GURL;
class Profile;

namespace fileapi {
class FileSystemContext;
}

namespace sync_file_system {

class FileChange;
class LocalChangeProcessor;
class LocalFileSyncContext;
struct LocalFileSyncInfo;

// Maintains local file change tracker and sync status.
// Owned by SyncFileSystemService (which is a per-profile object).
class LocalFileSyncService
    : public RemoteChangeProcessor,
      public LocalOriginChangeObserver,
      public base::SupportsWeakPtr<LocalFileSyncService> {
 public:
  class Observer {
   public:
    Observer() {}
    virtual ~Observer() {}

    // This is called when there're one or more local changes available.
    // |pending_changes_hint| indicates the pending queue length to help sync
    // scheduling but the value may not be accurately reflect the real-time
    // value.
    virtual void OnLocalChangeAvailable(int64 pending_changes_hint) = 0;

   private:
    DISALLOW_COPY_AND_ASSIGN(Observer);
  };

  typedef base::Callback<void(SyncStatusCode status,
                              bool has_pending_changes)>
      HasPendingLocalChangeCallback;

  explicit LocalFileSyncService(Profile* profile);
  virtual ~LocalFileSyncService();

  void Shutdown();

  void MaybeInitializeFileSystemContext(
      const GURL& app_origin,
      fileapi::FileSystemContext* file_system_context,
      const SyncStatusCallback& callback);

  void AddChangeObserver(Observer* observer);

  // Registers |url| to wait until sync is enabled for |url|.
  // |on_syncable_callback| is to be called when |url| becomes syncable
  // (i.e. when we have no pending writes and the file is successfully locked
  // for sync).
  // Calling this method again while this already has another URL waiting
  // for sync will overwrite the previously registered URL.
  void RegisterURLForWaitingSync(const fileapi::FileSystemURL& url,
                                 const base::Closure& on_syncable_callback);

  // Synchronize one (or a set of) local change(s) to the remote server
  // using local_change_processor given by SetLocalChangeProcessor().
  // |processor| must have same or longer lifetime than this service.
  // It is invalid to call this method before calling SetLocalChangeProcessor().
  void ProcessLocalChange(const SyncFileCallback& callback);

  // Sets a local change processor.  This must be called before any
  // ProcessLocalChange().
  void SetLocalChangeProcessor(LocalChangeProcessor* processor);

  // Returns true via |callback| if the given file |url| has local pending
  // changes.
  void HasPendingLocalChanges(
      const fileapi::FileSystemURL& url,
      const HasPendingLocalChangeCallback& callback);

  // A local or remote sync has been finished (either successfully or
  // with an error). Clears the internal sync flag and enable writing for |url|.
  void ClearSyncFlagForURL(const fileapi::FileSystemURL& url);

  // Returns the metadata of a remote file pointed by |url|.
  virtual void GetLocalFileMetadata(
      const fileapi::FileSystemURL& url,
      const SyncFileMetadataCallback& callback);

  // RemoteChangeProcessor overrides.
  virtual void PrepareForProcessRemoteChange(
      const fileapi::FileSystemURL& url,
      const PrepareChangeCallback& callback) OVERRIDE;
  virtual void ApplyRemoteChange(
      const FileChange& change,
      const base::FilePath& local_path,
      const fileapi::FileSystemURL& url,
      const SyncStatusCallback& callback) OVERRIDE;
  virtual void ClearLocalChanges(
      const fileapi::FileSystemURL& url,
      const base::Closure& completion_callback) OVERRIDE;
  virtual void RecordFakeLocalChange(
      const fileapi::FileSystemURL& url,
      const FileChange& change,
      const SyncStatusCallback& callback) OVERRIDE;

  // LocalOriginChangeObserver override.
  virtual void OnChangesAvailableInOrigins(
      const std::set<GURL>& origins) OVERRIDE;

  // Called when a particular origin (app) is disabled/enabled while
  // the service is running. This may be called for origins/apps that
  // are not initialized for the service.
  void SetOriginEnabled(const GURL& origin, bool enabled);

 private:
  friend class OriginChangeMapTest;

  class OriginChangeMap {
   public:
    typedef std::map<GURL, int64> Map;

    OriginChangeMap();
    ~OriginChangeMap();

    // Sets |origin| to the next origin to process. (For now we simply apply
    // round-robin to pick the next origin to avoid starvation.)
    // Returns false if no origins to process.
    bool NextOriginToProcess(GURL* origin);

    int64 GetTotalChangeCount() const;

    // Update change_count_map_ for |origin|.
    void SetOriginChangeCount(const GURL& origin, int64 changes);

    void SetOriginEnabled(const GURL& origin, bool enabled);

   private:
    // Per-origin changes (cached info, could be stale).
    Map change_count_map_;
    Map::iterator next_;

    // Holds a set of disabled (but initialized) origins.
    std::set<GURL> disabled_origins_;
  };

  void DidInitializeFileSystemContext(
      const GURL& app_origin,
      fileapi::FileSystemContext* file_system_context,
      const SyncStatusCallback& callback,
      SyncStatusCode status);
  void DidInitializeForRemoteSync(
      const fileapi::FileSystemURL& url,
      fileapi::FileSystemContext* file_system_context,
      const PrepareChangeCallback& callback,
      SyncStatusCode status);

  // Runs local_sync_callback_ and resets it.
  void RunLocalSyncCallback(
      SyncStatusCode status,
      const fileapi::FileSystemURL& url);

  // Callbacks for ProcessLocalChange.
  void DidGetFileForLocalSync(
      SyncStatusCode status,
      const LocalFileSyncInfo& sync_file_info);
  void ProcessNextChangeForURL(
      const LocalFileSyncInfo& sync_file_info,
      const FileChange& last_change,
      const FileChangeList& changes,
      SyncStatusCode status);

  Profile* profile_;

  scoped_refptr<LocalFileSyncContext> sync_context_;

  // Origin to context map. (Assuming that as far as we're in the same
  // profile single origin wouldn't belong to multiple FileSystemContexts.)
  std::map<GURL, fileapi::FileSystemContext*> origin_to_contexts_;

  // Origins which have pending changes but have not been initialized yet.
  // (Used only for handling dirty files left in the local tracker database
  // after a restart.)
  std::set<GURL> pending_origins_with_changes_;

  OriginChangeMap origin_change_map_;

  // This callback is non-null while a local sync is running (i.e.
  // ProcessLocalChange has been called and has not been returned yet).
  SyncFileCallback local_sync_callback_;

  LocalChangeProcessor* local_change_processor_;

  ObserverList<Observer> change_observers_;

  DISALLOW_COPY_AND_ASSIGN(LocalFileSyncService);
};

}  // namespace sync_file_system

#endif  // CHROME_BROWSER_SYNC_FILE_SYSTEM_LOCAL_LOCAL_FILE_SYNC_SERVICE_H_
