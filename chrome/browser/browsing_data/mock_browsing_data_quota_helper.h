// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_BROWSING_DATA_MOCK_BROWSING_DATA_QUOTA_HELPER_H_
#define CHROME_BROWSER_BROWSING_DATA_MOCK_BROWSING_DATA_QUOTA_HELPER_H_

#include <list>
#include <string>

#include "chrome/browser/browsing_data/browsing_data_quota_helper.h"

class MockBrowsingDataQuotaHelper : public BrowsingDataQuotaHelper {
 public:
  explicit MockBrowsingDataQuotaHelper(Profile* profile);

  void StartFetching(const FetchResultCallback& callback) override;
  void RevokeHostQuota(const std::string& host) override;

  void AddHost(const std::string& host,
               int64 temporary_usage,
               int64 persistent_usage,
               int64 syncable_usage);
  void AddQuotaSamples();
  void Notify();

 private:
  ~MockBrowsingDataQuotaHelper() override;

  FetchResultCallback callback_;
  std::list<QuotaInfo> response_;

  DISALLOW_COPY_AND_ASSIGN(MockBrowsingDataQuotaHelper);
};

#endif  // CHROME_BROWSER_BROWSING_DATA_MOCK_BROWSING_DATA_QUOTA_HELPER_H_
