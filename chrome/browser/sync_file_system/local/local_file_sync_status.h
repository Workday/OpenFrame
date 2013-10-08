// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_FILE_SYSTEM_LOCAL_LOCAL_FILE_SYNC_STATUS_H_
#define CHROME_BROWSER_SYNC_FILE_SYSTEM_LOCAL_LOCAL_FILE_SYNC_STATUS_H_

#include <map>
#include <set>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/observer_list.h"
#include "base/threading/non_thread_safe.h"
#include "webkit/browser/fileapi/file_system_url.h"

namespace fileapi {
class FileSystemURL;
}

namespace sync_file_system {

// Represents local file sync status.
// This class is supposed to run only on IO thread.
//
// This class manages two important synchronization flags: writing (counter)
// and syncing (flag).  Writing counter keeps track of which URL is in
// writing and syncing flag indicates which URL is in syncing.
//
// An entry can have multiple writers but sync is exclusive and cannot overwrap
// with any writes or syncs.
class LocalFileSyncStatus
    : public base::NonThreadSafe {
 public:
  class Observer {
   public:
    Observer() {}
    virtual ~Observer() {}
    virtual void OnSyncEnabled(const fileapi::FileSystemURL& url) = 0;
    virtual void OnWriteEnabled(const fileapi::FileSystemURL& url) = 0;
   private:
    DISALLOW_COPY_AND_ASSIGN(Observer);
  };

  LocalFileSyncStatus();
  ~LocalFileSyncStatus();

  // Increment writing counter for |url|.
  // This should not be called if the |url| is not writable.
  void StartWriting(const fileapi::FileSystemURL& url);

  // Decrement writing counter for |url|.
  void EndWriting(const fileapi::FileSystemURL& url);

  // Start syncing for |url| and disable writing.
  // This should not be called if |url| is in syncing or in writing.
  void StartSyncing(const fileapi::FileSystemURL& url);

  // Clears the syncing flag for |url| and enable writing.
  void EndSyncing(const fileapi::FileSystemURL& url);

  // Returns true if the |url| or its parent or child is in writing.
  bool IsWriting(const fileapi::FileSystemURL& url) const;

  // Returns true if the |url| is enabled for writing (i.e. not in syncing).
  bool IsWritable(const fileapi::FileSystemURL& url) const;

  // Returns true if the |url| is enabled for syncing (i.e. neither in
  // syncing nor writing).
  bool IsSyncable(const fileapi::FileSystemURL& url) const;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

 private:
  typedef std::map<fileapi::FileSystemURL,int64,
                   fileapi::FileSystemURL::Comparator> URLCountMap;

  bool IsChildOrParentWriting(const fileapi::FileSystemURL& url) const;
  bool IsChildOrParentSyncing(const fileapi::FileSystemURL& url) const;

  // If this count is non-zero positive there're ongoing write operations.
  URLCountMap writing_;

  // If this flag is set sync process is running on the file.
  fileapi::FileSystemURLSet syncing_;

  ObserverList<Observer> observer_list_;

  DISALLOW_COPY_AND_ASSIGN(LocalFileSyncStatus);
};

}  // namespace sync_file_system

#endif  // CHROME_BROWSER_SYNC_FILE_SYSTEM_LOCAL_LOCAL_FILE_SYNC_STATUS_H_
