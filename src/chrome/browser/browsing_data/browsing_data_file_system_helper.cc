// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/browsing_data/browsing_data_file_system_helper.h"

#include "base/bind.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/sequenced_task_runner.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/browsing_data/browsing_data_helper.h"
#include "content/public/browser/browser_thread.h"
#include "webkit/browser/fileapi/file_system_context.h"
#include "webkit/browser/fileapi/file_system_quota_util.h"
#include "webkit/browser/fileapi/sandbox_file_system_backend.h"
#include "webkit/common/fileapi/file_system_types.h"

using content::BrowserThread;

namespace fileapi {
class FileSystemContext;
}

namespace {

// An implementation of the BrowsingDataFileSystemHelper interface that pulls
// data from a given |filesystem_context| and returns a list of FileSystemInfo
// items to a client.
class BrowsingDataFileSystemHelperImpl : public BrowsingDataFileSystemHelper {
 public:
  // BrowsingDataFileSystemHelper implementation
  explicit BrowsingDataFileSystemHelperImpl(
      fileapi::FileSystemContext* filesystem_context);
  virtual void StartFetching(const base::Callback<
      void(const std::list<FileSystemInfo>&)>& callback) OVERRIDE;
  virtual void DeleteFileSystemOrigin(const GURL& origin) OVERRIDE;

 private:
  virtual ~BrowsingDataFileSystemHelperImpl();

  // Enumerates all filesystem files, storing the resulting list into
  // file_system_file_ for later use. This must be called on the file
  // task runner.
  void FetchFileSystemInfoInFileThread();

  // Triggers the success callback as the end of a StartFetching workflow. This
  // must be called on the UI thread.
  void NotifyOnUIThread();

  // Deletes all file systems associated with |origin|. This must be called on
  // the file task runner.
  void DeleteFileSystemOriginInFileThread(const GURL& origin);

  // Returns the file task runner for the |filesystem_context_|.
  base::SequencedTaskRunner* file_task_runner() {
    return filesystem_context_->default_file_task_runner();
  }

  // Keep a reference to the FileSystemContext object for the current profile
  // for use on the file task runner.
  scoped_refptr<fileapi::FileSystemContext> filesystem_context_;

  // Holds the current list of file systems returned to the client after
  // StartFetching is called. Access to |file_system_info_| is triggered
  // indirectly via the UI thread and guarded by |is_fetching_|. This means
  // |file_system_info_| is only accessed while |is_fetching_| is true. The
  // flag |is_fetching_| is only accessed on the UI thread. In the context of
  // this class |file_system_info_| only mutates on the file task runner.
  std::list<FileSystemInfo> file_system_info_;

  // Holds the callback passed in at the beginning of the StartFetching workflow
  // so that it can be triggered via NotifyOnUIThread. This only mutates on the
  // UI thread.
  base::Callback<void(const std::list<FileSystemInfo>&)> completion_callback_;

  // Indicates whether or not we're currently fetching information: set to true
  // when StartFetching is called on the UI thread, and reset to false when
  // NotifyOnUIThread triggers the success callback.
  // This property only mutates on the UI thread.
  bool is_fetching_;

  DISALLOW_COPY_AND_ASSIGN(BrowsingDataFileSystemHelperImpl);
};

BrowsingDataFileSystemHelperImpl::BrowsingDataFileSystemHelperImpl(
    fileapi::FileSystemContext* filesystem_context)
    : filesystem_context_(filesystem_context),
      is_fetching_(false) {
  DCHECK(filesystem_context_.get());
}

BrowsingDataFileSystemHelperImpl::~BrowsingDataFileSystemHelperImpl() {
}

void BrowsingDataFileSystemHelperImpl::StartFetching(
    const base::Callback<void(const std::list<FileSystemInfo>&)>& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!is_fetching_);
  DCHECK_EQ(false, callback.is_null());
  is_fetching_ = true;
  completion_callback_ = callback;
  file_task_runner()->PostTask(
      FROM_HERE,
      base::Bind(
          &BrowsingDataFileSystemHelperImpl::FetchFileSystemInfoInFileThread,
          this));
}

void BrowsingDataFileSystemHelperImpl::DeleteFileSystemOrigin(
    const GURL& origin) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  file_task_runner()->PostTask(
      FROM_HERE,
      base::Bind(
          &BrowsingDataFileSystemHelperImpl::DeleteFileSystemOriginInFileThread,
          this, origin));
}

