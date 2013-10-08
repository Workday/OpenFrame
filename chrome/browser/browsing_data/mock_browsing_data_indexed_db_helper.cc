// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/browsing_data/mock_browsing_data_indexed_db_helper.h"

#include "base/callback.h"
#include "base/logging.h"

MockBrowsingDataIndexedDBHelper::MockBrowsingDataIndexedDBHelper() {
}

MockBrowsingDataIndexedDBHelper::~MockBrowsingDataIndexedDBHelper() {
}

void MockBrowsingDataIndexedDBHelper::StartFetching(
    const base::Callback<void(const std::list<content::IndexedDBInfo>&)>&
    callback) {
  callback_ = callback;
}

void MockBrowsingDataIndexedDBHelper::DeleteIndexedDB(
    const GURL& origin) {
  CHECK(origins_.find(origin) != origins_.end());
  origins_[origin] = false;
}

void MockBrowsingDataIndexedDBHelper::AddIndexedDBSamples() {
  const GURL kOrigin1("http://idbhost1:1/");
  const GURL kOrigin2("http://idbhost2:2/");
  content::IndexedDBInfo info1(kOrigin1, 1, base::Time(), base::FilePath(), 0);
  response_.push_back(info1);
  origins_[kOrigin1] = true;
  content::IndexedDBInfo info2(kOrigin2, 2, base::Time(), base::FilePath(), 0);
  response_.push_back(info2);
  origins_[kOrigin2] = true;
}

void MockBrowsingDataIndexedDBHelper::Notify() {
  CHECK_EQ(false, callback_.is_null());
  callback_.Run(response_);
}

void MockBrowsingDataIndexedDBHelper::Reset() {
  for (std::map<GURL, bool>::iterator i = origins_.begin();
       i != origins_.end(); ++i)
    i->second = true;
}

bool MockBrowsingDataIndexedDBHelper::AllDeleted() {
  for (std::map<GURL, bool>::const_iterator i = origins_.begin();
       i != origins_.end(); ++i)
    if (i->second)
      return false;
  return true;
}
