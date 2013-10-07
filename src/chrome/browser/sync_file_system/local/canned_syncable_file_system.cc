// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync_file_system/local/canned_syncable_file_system.h"

#include <iterator>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/file_util.h"
#include "base/message_loop/message_loop_proxy.h"
#include "base/run_loop.h"
#include "base/single_thread_task_runner.h"
#include "base/task_runner_util.h"
#include "chrome/browser/sync_file_system/local/local_file_change_tracker.h"
#include "chrome/browser/sync_file_system/local/local_file_sync_context.h"
#include "chrome/browser/sync_file_system/local/sync_file_system_backend.h"
#include "chrome/browser/sync_file_system/syncable_file_system_util.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "webkit/browser/blob/mock_blob_url_request_context.h"
#include "webkit/browser/fileapi/external_mount_points.h"
#include "webkit/browser/fileapi/file_system_backend.h"
#include "webkit/browser/fileapi/file_system_context.h"
#include "webkit/browser/fileapi/file_system_operation_context.h"
#include "webkit/browser/fileapi/file_system_operation_runner.h"
#include "webkit/browser/fileapi/mock_file_system_options.h"
#include "webkit/browser/fileapi/sandbox_file_system_backend.h"
#include "webkit/browser/quota/mock_special_storage_policy.h"
#include "webkit/browser/quota/quota_manager.h"
#include "webkit/common/blob/shareable_file_reference.h"

using base::PlatformFileError;
using fileapi::FileSystemContext;
using fileapi::FileSystemOperationRunner;
using fileapi::FileSystemURL;
using fileapi::FileSystemURLSet;
using quota::QuotaManager;
using webkit_blob::MockBlobURLRequestContext;
using webkit_blob::ScopedTextBlob;

namespace sync_file_system {

namespace {

void Quit() { base::MessageLoop::current()->Quit(); }

template <typename R>
void AssignAndQuit(base::TaskRunner* original_task_runner,
                   R* result_out, R result) {
  DCHECK(result_out);
  *result_out = result;
  original_task_runner->PostTask(FROM_HERE, base::Bind(&Quit));
}

template <typename R>
R RunOnThread(
    base::SingleThreadTaskRunner* task_runner,
    const tracked_objects::Location& location,
    const base::Callback<void(const base::Callback<void(R)>& callback)>& task) {
  R result;
  task_runner->PostTask(
      location,
      base::Bind(task, base::Bind(&AssignAndQuit<R>,
                                  base::MessageLoopProxy::current(),
                                  &result)));
  base::MessageLoop::current()->Run();
  return result;
}

void RunOnThread(base::SingleThreadTaskRunner* task_runner,
                 const tracked_objects::Location& location,
                 const base::Closure& task) {
  task_runner->PostTaskAndReply(
      location, task,
      base::Bind(base::IgnoreResult(
          base::Bind(&base::MessageLoopProxy::PostTask,
                     base::MessageLoopProxy::current(),
                     FROM_HERE, base::Bind(&Quit)))));
  base::MessageLoop::current()->Run();
}

void EnsureRunningOn(base::SingleThreadTaskRunner* runner) {
  EXPECT_TRUE(runner->RunsTasksOnCurrentThread());
}

void VerifySameTaskRunner(
    base::SingleThreadTaskRunner* runner1,
    base::SingleThreadTaskRunner* runner2) {
  ASSERT_TRUE(runner1 != NULL);
  ASSERT_TRUE(runner2 != NULL);
  runner1->PostTask(FROM_HERE,
                    base::Bind(&EnsureRunningOn, make_scoped_refptr(runner2)));
}

void OnCreateSnapshotFileAndVerifyData(
    const std::string& expected_data,
    const CannedSyncableFileSystem::StatusCallback& callback,
    base::PlatformFileError result,
    const base::PlatformFileInfo& file_info,
    const base::FilePath& platform_path,
    const scoped_refptr<webkit_blob::ShareableFileReference>& /* file_ref */) {
  if (result != base::PLATFORM_FILE_OK) {
    callback.Run(result);
    return;
  }
  EXPECT_EQ(expected_data.size(), static_cast<size_t>(file_info.size));
  std::string data;
  const bool read_status = file_util::ReadFileToString(platform_path, &data);
  EXPECT_TRUE(read_status);
  EXPECT_EQ(expected_data, data);
  callback.Run(result);
}

void OnCreateSnapshotFile(
    base::PlatformFileInfo* file_info_out,
    base::FilePath* platform_path_out,
    const CannedSyncableFileSystem::StatusCallback& callback,
    base::PlatformFileError result,
    const base::PlatformFileInfo& file_info,
    const base::FilePath& platform_path,
    const scoped_refptr<webkit_blob::ShareableFileReference>& file_ref) {
  DCHECK(!file_ref.get());
  DCHECK(file_info_out);
  DCHECK(platform_path_out);
  *file_info_out = file_info;
  *platform_path_out = platform_path;
  callback.Run(result);
}

void OnReadDirectory(
    CannedSyncableFileSystem::FileEntryList* entries_out,
    const CannedSyncableFileSystem::StatusCallback& callback,
    base::PlatformFileError error,
    const fileapi::FileSystemOperation::FileEntryList& entries,
    bool has_more) {
  DCHECK(entries_out);
  entries_out->reserve(entries_out->size() + entries.size());
  std::copy(entries.begin(), entries.end(), std::back_inserter(*entries_out));

  if (!has_more)
    callback.Run(error);
}

class WriteHelper {
 public:
  WriteHelper() : bytes_written_(0) {}
  WriteHelper(MockBlobURLRequestContext* request_context,
              const GURL& blob_url,
              const std::string& blob_data)
      : bytes_written_(0),
        request_context_(request_context),
        blob_data_(new ScopedTextBlob(*request_context, blob_url, blob_data)) {}