void BrowsingDataFileSystemHelperImpl::FetchFileSystemInfoInFileThread() {
  DCHECK(file_task_runner()->RunsTasksOnCurrentThread());

  // We check usage for these filesystem types.
  const fileapi::FileSystemType types[] = {
    fileapi::kFileSystemTypeTemporary,
    fileapi::kFileSystemTypePersistent,
    fileapi::kFileSystemTypeSyncable,
  };

  typedef std::map<GURL, FileSystemInfo> OriginInfoMap;
  OriginInfoMap file_system_info_map;
  for (size_t i = 0; i < arraysize(types); ++i) {
    fileapi::FileSystemType type = types[i];
    fileapi::FileSystemQuotaUtil* quota_util =
      filesystem_context_->GetQuotaUtil(type);
    DCHECK(quota_util);
    std::set<GURL> origins;
    quota_util->GetOriginsForTypeOnFileThread(type, &origins);
    for (std::set<GURL>::iterator iter = origins.begin();
        iter != origins.end(); ++iter) {
      const GURL& current = *iter;
      if (!BrowsingDataHelper::HasWebScheme(current))
        continue;  // Non-websafe state is not considered browsing data.
      int64 usage = quota_util->GetOriginUsageOnFileThread(
          filesystem_context_.get(), current, type);
      OriginInfoMap::iterator inserted =
          file_system_info_map.insert(
              std::make_pair(current, FileSystemInfo(current))).first;
      inserted->second.usage_map[type] = usage;
    }
  }

  for (OriginInfoMap::iterator iter = file_system_info_map.begin();
       iter != file_system_info_map.end(); ++iter) {
    file_system_info_.push_back(iter->second);
  }

  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&BrowsingDataFileSystemHelperImpl::NotifyOnUIThread, this));
}

void BrowsingDataFileSystemHelperImpl::NotifyOnUIThread() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(is_fetching_);
  completion_callback_.Run(file_system_info_);
  completion_callback_.Reset();
  is_fetching_ = false;
}

void BrowsingDataFileSystemHelperImpl::DeleteFileSystemOriginInFileThread(
    const GURL& origin) {
  DCHECK(file_task_runner()->RunsTasksOnCurrentThread());
  filesystem_context_->DeleteDataForOriginOnFileThread(origin);
}

}  // namespace

BrowsingDataFileSystemHelper::FileSystemInfo::FileSystemInfo(
    const GURL& origin) : origin(origin) {}

BrowsingDataFileSystemHelper::FileSystemInfo::~FileSystemInfo() {}

// static
BrowsingDataFileSystemHelper* BrowsingDataFileSystemHelper::Create(
    fileapi::FileSystemContext* filesystem_context) {
  return new BrowsingDataFileSystemHelperImpl(filesystem_context);
}

CannedBrowsingDataFileSystemHelper::CannedBrowsingDataFileSystemHelper(
    Profile* profile)
    : is_fetching_(false) {
}

CannedBrowsingDataFileSystemHelper::CannedBrowsingDataFileSystemHelper()
    : is_fetching_(false) {
}

CannedBrowsingDataFileSystemHelper::~CannedBrowsingDataFileSystemHelper() {}

CannedBrowsingDataFileSystemHelper*
    CannedBrowsingDataFileSystemHelper::Clone() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  CannedBrowsingDataFileSystemHelper* clone =
      new CannedBrowsingDataFileSystemHelper();
  // This list only mutates on the UI thread, so it's safe to work with it here
  // (given the DCHECK above).
  clone->file_system_info_ = file_system_info_;
  return clone;
}

void CannedBrowsingDataFileSystemHelper::AddFileSystem(
    const GURL& origin, const fileapi::FileSystemType type, const int64 size) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  // This canned implementation of AddFileSystem uses an O(n^2) algorithm; which
  // is fine, as it isn't meant for use in a high-volume context. If it turns
  // out that we want to start using this in a context with many, many origins,
  // we should think about reworking the implementation.
  bool duplicate_origin = false;
  for (std::list<FileSystemInfo>::iterator
           file_system = file_system_info_.begin();
       file_system != file_system_info_.end();
       ++file_system) {
    if (file_system->origin == origin) {
      file_system->usage_map[type] = size;
      duplicate_origin = true;
      break;
    }
  }
  if (duplicate_origin)
    return;

  if (!BrowsingDataHelper::HasWebScheme(origin))
    return;  // Non-websafe state is not considered browsing data.

  FileSystemInfo info(origin);
  info.usage_map[type] = size;
  file_system_info_.push_back(info);
}

void CannedBrowsingDataFileSystemHelper::Reset() {
  file_system_info_.clear();
}

bool CannedBrowsingDataFileSystemHelper::empty() const {
  return file_system_info_.empty();
}

size_t CannedBrowsingDataFileSystemHelper::GetFileSystemCount() const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  return file_system_info_.size();
}

void CannedBrowsingDataFileSystemHelper::StartFetching(
    const base::Callback<void(const std::list<FileSystemInfo>&)>& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!is_fetching_);
  DCHECK_EQ(false, callback.is_null());
  is_fetching_ = true;
  completion_callback_ = callback;

  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&CannedBrowsingDataFileSystemHelper::NotifyOnUIThread, this));
}

void CannedBrowsingDataFileSystemHelper::NotifyOnUIThread() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(is_fetching_);
  completion_callback_.Run(file_system_info_);
  completion_callback_.Reset();
  is_fetching_ = false;
}
