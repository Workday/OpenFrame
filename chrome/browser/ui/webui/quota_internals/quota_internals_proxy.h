// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_QUOTA_INTERNALS_QUOTA_INTERNALS_PROXY_H_
#define CHROME_BROWSER_UI_WEBUI_QUOTA_INTERNALS_QUOTA_INTERNALS_PROXY_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/sequenced_task_runner_helpers.h"
#include "content/public/browser/browser_thread.h"
#include "webkit/browser/quota/quota_manager.h"
#include "webkit/common/quota/quota_types.h"

namespace quota_internals {

class QuotaInternalsHandler;
class GlobalStorageInfo;
class PerHostStorageInfo;
class PerOriginStorageInfo;
typedef std::map<std::string, std::string> Statistics;

// This class is the bridge between QuotaInternalsHandler and QuotaManager.
// Each QuotaInternalsHandler instances creates and owns a instance of this
// class.
class QuotaInternalsProxy
    : public base::RefCountedThreadSafe<
          QuotaInternalsProxy,
          content::BrowserThread::DeleteOnIOThread> {
 public:
  explicit QuotaInternalsProxy(QuotaInternalsHandler* handler);

  void RequestInfo(scoped_refptr<quota::QuotaManager> quota_manager);

 private:
  friend class base::DeleteHelper<QuotaInternalsProxy>;
  friend struct content::BrowserThread::DeleteOnThread<
      content::BrowserThread::IO>;
  friend class QuotaInternalsHandler;

  typedef quota::QuotaManager::QuotaTableEntries QuotaTableEntries;
  typedef quota::QuotaManager::OriginInfoTableEntries OriginInfoTableEntries;

  virtual ~QuotaInternalsProxy();

  void ReportAvailableSpace(int64 available_space);
  void ReportGlobalInfo(const GlobalStorageInfo& data);
  void ReportPerHostInfo(const std::vector<PerHostStorageInfo>& hosts);
  void ReportPerOriginInfo(const std::vector<PerOriginStorageInfo>& origins);
  void ReportStatistics(const Statistics& stats);

  // Called on IO Thread by QuotaManager as callback.
  void DidGetAvailableSpace(quota::QuotaStatusCode status, int64 space);
  void DidGetGlobalQuota(quota::StorageType type,
                         quota::QuotaStatusCode status,
                         int64 quota);
  void DidGetGlobalUsage(quota::StorageType type,
                         int64 usage,
                         int64 unlimited_usage);
  void DidDumpQuotaTable(const QuotaTableEntries& entries);
  void DidDumpOriginInfoTable(const OriginInfoTableEntries& entries);
  void DidGetHostUsage(const std::string& host,
                       quota::StorageType type,
                       int64 usage);

  // Helper. Called on IO Thread.
  void RequestPerOriginInfo(quota::StorageType type);
  void VisitHost(const std::string& host, quota::StorageType type);
  void GetHostUsage(const std::string& host, quota::StorageType type);

  // Used on UI Thread.
  QuotaInternalsHandler* handler_;

  // Used on IO Thread.
  base::WeakPtrFactory<QuotaInternalsProxy> weak_factory_;
  scoped_refptr<quota::QuotaManager> quota_manager_;
  std::set<std::pair<std::string, quota::StorageType> >
      hosts_visited_, hosts_pending_;
  std::vector<PerHostStorageInfo> report_pending_;

  DISALLOW_COPY_AND_ASSIGN(QuotaInternalsProxy);
};
}  // quota_internals

#endif  // CHROME_BROWSER_UI_WEBUI_QUOTA_INTERNALS_QUOTA_INTERNALS_PROXY_H_
