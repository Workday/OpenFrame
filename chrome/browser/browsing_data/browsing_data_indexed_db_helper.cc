// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/browsing_data/browsing_data_indexed_db_helper.h"

#include <vector>

#include "base/bind.h"
#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/browsing_data/browsing_data_helper.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/indexed_db_context.h"

using content::BrowserThread;
using content::IndexedDBContext;
using content::IndexedDBInfo;

namespace {

class BrowsingDataIndexedDBHelperImpl : public BrowsingDataIndexedDBHelper {
 public:
  explicit BrowsingDataIndexedDBHelperImpl(
      IndexedDBContext* indexed_db_context);

  virtual void StartFetching(
      const base::Callback<void(const std::list<IndexedDBInfo>&)>&
          callback) OVERRIDE;
  virtual void DeleteIndexedDB(const GURL& origin) OVERRIDE;

 private:
  virtual ~BrowsingDataIndexedDBHelperImpl();

  // Enumerates all indexed database files in the IndexedDB thread.
  void FetchIndexedDBInfoInIndexedDBThread();
  // Notifies the completion callback in the UI thread.
  void NotifyInUIThread();
  // Delete a single indexed database in the IndexedDB thread.
  void DeleteIndexedDBInIndexedDBThread(const GURL& origin);

  scoped_refptr<IndexedDBContext> indexed_db_context_;

  // Access to |indexed_db_info_| is triggered indirectly via the UI thread and
  // guarded by |is_fetching_|. This means |indexed_db_info_| is only accessed
  // while |is_fetching_| is true. The flag |is_fetching_| is only accessed on
  // the UI thread.
  // In the context of this class |indexed_db_info_| is only accessed on the
  // context's IndexedDB thread.
  std::list<IndexedDBInfo> indexed_db_info_;

  // This only mutates on the UI thread.
  base::Callback<void(const std::list<IndexedDBInfo>&)> completion_callback_;

  // Indicates whether or not we're currently fetching information:
  // it's true when StartFetching() is called in the UI thread, and it's reset
  // after we notified the callback in the UI thread.
  // This only mutates on the UI thread.
  bool is_fetching_;

  DISALLOW_COPY_AND_ASSIGN(BrowsingDataIndexedDBHelperImpl);
};

BrowsingDataIndexedDBHelperImpl::BrowsingDataIndexedDBHelperImpl(
    IndexedDBContext* indexed_db_context)
    : indexed_db_context_(indexed_db_context),
      is_fetching_(false) {
  DCHECK(indexed_db_context_.get());
}

BrowsingDataIndexedDBHelperImpl::~BrowsingDataIndexedDBHelperImpl() {
}

void BrowsingDataIndexedDBHelperImpl::StartFetching(
    const base::Callback<void(const std::list<IndexedDBInfo>&)>& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!is_fetching_);
  DCHECK_EQ(false, callback.is_null());

  is_fetching_ = true;
  completion_callback_ = callback;
  indexed_db_context_->TaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(
          &BrowsingDataIndexedDBHelperImpl::FetchIndexedDBInfoInIndexedDBThread,
          this));
}

void BrowsingDataIndexedDBHelperImpl::DeleteIndexedDB(
    const GURL& origin) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  indexed_db_context_->TaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(
          &BrowsingDataIndexedDBHelperImpl::DeleteIndexedDBInIndexedDBThread,
          this,
          origin));
}

void BrowsingDataIndexedDBHelperImpl::FetchIndexedDBInfoInIndexedDBThread() {
  DCHECK(indexed_db_context_->TaskRunner()->RunsTasksOnCurrentThread());
  std::vector<IndexedDBInfo> origins = indexed_db_context_->GetAllOriginsInfo();
  for (std::vector<IndexedDBInfo>::const_iterator iter = origins.begin();
       iter != origins.end(); ++iter) {
    const IndexedDBInfo& origin = *iter;
    if (!BrowsingDataHelper::HasWebScheme(origin.origin_))
      continue;  // Non-websafe state is not considered browsing data.

    indexed_db_info_.push_back(origin);
  }

  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&BrowsingDataIndexedDBHelperImpl::NotifyInUIThread, this));
}

