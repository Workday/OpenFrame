// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/browsing_data/browsing_data_local_storage_helper.h"

#include "base/bind.h"
#include "base/message_loop/message_loop.h"
#include "chrome/browser/browsing_data/browsing_data_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/local_storage_usage_info.h"
#include "content/public/browser/storage_partition.h"

using content::BrowserContext;
using content::BrowserThread;
using content::DOMStorageContext;

BrowsingDataLocalStorageHelper::LocalStorageInfo::LocalStorageInfo(
    const GURL& origin_url, int64 size, base::Time last_modified)
    : origin_url(origin_url), size(size), last_modified(last_modified) {}

BrowsingDataLocalStorageHelper::LocalStorageInfo::~LocalStorageInfo() {}

BrowsingDataLocalStorageHelper::BrowsingDataLocalStorageHelper(
    Profile* profile)
    : dom_storage_context_(
          BrowserContext::GetDefaultStoragePartition(profile)->
              GetDOMStorageContext()),
      is_fetching_(false) {
  DCHECK(dom_storage_context_);
}

BrowsingDataLocalStorageHelper::~BrowsingDataLocalStorageHelper() {
}

void BrowsingDataLocalStorageHelper::StartFetching(
    const base::Callback<void(const std::list<LocalStorageInfo>&)>& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!is_fetching_);
  DCHECK_EQ(false, callback.is_null());

  is_fetching_ = true;
  completion_callback_ = callback;
  dom_storage_context_->GetLocalStorageUsage(
      base::Bind(
          &BrowsingDataLocalStorageHelper::GetUsageInfoCallback, this));
}

void BrowsingDataLocalStorageHelper::DeleteOrigin(const GURL& origin) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  dom_storage_context_->DeleteLocalStorage(origin);
}

void BrowsingDataLocalStorageHelper::GetUsageInfoCallback(
    const std::vector<content::LocalStorageUsageInfo>& infos) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  for (size_t i = 0; i < infos.size(); ++i) {
    // Non-websafe state is not considered browsing data.
    const content::LocalStorageUsageInfo& info = infos[i];
    if (BrowsingDataHelper::HasWebScheme(info.origin)) {
      local_storage_info_.push_back(
          LocalStorageInfo(info.origin, info.data_size, info.last_modified));
    }
  }

  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&BrowsingDataLocalStorageHelper::CallCompletionCallback,
                 this));
}

void BrowsingDataLocalStorageHelper::CallCompletionCallback() {
  DCHECK(is_fetching_);
  completion_callback_.Run(local_storage_info_);
  completion_callback_.Reset();
  is_fetching_ = false;
}

//---------------------------------------------------------

CannedBrowsingDataLocalStorageHelper::CannedBrowsingDataLocalStorageHelper(
    Profile* profile)
    : BrowsingDataLocalStorageHelper(profile),
      profile_(profile) {
}

CannedBrowsingDataLocalStorageHelper*
CannedBrowsingDataLocalStorageHelper::Clone() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  CannedBrowsingDataLocalStorageHelper* clone =
      new CannedBrowsingDataLocalStorageHelper(profile_);

  clone->pending_local_storage_info_ = pending_local_storage_info_;
  return clone;
}

void CannedBrowsingDataLocalStorageHelper::AddLocalStorage(
    const GURL& origin) {
  if (BrowsingDataHelper::HasWebScheme(origin))
    pending_local_storage_info_.insert(origin);
}

void CannedBrowsingDataLocalStorageHelper::Reset() {
  pending_local_storage_info_.clear();
}

bool CannedBrowsingDataLocalStorageHelper::empty() const {
  return pending_local_storage_info_.empty();
}

size_t CannedBrowsingDataLocalStorageHelper::GetLocalStorageCount() const {
  return pending_local_storage_info_.size();
}

const std::set<GURL>&
CannedBrowsingDataLocalStorageHelper::GetLocalStorageInfo() const {
  return pending_local_storage_info_;
}

void CannedBrowsingDataLocalStorageHelper::StartFetching(
    const base::Callback<void(const std::list<LocalStorageInfo>&)>& callback) {
  DCHECK(!is_fetching_);
  DCHECK_EQ(false, callback.is_null());

  is_fetching_ = true;
  completion_callback_ = callback;

  // We post a task to emulate async fetching behavior.
  base::MessageLoop::current()->PostTask(
      FROM_HERE,
      base::Bind(&CannedBrowsingDataLocalStorageHelper::ConvertPendingInfo,
                 this));
}

CannedBrowsingDataLocalStorageHelper::~CannedBrowsingDataLocalStorageHelper() {}

void CannedBrowsingDataLocalStorageHelper::ConvertPendingInfo() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  local_storage_info_.clear();
  for (std::set<GURL>::iterator iter = pending_local_storage_info_.begin();
       iter != pending_local_storage_info_.end(); ++iter) {
    local_storage_info_.push_back(
        LocalStorageInfo(*iter, 0,  base::Time()));
  }
  base::MessageLoop::current()->PostTask(
      FROM_HERE,
      base::Bind(&CannedBrowsingDataLocalStorageHelper::CallCompletionCallback,
                 this));
}
