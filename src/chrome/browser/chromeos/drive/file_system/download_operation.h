// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_DRIVE_FILE_SYSTEM_DOWNLOAD_OPERATION_H_
#define CHROME_BROWSER_CHROMEOS_DRIVE_FILE_SYSTEM_DOWNLOAD_OPERATION_H_

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/chromeos/drive/file_errors.h"
#include "chrome/browser/chromeos/drive/file_system_interface.h"
#include "chrome/browser/chromeos/drive/job_list.h"
#include "chrome/browser/google_apis/gdata_errorcode.h"

namespace base {
class FilePath;
class SequencedTaskRunner;
}  // namespace base

namespace google_apis {
class ResourceEntry;
}  // namespace google_apis

namespace drive {

class JobScheduler;
class ResourceEntry;

namespace internal {
class FileCache;
class ResourceMetadata;
}  // namespace internal

namespace file_system {

class OperationObserver;

class DownloadOperation {
 public:
  DownloadOperation(base::SequencedTaskRunner* blocking_task_runner,
                    OperationObserver* observer,
                    JobScheduler* scheduler,
                    internal::ResourceMetadata* metadata,
                    internal::FileCache* cache,
                    const base::FilePath& temporary_file_directory);
  ~DownloadOperation();

  // Ensures that the file content specified by |resource_id| is locally
  // downloaded.
  // For hosted documents, this method may create a JSON file representing the
  // file.
  // For regular files, if the locally cached file is found, returns it.
  // If not found, start to download the file from the server.
  // When a JSON file is created, the cache file is found or downloading is
  // being started, |initialized_callback| is called with |local_file|
  // for JSON file or the cache file, or with |cancel_download_closure| for
  // downloading.
  // During the downloading |get_content_callback| will be called periodically
  // with the downloaded content.
  // Upon completion or an error is found, |completion_callback| will be called.
  // |initialized_callback| and |get_content_callback| can be null if not
  // needed.
  // |completion_callback| must not be null.
  void EnsureFileDownloadedByResourceId(
      const std::string& resource_id,
      const ClientContext& context,
      const GetFileContentInitializedCallback& initialized_callback,
      const google_apis::GetContentCallback& get_content_callback,
      const GetFileCallback& completion_callback);

  // Does the same thing as EnsureFileDownloadedByResourceId for the file
  // specified by |file_path|.
  void EnsureFileDownloadedByPath(
      const base::FilePath& file_path,
      const ClientContext& context,
      const GetFileContentInitializedCallback& initialized_callback,
      const google_apis::GetContentCallback& get_content_callback,
      const GetFileCallback& completion_callback);

 private:
  // Thin wrapper of Callbacks for EnsureFileDownloaded.
  class DownloadCallback;

  // Part of EnsureFileDownloaded(). Called upon the completion of precondition
  // check.
  void EnsureFileDownloadedAfterCheckPreCondition(
      const DownloadCallback& callback,
      const ClientContext& context,
      scoped_ptr<ResourceEntry> entry,
      base::FilePath* drive_file_path,
      base::FilePath* cache_file_path,
      FileError error);

  // Part of EnsureFileDownloaded(). Called when it is ready to start
  // downloading the file.
  void EnsureFileDownloadedAfterPrepareForDownloadFile(
      const DownloadCallback& callback,
      const ClientContext& context,
      scoped_ptr<ResourceEntry> entry,
      const base::FilePath& drive_file_path,
      base::FilePath* temp_download_file_path,
      FileError error);

  // Part of EnsureFileDownloaded(). Called after the actual downloading.
  void EnsureFileDownloadedAfterDownloadFile(
      const base::FilePath& drive_file_path,
      scoped_ptr<ResourceEntry> entry,
      const DownloadCallback& callback,
      google_apis::GDataErrorCode gdata_error,
      const base::FilePath& downloaded_file_path);

  // Part of EnsureFileDownloaded(). Called after updating local state is
  // completed.
  void EnsureFileDownloadedAfterUpdateLocalState(
      const base::FilePath& file_path,
      const DownloadCallback& callback,
      scoped_ptr<ResourceEntry> entry,
      base::FilePath* cache_file_path,
      FileError error);

  // Cancels the job with |job_id| in the scheduler.
  void CancelJob(JobID job_id);

  scoped_refptr<base::SequencedTaskRunner> blocking_task_runner_;
  OperationObserver* observer_;
  JobScheduler* scheduler_;
  internal::ResourceMetadata* metadata_;
  internal::FileCache* cache_;
  const base::FilePath temporary_file_directory_;

  // Note: This should remain the last member so it'll be destroyed and
  // invalidate its weak pointers before any other members are destroyed.
  base::WeakPtrFactory<DownloadOperation> weak_ptr_factory_;
  DISALLOW_COPY_AND_ASSIGN(DownloadOperation);
};

}  // namespace file_system
}  // namespace drive

#endif  // CHROME_BROWSER_CHROMEOS_DRIVE_FILE_SYSTEM_DOWNLOAD_OPERATION_H_