  ~WriteHelper() {
    if (request_context_) {
      base::MessageLoop::current()->DeleteSoon(FROM_HERE,
                                               request_context_.release());
    }
  }

  void DidWrite(const base::Callback<void(int64 result)>& completion_callback,
                PlatformFileError error, int64 bytes, bool complete) {
    if (error == base::PLATFORM_FILE_OK) {
      bytes_written_ += bytes;
      if (!complete)
        return;
    }
    completion_callback.Run(error == base::PLATFORM_FILE_OK
                            ? bytes_written_ : static_cast<int64>(error));
  }

 private:
  int64 bytes_written_;
  scoped_ptr<MockBlobURLRequestContext> request_context_;
  scoped_ptr<ScopedTextBlob> blob_data_;

  DISALLOW_COPY_AND_ASSIGN(WriteHelper);
};

void DidGetUsageAndQuota(const quota::StatusCallback& callback,
                         int64* usage_out, int64* quota_out,
                         quota::QuotaStatusCode status,
                         int64 usage, int64 quota) {
  *usage_out = usage;
  *quota_out = quota;
  callback.Run(status);
}

void EnsureLastTaskRuns(base::SingleThreadTaskRunner* runner) {
  base::RunLoop run_loop;
  runner->PostTaskAndReply(
      FROM_HERE, base::Bind(&base::DoNothing), run_loop.QuitClosure());
  run_loop.Run();
}

}  // namespace

CannedSyncableFileSystem::CannedSyncableFileSystem(
    const GURL& origin,
    base::SingleThreadTaskRunner* io_task_runner,
    base::SingleThreadTaskRunner* file_task_runner)
    : origin_(origin),
      type_(fileapi::kFileSystemTypeSyncable),
      result_(base::PLATFORM_FILE_OK),
      sync_status_(sync_file_system::SYNC_STATUS_OK),
      io_task_runner_(io_task_runner),
      file_task_runner_(file_task_runner),
      is_filesystem_set_up_(false),
      is_filesystem_opened_(false),
      sync_status_observers_(new ObserverList) {
}

CannedSyncableFileSystem::~CannedSyncableFileSystem() {}

void CannedSyncableFileSystem::SetUp() {
  ASSERT_FALSE(is_filesystem_set_up_);
  ASSERT_TRUE(data_dir_.CreateUniqueTempDir());

  scoped_refptr<quota::SpecialStoragePolicy> storage_policy =
      new quota::MockSpecialStoragePolicy();

  quota_manager_ = new QuotaManager(false /* is_incognito */,
                                    data_dir_.path(),
                                    io_task_runner_.get(),
                                    base::MessageLoopProxy::current().get(),
                                    storage_policy.get());

  std::vector<std::string> additional_allowed_schemes;
  additional_allowed_schemes.push_back(origin_.scheme());
  fileapi::FileSystemOptions options(
      fileapi::FileSystemOptions::PROFILE_MODE_NORMAL,
      additional_allowed_schemes);

  ScopedVector<fileapi::FileSystemBackend> additional_backends;
  additional_backends.push_back(new SyncFileSystemBackend());

  file_system_context_ = new FileSystemContext(
      io_task_runner_.get(),
      file_task_runner_.get(),
      fileapi::ExternalMountPoints::CreateRefCounted().get(),
      storage_policy.get(),
      quota_manager_->proxy(),
      additional_backends.Pass(),
      data_dir_.path(), options);

  is_filesystem_set_up_ = true;
}

void CannedSyncableFileSystem::TearDown() {
  quota_manager_ = NULL;
  file_system_context_ = NULL;

  // Make sure we give some more time to finish tasks on other threads.
  EnsureLastTaskRuns(io_task_runner_.get());
  EnsureLastTaskRuns(file_task_runner_.get());
}

FileSystemURL CannedSyncableFileSystem::URL(const std::string& path) const {
  EXPECT_TRUE(is_filesystem_set_up_);
  EXPECT_TRUE(is_filesystem_opened_);

  GURL url(root_url_.spec() + path);
  return file_system_context_->CrackURL(url);
}

PlatformFileError CannedSyncableFileSystem::OpenFileSystem() {
  EXPECT_TRUE(is_filesystem_set_up_);
  EXPECT_FALSE(is_filesystem_opened_);
  file_system_context_->OpenFileSystem(
      origin_, type_,
      fileapi::OPEN_FILE_SYSTEM_CREATE_IF_NONEXISTENT,
      base::Bind(&CannedSyncableFileSystem::DidOpenFileSystem,
                 base::Unretained(this)));
  base::MessageLoop::current()->Run();
  if (backend()->sync_context()) {
    // Register 'this' as a sync status observer.
    RunOnThread(
        io_task_runner_.get(),
        FROM_HERE,
        base::Bind(&CannedSyncableFileSystem::InitializeSyncStatusObserver,
                   base::Unretained(this)));
  }
  return result_;
}

void CannedSyncableFileSystem::AddSyncStatusObserver(
    LocalFileSyncStatus::Observer* observer) {
  sync_status_observers_->AddObserver(observer);
}

void CannedSyncableFileSystem::RemoveSyncStatusObserver(
    LocalFileSyncStatus::Observer* observer) {
  sync_status_observers_->RemoveObserver(observer);
}

SyncStatusCode CannedSyncableFileSystem::MaybeInitializeFileSystemContext(
    LocalFileSyncContext* sync_context) {
  DCHECK(sync_context);
  sync_status_ = sync_file_system::SYNC_STATUS_UNKNOWN;
  VerifySameTaskRunner(io_task_runner_.get(),
                       sync_context->io_task_runner_.get());
  sync_context->MaybeInitializeFileSystemContext(
      origin_,
      file_system_context_.get(),
      base::Bind(&CannedSyncableFileSystem::DidInitializeFileSystemContext,
                 base::Unretained(this)));
  base::MessageLoop::current()->Run();
  return sync_status_;
}

PlatformFileError CannedSyncableFileSystem::CreateDirectory(
    const FileSystemURL& url) {
  return RunOnThread<PlatformFileError>(
      io_task_runner_.get(),
      FROM_HERE,
      base::Bind(&CannedSyncableFileSystem::DoCreateDirectory,
                 base::Unretained(this),
                 url));
}

PlatformFileError CannedSyncableFileSystem::CreateFile(
    const FileSystemURL& url) {
  return RunOnThread<PlatformFileError>(
      io_task_runner_.get(),
      FROM_HERE,
      base::Bind(&CannedSyncableFileSystem::DoCreateFile,
                 base::Unretained(this),
                 url));
}

PlatformFileError CannedSyncableFileSystem::Copy(
    const FileSystemURL& src_url, const FileSystemURL& dest_url) {
  return RunOnThread<PlatformFileError>(
      io_task_runner_.get(),
      FROM_HERE,
      base::Bind(&CannedSyncableFileSystem::DoCopy,
                 base::Unretained(this),
                 src_url,
                 dest_url));
}

PlatformFileError CannedSyncableFileSystem::Move(
    const FileSystemURL& src_url, const FileSystemURL& dest_url) {
  return RunOnThread<PlatformFileError>(
      io_task_runner_.get(),
      FROM_HERE,
      base::Bind(&CannedSyncableFileSystem::DoMove,
                 base::Unretained(this),
                 src_url,
                 dest_url));
}

PlatformFileError CannedSyncableFileSystem::TruncateFile(
    const FileSystemURL& url, int64 size) {
  return RunOnThread<PlatformFileError>(
      io_task_runner_.get(),
      FROM_HERE,
      base::Bind(&CannedSyncableFileSystem::DoTruncateFile,
                 base::Unretained(this),
                 url,
                 size));
}

PlatformFileError CannedSyncableFileSystem::TouchFile(
    const FileSystemURL& url,
    const base::Time& last_access_time,
    const base::Time& last_modified_time) {
  return RunOnThread<PlatformFileError>(
      io_task_runner_.get(),
      FROM_HERE,
      base::Bind(&CannedSyncableFileSystem::DoTouchFile,
                 base::Unretained(this),
                 url,
                 last_access_time,
                 last_modified_time));
}

PlatformFileError CannedSyncableFileSystem::Remove(
    const FileSystemURL& url, bool recursive) {
  return RunOnThread<PlatformFileError>(
      io_task_runner_.get(),
      FROM_HERE,
      base::Bind(&CannedSyncableFileSystem::DoRemove,
                 base::Unretained(this),
                 url,
                 recursive));
}

PlatformFileError CannedSyncableFileSystem::FileExists(
    const FileSystemURL& url) {
  return RunOnThread<PlatformFileError>(
      io_task_runner_.get(),
      FROM_HERE,
      base::Bind(&CannedSyncableFileSystem::DoFileExists,
                 base::Unretained(this),
                 url));
}

PlatformFileError CannedSyncableFileSystem::DirectoryExists(
    const FileSystemURL& url) {
  return RunOnThread<PlatformFileError>(
      io_task_runner_.get(),
      FROM_HERE,
      base::Bind(&CannedSyncableFileSystem::DoDirectoryExists,
                 base::Unretained(this),
                 url));
}

PlatformFileError CannedSyncableFileSystem::VerifyFile(
    const FileSystemURL& url,
    const std::string& expected_data) {
  return RunOnThread<PlatformFileError>(
      io_task_runner_.get(),
      FROM_HERE,
      base::Bind(&CannedSyncableFileSystem::DoVerifyFile,
                 base::Unretained(this),
                 url,
                 expected_data));
}

PlatformFileError CannedSyncableFileSystem::GetMetadataAndPlatformPath(
    const FileSystemURL& url,
    base::PlatformFileInfo* info,
    base::FilePath* platform_path) {
  return RunOnThread<PlatformFileError>(
      io_task_runner_.get(),
      FROM_HERE,
      base::Bind(&CannedSyncableFileSystem::DoGetMetadataAndPlatformPath,
                 base::Unretained(this),
                 url,
                 info,
                 platform_path));
}

PlatformFileError CannedSyncableFileSystem::ReadDirectory(
    const fileapi::FileSystemURL& url,
    FileEntryList* entries) {
  return RunOnThread<PlatformFileError>(
      io_task_runner_.get(),
      FROM_HERE,
      base::Bind(&CannedSyncableFileSystem::DoReadDirectory,
          base::Unretained(this),
          url,
          entries));
}

int64 CannedSyncableFileSystem::Write(
    net::URLRequestContext* url_request_context,
    const FileSystemURL& url, const GURL& blob_url) {
  return RunOnThread<int64>(io_task_runner_.get(),
                            FROM_HERE,
                            base::Bind(&CannedSyncableFileSystem::DoWrite,
                                       base::Unretained(this),
                                       url_request_context,
                                       url,
                                       blob_url));
}

int64 CannedSyncableFileSystem::WriteString(
    const FileSystemURL& url, const std::string& data) {
  return RunOnThread<int64>(io_task_runner_.get(),
                            FROM_HERE,
                            base::Bind(&CannedSyncableFileSystem::DoWriteString,
                                       base::Unretained(this),
                                       url,
                                       data));
}

PlatformFileError CannedSyncableFileSystem::DeleteFileSystem() {
  EXPECT_TRUE(is_filesystem_set_up_);
  return RunOnThread<PlatformFileError>(
      io_task_runner_.get(),
      FROM_HERE,
      base::Bind(&FileSystemContext::DeleteFileSystem,
                 file_system_context_,
                 origin_,
                 type_));
}

quota::QuotaStatusCode CannedSyncableFileSystem::GetUsageAndQuota(
    int64* usage, int64* quota) {
  return RunOnThread<quota::QuotaStatusCode>(
      io_task_runner_.get(),
      FROM_HERE,
      base::Bind(&CannedSyncableFileSystem::DoGetUsageAndQuota,
                 base::Unretained(this),
                 usage,
                 quota));
}

void CannedSyncableFileSystem::GetChangedURLsInTracker(
    FileSystemURLSet* urls) {
  return RunOnThread(
      file_task_runner_.get(),
      FROM_HERE,
      base::Bind(&LocalFileChangeTracker::GetAllChangedURLs,
                 base::Unretained(backend()->change_tracker()),
                 urls));
}

void CannedSyncableFileSystem::ClearChangeForURLInTracker(
    const FileSystemURL& url) {
  return RunOnThread(
      file_task_runner_.get(),
      FROM_HERE,
      base::Bind(&LocalFileChangeTracker::ClearChangesForURL,
                 base::Unretained(backend()->change_tracker()),
                 url));
}

SyncFileSystemBackend* CannedSyncableFileSystem::backend() {
  return SyncFileSystemBackend::GetBackend(file_system_context_);
}

FileSystemOperationRunner* CannedSyncableFileSystem::operation_runner() {
  return file_system_context_->operation_runner();
}

void CannedSyncableFileSystem::OnSyncEnabled(const FileSystemURL& url) {
  sync_status_observers_->Notify(&LocalFileSyncStatus::Observer::OnSyncEnabled,
                                 url);
}

void CannedSyncableFileSystem::OnWriteEnabled(const FileSystemURL& url) {
  sync_status_observers_->Notify(&LocalFileSyncStatus::Observer::OnWriteEnabled,
                                 url);
}

void CannedSyncableFileSystem::DoCreateDirectory(
    const FileSystemURL& url,
    const StatusCallback& callback) {
  EXPECT_TRUE(is_filesystem_opened_);
  operation_runner()->CreateDirectory(
      url, false /* exclusive */, false /* recursive */, callback);
}

void CannedSyncableFileSystem::DoCreateFile(
    const FileSystemURL& url,
    const StatusCallback& callback) {
  EXPECT_TRUE(is_filesystem_opened_);
  operation_runner()->CreateFile(url, false /* exclusive */, callback);
}

void CannedSyncableFileSystem::DoCopy(
    const FileSystemURL& src_url,
    const FileSystemURL& dest_url,
    const StatusCallback& callback) {
  EXPECT_TRUE(is_filesystem_opened_);
  operation_runner()->Copy(src_url, dest_url, callback);
}

void CannedSyncableFileSystem::DoMove(
    const FileSystemURL& src_url,
    const FileSystemURL& dest_url,
    const StatusCallback& callback) {
  EXPECT_TRUE(is_filesystem_opened_);
  operation_runner()->Move(src_url, dest_url, callback);
}

void CannedSyncableFileSystem::DoTruncateFile(
    const FileSystemURL& url, int64 size,
    const StatusCallback& callback) {
  EXPECT_TRUE(is_filesystem_opened_);
  operation_runner()->Truncate(url, size, callback);
}

void CannedSyncableFileSystem::DoTouchFile(
    const FileSystemURL& url,
    const base::Time& last_access_time,
    const base::Time& last_modified_time,
    const StatusCallback& callback) {
  EXPECT_TRUE(is_filesystem_opened_);
  operation_runner()->TouchFile(url, last_access_time,
                                last_modified_time, callback);
}

void CannedSyncableFileSystem::DoRemove(
    const FileSystemURL& url, bool recursive,
    const StatusCallback& callback) {
  EXPECT_TRUE(is_filesystem_opened_);
  operation_runner()->Remove(url, recursive, callback);
}

void CannedSyncableFileSystem::DoFileExists(
    const FileSystemURL& url, const StatusCallback& callback) {
  EXPECT_TRUE(is_filesystem_opened_);
  operation_runner()->FileExists(url, callback);
}

void CannedSyncableFileSystem::DoDirectoryExists(
    const FileSystemURL& url, const StatusCallback& callback) {
  EXPECT_TRUE(is_filesystem_opened_);
  operation_runner()->DirectoryExists(url, callback);
}

void CannedSyncableFileSystem::DoVerifyFile(
    const FileSystemURL& url,
    const std::string& expected_data,
    const StatusCallback& callback) {
  EXPECT_TRUE(is_filesystem_opened_);
  operation_runner()->CreateSnapshotFile(
      url,
      base::Bind(&OnCreateSnapshotFileAndVerifyData,expected_data, callback));
}

void CannedSyncableFileSystem::DoGetMetadataAndPlatformPath(
    const FileSystemURL& url,
    base::PlatformFileInfo* info,
    base::FilePath* platform_path,
    const StatusCallback& callback) {
  EXPECT_TRUE(is_filesystem_opened_);
  operation_runner()->CreateSnapshotFile(
      url, base::Bind(&OnCreateSnapshotFile, info, platform_path, callback));
}

void CannedSyncableFileSystem::DoReadDirectory(
    const FileSystemURL& url,
    FileEntryList* entries,
    const StatusCallback& callback) {
  EXPECT_TRUE(is_filesystem_opened_);
  operation_runner()->ReadDirectory(
      url, base::Bind(&OnReadDirectory, entries, callback));
}

void CannedSyncableFileSystem::DoWrite(
    net::URLRequestContext* url_request_context,
    const FileSystemURL& url, const GURL& blob_url,
    const WriteCallback& callback) {
  EXPECT_TRUE(is_filesystem_opened_);
  WriteHelper* helper = new WriteHelper;
  operation_runner()->Write(url_request_context, url, blob_url, 0,
                            base::Bind(&WriteHelper::DidWrite,
                                       base::Owned(helper), callback));
}

void CannedSyncableFileSystem::DoWriteString(
    const FileSystemURL& url,
    const std::string& data,
    const WriteCallback& callback) {
  MockBlobURLRequestContext* url_request_context(
      new MockBlobURLRequestContext(file_system_context_.get()));
  const GURL blob_url(std::string("blob:") + data);
  WriteHelper* helper = new WriteHelper(url_request_context, blob_url, data);
  operation_runner()->Write(url_request_context, url, blob_url, 0,
                            base::Bind(&WriteHelper::DidWrite,
                                       base::Owned(helper), callback));
}

void CannedSyncableFileSystem::DoGetUsageAndQuota(
    int64* usage,
    int64* quota,
    const quota::StatusCallback& callback) {
  quota_manager_->GetUsageAndQuota(
      origin_, storage_type(),
      base::Bind(&DidGetUsageAndQuota, callback, usage, quota));
}

void CannedSyncableFileSystem::DidOpenFileSystem(
    PlatformFileError result, const std::string& name, const GURL& root) {
  result_ = result;
  root_url_ = root;
  is_filesystem_opened_ = true;
  base::MessageLoop::current()->Quit();
}

void CannedSyncableFileSystem::DidInitializeFileSystemContext(
    SyncStatusCode status) {
  sync_status_ = status;
  base::MessageLoop::current()->Quit();
}

void CannedSyncableFileSystem::InitializeSyncStatusObserver() {
  ASSERT_TRUE(io_task_runner_->RunsTasksOnCurrentThread());
  backend()->sync_context()->sync_status()->AddObserver(this);
}

}  // namespace sync_file_system
