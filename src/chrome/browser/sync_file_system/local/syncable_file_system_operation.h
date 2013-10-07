// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_FILE_SYSTEM_LOCAL_SYNCABLE_FILE_SYSTEM_OPERATION_H_
#define CHROME_BROWSER_SYNC_FILE_SYSTEM_LOCAL_SYNCABLE_FILE_SYSTEM_OPERATION_H_

#include <vector>

#include "base/callback.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/non_thread_safe.h"
#include "webkit/browser/fileapi/file_system_operation_impl.h"

namespace fileapi {
class FileSystemContext;
class FileSystemOperationContext;
class SandboxFileSystemBackend;
}

namespace sync_file_system {

class SyncableFileOperationRunner;

// A wrapper class of FileSystemOperationImpl for syncable file system.
class SyncableFileSystemOperation
    : public fileapi::FileSystemOperationImpl,
      public base::SupportsWeakPtr<SyncableFileSystemOperation>,
      public base::NonThreadSafe {
 public:
  virtual ~SyncableFileSystemOperation();

  // fileapi::FileSystemOperation overrides.
  virtual void CreateFile(const fileapi::FileSystemURL& url,
                          bool exclusive,
                          const StatusCallback& callback) OVERRIDE;
  virtual void CreateDirectory(const fileapi::FileSystemURL& url,
                               bool exclusive,
                               bool recursive,
                               const StatusCallback& callback) OVERRIDE;
  virtual void Copy(const fileapi::FileSystemURL& src_url,
                    const fileapi::FileSystemURL& dest_url,
                    const StatusCallback& callback) OVERRIDE;
  virtual void Move(const fileapi::FileSystemURL& src_url,
                    const fileapi::FileSystemURL& dest_url,
                    const StatusCallback& callback) OVERRIDE;
  virtual void DirectoryExists(const fileapi::FileSystemURL& url,
                               const StatusCallback& callback) OVERRIDE;
  virtual void FileExists(const fileapi::FileSystemURL& url,
                          const StatusCallback& callback) OVERRIDE;
  virtual void GetMetadata(const fileapi::FileSystemURL& url,
                           const GetMetadataCallback& callback) OVERRIDE;
  virtual void ReadDirectory(const fileapi::FileSystemURL& url,
                             const ReadDirectoryCallback& callback) OVERRIDE;
  virtual void Remove(const fileapi::FileSystemURL& url, bool recursive,
                      const StatusCallback& callback) OVERRIDE;
  virtual void Write(const fileapi::FileSystemURL& url,
                     scoped_ptr<fileapi::FileWriterDelegate> writer_delegate,
                     scoped_ptr<net::URLRequest> blob_request,
                     const WriteCallback& callback) OVERRIDE;
  virtual void Truncate(const fileapi::FileSystemURL& url, int64 length,
                        const StatusCallback& callback) OVERRIDE;
  virtual void TouchFile(const fileapi::FileSystemURL& url,
                         const base::Time& last_access_time,
                         const base::Time& last_modified_time,
                         const StatusCallback& callback) OVERRIDE;
  virtual void OpenFile(const fileapi::FileSystemURL& url,
                        int file_flags,
                        base::ProcessHandle peer_handle,
                        const OpenFileCallback& callback) OVERRIDE;
  virtual void Cancel(const StatusCallback& cancel_callback) OVERRIDE;
  virtual void CreateSnapshotFile(
      const fileapi::FileSystemURL& path,
      const SnapshotFileCallback& callback) OVERRIDE;

  // FileSystemOperationImpl overrides.
  virtual void CopyInForeignFile(const base::FilePath& src_local_disk_path,
                                 const fileapi::FileSystemURL& dest_url,
                                 const StatusCallback& callback) OVERRIDE;

  using base::SupportsWeakPtr<SyncableFileSystemOperation>::AsWeakPtr;

 private:
  typedef SyncableFileSystemOperation self;
  class QueueableTask;

  // Only SyncFileSystemBackend can create a new operation directly.
  friend class SyncFileSystemBackend;

  SyncableFileSystemOperation(
      const fileapi::FileSystemURL& url,
      fileapi::FileSystemContext* file_system_context,
      scoped_ptr<fileapi::FileSystemOperationContext> operation_context);
  fileapi::FileSystemOperationImpl* NewOperation();

  void DidFinish(base::PlatformFileError status);
  void DidWrite(const WriteCallback& callback,
                base::PlatformFileError result,
                int64 bytes,
                bool complete);

  void OnCancelled();

  const fileapi::FileSystemURL url_;

  base::WeakPtr<SyncableFileOperationRunner> operation_runner_;
  scoped_ptr<fileapi::FileSystemOperationImpl> inflight_operation_;
  std::vector<fileapi::FileSystemURL> target_paths_;

  StatusCallback completion_callback_;

  bool is_directory_operation_enabled_;

  DISALLOW_COPY_AND_ASSIGN(SyncableFileSystemOperation);
};

}  // namespace sync_file_system

#endif  // CHROME_BROWSER_SYNC_FILE_SYSTEM_LOCAL_SYNCABLE_FILE_SYSTEM_OPERATION_H_
