// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/drive/file_system/open_file_operation.h"

#include "base/bind.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/message_loop/message_loop_proxy.h"
#include "base/task_runner_util.h"
#include "chrome/browser/chromeos/drive/drive.pb.h"
#include "chrome/browser/chromeos/drive/file_cache.h"
#include "chrome/browser/chromeos/drive/file_errors.h"
#include "chrome/browser/chromeos/drive/file_system/create_file_operation.h"
#include "chrome/browser/chromeos/drive/file_system/download_operation.h"
#include "chrome/browser/chromeos/drive/file_system/operation_observer.h"
#include "chrome/browser/chromeos/drive/file_system_interface.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

namespace drive {
namespace file_system {

OpenFileOperation::OpenFileOperation(
    base::SequencedTaskRunner* blocking_task_runner,
    OperationObserver* observer,
    JobScheduler* scheduler,
    internal::ResourceMetadata* metadata,
    internal::FileCache* cache,
    const base::FilePath& temporary_file_directory)
    : blocking_task_runner_(blocking_task_runner),
      observer_(observer),
      cache_(cache),
      create_file_operation_(new CreateFileOperation(
          blocking_task_runner, observer, scheduler, metadata, cache)),
      download_operation_(new DownloadOperation(
          blocking_task_runner, observer, scheduler,
          metadata, cache, temporary_file_directory)),
      weak_ptr_factory_(this) {
}

OpenFileOperation::~OpenFileOperation() {
}

void OpenFileOperation::OpenFile(const base::FilePath& file_path,
                                 OpenMode open_mode,
                                 const OpenFileCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  switch (open_mode) {
    case OPEN_FILE:
      // It is not necessary to create a new file even if not exists.
      // So call OpenFileAfterCreateFile directly with FILE_ERROR_OK
      // to skip file creation.
      OpenFileAfterCreateFile(file_path, callback, FILE_ERROR_OK);
      break;
    case CREATE_FILE:
      create_file_operation_->CreateFile(
          file_path,
          true /* exclusive */,
          base::Bind(&OpenFileOperation::OpenFileAfterCreateFile,
                     weak_ptr_factory_.GetWeakPtr(), file_path, callback));
      break;
    case OPEN_OR_CREATE_FILE:
      create_file_operation_->CreateFile(
          file_path,
          false /* not-exclusive */,
          base::Bind(&OpenFileOperation::OpenFileAfterCreateFile,
                     weak_ptr_factory_.GetWeakPtr(), file_path, callback));
      break;
  }
}

void OpenFileOperation::OpenFileAfterCreateFile(
    const base::FilePath& file_path,
    const OpenFileCallback& callback,
    FileError error) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  if (error != FILE_ERROR_OK) {
    callback.Run(error, base::FilePath(), base::Closure());
    return;
  }

  download_operation_->EnsureFileDownloadedByPath(
      file_path,
      ClientContext(USER_INITIATED),
      GetFileContentInitializedCallback(),
      google_apis::GetContentCallback(),
      base::Bind(
          &OpenFileOperation::OpenFileAfterFileDownloaded,
          weak_ptr_factory_.GetWeakPtr(), callback));
}

void OpenFileOperation::OpenFileAfterFileDownloaded(
    const OpenFileCallback& callback,
    FileError error,
    const base::FilePath& local_file_path,
    scoped_ptr<ResourceEntry> entry) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  if (error == FILE_ERROR_OK) {
    DCHECK(entry);
    DCHECK(entry->has_file_specific_info());
    if (entry->file_specific_info().is_hosted_document())
      // No support for opening a hosted document.
      error = FILE_ERROR_INVALID_OPERATION;
  }

  if (error != FILE_ERROR_OK) {
    callback.Run(error, base::FilePath(), base::Closure());
    return;
  }

  base::PostTaskAndReplyWithResult(
      blocking_task_runner_.get(),
      FROM_HERE,
      base::Bind(&internal::FileCache::MarkDirty,
                 base::Unretained(cache_),
                 entry->resource_id()),
      base::Bind(&OpenFileOperation::OpenFileAfterMarkDirty,
                 weak_ptr_factory_.GetWeakPtr(),
                 local_file_path,
                 entry->resource_id(),
                 callback));
}

void OpenFileOperation::OpenFileAfterMarkDirty(
    const base::FilePath& local_file_path,
    const std::string& resource_id,
    const OpenFileCallback& callback,
    FileError error) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  if (error != FILE_ERROR_OK) {
    callback.Run(error, base::FilePath(), base::Closure());
    return;
  }

  ++open_files_[resource_id];
  callback.Run(error, local_file_path,
               base::Bind(&OpenFileOperation::CloseFile,
                          weak_ptr_factory_.GetWeakPtr(), resource_id));
}

void OpenFileOperation::CloseFile(const std::string& resource_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK_GT(open_files_[resource_id], 0);

  if (--open_files_[resource_id] == 0) {
    // All clients closes this file, so notify to upload the file.
    open_files_.erase(resource_id);
    observer_->OnCacheFileUploadNeededByOperation(resource_id);
  }
}

}  // namespace file_system
}  // namespace drive