void BrowsingDataIndexedDBHelperImpl::NotifyInUIThread() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(is_fetching_);
  completion_callback_.Run(indexed_db_info_);
  completion_callback_.Reset();
  is_fetching_ = false;
}

void BrowsingDataIndexedDBHelperImpl::DeleteIndexedDBInIndexedDBThread(
    const GURL& origin) {
  DCHECK(indexed_db_context_->TaskRunner()->RunsTasksOnCurrentThread());
  indexed_db_context_->DeleteForOrigin(origin);
}

}  // namespace


// static
BrowsingDataIndexedDBHelper* BrowsingDataIndexedDBHelper::Create(
    IndexedDBContext* indexed_db_context) {
  return new BrowsingDataIndexedDBHelperImpl(indexed_db_context);
}

CannedBrowsingDataIndexedDBHelper::
PendingIndexedDBInfo::PendingIndexedDBInfo(const GURL& origin,
                                           const string16& name)
    : origin(origin),
      name(name) {
}

CannedBrowsingDataIndexedDBHelper::
PendingIndexedDBInfo::~PendingIndexedDBInfo() {
}

bool CannedBrowsingDataIndexedDBHelper::PendingIndexedDBInfo::operator<(
    const PendingIndexedDBInfo& other) const {
  if (origin == other.origin)
    return name < other.name;
  return origin < other.origin;
}

CannedBrowsingDataIndexedDBHelper::CannedBrowsingDataIndexedDBHelper()
    : is_fetching_(false) {
}

CannedBrowsingDataIndexedDBHelper::~CannedBrowsingDataIndexedDBHelper() {}

CannedBrowsingDataIndexedDBHelper* CannedBrowsingDataIndexedDBHelper::Clone() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  CannedBrowsingDataIndexedDBHelper* clone =
      new CannedBrowsingDataIndexedDBHelper();

  clone->pending_indexed_db_info_ = pending_indexed_db_info_;
  clone->indexed_db_info_ = indexed_db_info_;
  return clone;
}

void CannedBrowsingDataIndexedDBHelper::AddIndexedDB(
    const GURL& origin, const string16& name) {
  if (!BrowsingDataHelper::HasWebScheme(origin))
    return;  // Non-websafe state is not considered browsing data.

  pending_indexed_db_info_.insert(PendingIndexedDBInfo(origin, name));
}

void CannedBrowsingDataIndexedDBHelper::Reset() {
  indexed_db_info_.clear();
  pending_indexed_db_info_.clear();
}

bool CannedBrowsingDataIndexedDBHelper::empty() const {
  return indexed_db_info_.empty() && pending_indexed_db_info_.empty();
}

size_t CannedBrowsingDataIndexedDBHelper::GetIndexedDBCount() const {
  return pending_indexed_db_info_.size();
}

const std::set<CannedBrowsingDataIndexedDBHelper::PendingIndexedDBInfo>&
CannedBrowsingDataIndexedDBHelper::GetIndexedDBInfo() const  {
  return pending_indexed_db_info_;
}

void CannedBrowsingDataIndexedDBHelper::StartFetching(
    const base::Callback<void(const std::list<IndexedDBInfo>&)>& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!is_fetching_);
  DCHECK_EQ(false, callback.is_null());

  is_fetching_ = true;
  completion_callback_ = callback;

  // We post a task to emulate async fetching behavior.
  base::MessageLoop::current()->PostTask(
      FROM_HERE,
      base::Bind(&CannedBrowsingDataIndexedDBHelper::
                     ConvertPendingInfo,
                 this));
}

void CannedBrowsingDataIndexedDBHelper::ConvertPendingInfo() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  indexed_db_info_.clear();
  for (std::set<PendingIndexedDBInfo>::const_iterator
       pending_info = pending_indexed_db_info_.begin();
       pending_info != pending_indexed_db_info_.end(); ++pending_info) {
    IndexedDBInfo info(
        pending_info->origin, 0, base::Time(), base::FilePath(), 0);
    indexed_db_info_.push_back(info);
  }

  completion_callback_.Run(indexed_db_info_);
  completion_callback_.Reset();
  is_fetching_ = false;
}
