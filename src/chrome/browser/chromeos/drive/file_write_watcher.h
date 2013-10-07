// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_DRIVE_FILE_WRITE_WATCHER_H_
#define CHROME_BROWSER_CHROMEOS_DRIVE_FILE_WRITE_WATCHER_H_

#include "base/callback_forward.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/chromeos/drive/file_system_util.h"

namespace base {
class FilePath;
}  // namespace base

namespace drive {

namespace file_system {
class OperationObserver;
}  // namespace file_system

namespace internal {

typedef base::Callback<void(bool)> StartWatchCallback;

// The class watches modification to Drive files in the cache directory.
// This is used for returning a local writable snapshot of Drive files from the
// Save-As file dialog, so that the callers of the dialog can save to Drive
// without any special handling about Drive.
class FileWriteWatcher {
 public:
  explicit FileWriteWatcher(file_system::OperationObserver* observer);
  ~FileWriteWatcher();

  // Starts watching the modification to |path|. When it successfully started
  // watching, it runs |callback| by passing true as the argument. Or if it
  // failed, the callback is run with false.
  // When modification is detected, it is notified to the |observer| passed to
  // the constructor by calling OnCacheFileUploadNeededByOperation(resource_id).
  //
  // Currently, the modification is watched in "one-shot" manner. That is, once
  // a modification is notified, the watch is deactivated for freeing system
  // resources. As a heuristic to capture the real end of write operations that
  // might be done by several chunked writes, the notification is fired after
  // 5 seconds has passed after the last write operation is detected.
  //
  // TODO(kinaba): investigate the possibility to continuously watch the whole
  // cache directory. http://crbug.com/269424
  void StartWatch(const base::FilePath& path,
                  const std::string& resource_id,
                  const StartWatchCallback& callback);

  // For testing purpose, stops inserting delay between the write detection and
  // notification to the observer.
  void DisableDelayForTesting();

 private:
  // Invoked when a modification is observed.
  void OnWriteEvent(const std::string& resource_id);

  class FileWriteWatcherImpl;
  scoped_ptr<FileWriteWatcherImpl, util::DestroyHelper> watcher_impl_;
  file_system::OperationObserver* operation_observer_;

  // Note: This should remain the last member so it'll be destroyed and
  // invalidate its weak pointers before any other members are destroyed.
  base::WeakPtrFactory<FileWriteWatcher> weak_ptr_factory_;
  DISALLOW_COPY_AND_ASSIGN(FileWriteWatcher);
};

}  // namespace internal
}  // namespace drive

#endif  // CHROME_BROWSER_CHROMEOS_DRIVE_FILE_WRITE_WATCHER_H_
