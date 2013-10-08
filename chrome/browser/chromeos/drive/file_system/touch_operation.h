// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_DRIVE_FILE_SYSTEM_TOUCH_OPERATION_H_
#define CHROME_BROWSER_CHROMEOS_DRIVE_FILE_SYSTEM_TOUCH_OPERATION_H_

#include "base/basictypes.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/chromeos/drive/file_errors.h"
#include "chrome/browser/google_apis/gdata_errorcode.h"

namespace base {
class FilePath;
class SequencedTaskRunner;
class Time;
}  // namespace base

namespace google_apis {
class ResourceEntry;
}  // namespace google_apis

namespace drive {
namespace internal {
class ResourceMetadata;
}  // namespace internal

class JobScheduler;
class ResourceEntry;

namespace file_system {

class OperationObserver;

class TouchOperation {
 public:
  TouchOperation(base::SequencedTaskRunner* blocking_task_runner,
                 OperationObserver* observer,
                 JobScheduler* scheduler,
                 internal::ResourceMetadata* metadata);
  ~TouchOperation();

  // Touches the file by updating last access time and last modified time.
  // Upon completion, invokes |callback|.
  // |last_access_time|, |last_modified_time| and |callback| must not be null.
  void TouchFile(const base::FilePath& file_path,
                 const base::Time& last_access_time,
                 const base::Time& last_modified_time,
                 const FileOperationCallback& callback);

 private:
  // Part of TouchFile(). Runs after GetResourceEntry is completed.
  void TouchFileAfterGetResourceEntry(const base::FilePath& file_path,
                                      const base::Time& last_access_time,
                                      const base::Time& last_modified_time,
                                      const FileOperationCallback& callback,
                                      ResourceEntry* entry,
                                      FileError error);

  // Part of TouchFile(). Runs after the server side update for last access time
  // and last modified time is completed.
  void TouchFileAfterServerTimeStampUpdated(
      const base::FilePath& file_path,
      const FileOperationCallback& callback,
      google_apis::GDataErrorCode gdata_error,
      scoped_ptr<google_apis::ResourceEntry> resource_entry);

  // Part of TouchFile(). Runs after refreshing the local metadata is completed.
  void TouchFileAfterRefreshMetadata(const base::FilePath& file_path,
                                     const FileOperationCallback& callback,
                                     FileError error);

  scoped_refptr<base::SequencedTaskRunner> blocking_task_runner_;
  OperationObserver* observer_;
  JobScheduler* scheduler_;
  internal::ResourceMetadata* metadata_;

  // Note: This should remain the last member so it'll be destroyed and
  // invalidate the weak pointers before any other members are destroyed.
  base::WeakPtrFactory<TouchOperation> weak_ptr_factory_;
  DISALLOW_COPY_AND_ASSIGN(TouchOperation);
};

}  // namespace file_system
}  // namespace drive

#endif  // CHROME_BROWSER_CHROMEOS_DRIVE_FILE_SYSTEM_TOUCH_OPERATION_H_
