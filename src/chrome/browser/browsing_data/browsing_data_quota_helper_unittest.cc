// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "testing/gtest/include/gtest/gtest.h"

#include "base/bind.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/message_loop/message_loop_proxy.h"
#include "chrome/browser/browsing_data/browsing_data_quota_helper_impl.h"
#include "content/public/test/test_browser_thread.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "webkit/browser/quota/mock_storage_client.h"
#include "webkit/browser/quota/quota_manager.h"

using content::BrowserThread;

class BrowsingDataQuotaHelperTest : public testing::Test {
 public:
  typedef BrowsingDataQuotaHelper::QuotaInfo QuotaInfo;
  typedef BrowsingDataQuotaHelper::QuotaInfoArray QuotaInfoArray;

  BrowsingDataQuotaHelperTest()
      : fetching_completed_(true),
        quota_(-1),
        weak_factory_(this) {}

  virtual ~BrowsingDataQuotaHelperTest() {}

  virtual void SetUp() OVERRIDE {
    EXPECT_TRUE(dir_.CreateUniqueTempDir());
    quota_manager_ = new quota::QuotaManager(
        false,
        dir_.path(),
        BrowserThread::GetMessageLoopProxyForThread(BrowserThread::IO).get(),
        BrowserThread::GetMessageLoopProxyForThread(BrowserThread::DB).get(),
        NULL);
    helper_ = new BrowsingDataQuotaHelperImpl(
        BrowserThread::GetMessageLoopProxyForThread(BrowserThread::UI).get(),
        BrowserThread::GetMessageLoopProxyForThread(BrowserThread::IO).get(),
        quota_manager_.get());
  }

  virtual void TearDown() OVERRIDE {
    helper_ = NULL;
    quota_manager_ = NULL;
    quota_info_.clear();
    base::MessageLoop::current()->RunUntilIdle();
  }

 protected:
  const QuotaInfoArray& quota_info() const {
    return quota_info_;
  }

  bool fetching_completed() const {
    return fetching_completed_;
  }

  void StartFetching() {
    fetching_completed_ = false;
    helper_->StartFetching(
        base::Bind(&BrowsingDataQuotaHelperTest::FetchCompleted,
                   weak_factory_.GetWeakPtr()));
  }

  void RegisterClient(const quota::MockOriginData* data, std::size_t data_len) {
    quota::MockStorageClient* client =
        new quota::MockStorageClient(
            quota_manager_->proxy(), data, quota::QuotaClient::kFileSystem,
            data_len);
    quota_manager_->proxy()->RegisterClient(client);
    client->TouchAllOriginsAndNotify();
  }

  void SetPersistentHostQuota(const std::string& host, int64 quota) {
    quota_ = -1;
    quota_manager_->SetPersistentHostQuota(
        host, quota,
        base::Bind(&BrowsingDataQuotaHelperTest::GotPersistentHostQuota,
                   weak_factory_.GetWeakPtr()));
  }

  void GetPersistentHostQuota(const std::string& host) {
    quota_ = -1;
    quota_manager_->GetPersistentHostQuota(
        host,
        base::Bind(&BrowsingDataQuotaHelperTest::GotPersistentHostQuota,
                   weak_factory_.GetWeakPtr()));
  }

  void GotPersistentHostQuota(quota::QuotaStatusCode status,
                              int64 quota) {
    EXPECT_EQ(quota::kQuotaStatusOk, status);
    quota_ = quota;
  }

  void RevokeHostQuota(const std::string& host) {
    helper_->RevokeHostQuota(host);
  }

  int64 quota() {
    return quota_;
  }

 private:
  void FetchCompleted(const QuotaInfoArray& quota_info) {
    quota_info_ = quota_info;
    fetching_completed_ = true;
  }

  content::TestBrowserThreadBundle thread_bundle_;
  scoped_refptr<quota::QuotaManager> quota_manager_;

  base::ScopedTempDir dir_;
  scoped_refptr<BrowsingDataQuotaHelper> helper_;

  bool fetching_completed_;
  QuotaInfoArray quota_info_;
  int64 quota_;
  base::WeakPtrFactory<BrowsingDataQuotaHelperTest> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(BrowsingDataQuotaHelperTest);
};

TEST_F(BrowsingDataQuotaHelperTest, Empty) {
  StartFetching();
  base::MessageLoop::current()->RunUntilIdle();
  EXPECT_TRUE(fetching_completed());
  EXPECT_TRUE(quota_info().empty());
}

TEST_F(BrowsingDataQuotaHelperTest, FetchData) {
  const quota::MockOriginData kOrigins[] = {
    {"http://example.com/", quota::kStorageTypeTemporary, 1},
    {"https://example.com/", quota::kStorageTypeTemporary, 10},
    {"http://example.com/", quota::kStorageTypePersistent, 100},
    {"https://example.com/", quota::kStorageTypeSyncable, 1},
    {"http://example2.com/", quota::kStorageTypeTemporary, 1000},
  };

  RegisterClient(kOrigins, arraysize(kOrigins));
  StartFetching();
  base::MessageLoop::current()->RunUntilIdle();
  EXPECT_TRUE(fetching_completed());

  std::set<QuotaInfo> expected, actual;
  actual.insert(quota_info().begin(), quota_info().end());
  expected.insert(QuotaInfo("example.com", 11, 100, 1));
  expected.insert(QuotaInfo("example2.com", 1000, 0, 0));
  EXPECT_TRUE(expected == actual);
}

TEST_F(BrowsingDataQuotaHelperTest, IgnoreExtensionsAndDevTools) {
  const quota::MockOriginData kOrigins[] = {
    {"http://example.com/", quota::kStorageTypeTemporary, 1},
    {"https://example.com/", quota::kStorageTypeTemporary, 10},
    {"http://example.com/", quota::kStorageTypePersistent, 100},
    {"https://example.com/", quota::kStorageTypeSyncable, 1},
    {"http://example2.com/", quota::kStorageTypeTemporary, 1000},
    {"chrome-extension://abcdefghijklmnopqrstuvwxyz/",
        quota::kStorageTypeTemporary, 10000},
    {"chrome-extension://abcdefghijklmnopqrstuvwxyz/",
        quota::kStorageTypePersistent, 100000},
    {"chrome-devtools://abcdefghijklmnopqrstuvwxyz/",
        quota::kStorageTypeTemporary, 10000},
    {"chrome-devtools://abcdefghijklmnopqrstuvwxyz/",
        quota::kStorageTypePersistent, 100000},
  };

  RegisterClient(kOrigins, arraysize(kOrigins));
  StartFetching();
  base::MessageLoop::current()->RunUntilIdle();
  EXPECT_TRUE(fetching_completed());

  std::set<QuotaInfo> expected, actual;
  actual.insert(quota_info().begin(), quota_info().end());
  expected.insert(QuotaInfo("example.com", 11, 100, 1));
  expected.insert(QuotaInfo("example2.com", 1000, 0, 0));
  EXPECT_TRUE(expected == actual);
}

TEST_F(BrowsingDataQuotaHelperTest, RevokeHostQuota) {
  const std::string kHost1("example1.com");
  const std::string kHost2("example2.com");

  SetPersistentHostQuota(kHost1, 1);
  SetPersistentHostQuota(kHost2, 10);
  base::MessageLoop::current()->RunUntilIdle();

  RevokeHostQuota(kHost1);
  base::MessageLoop::current()->RunUntilIdle();

  GetPersistentHostQuota(kHost1);
  base::MessageLoop::current()->RunUntilIdle();
  EXPECT_EQ(0, quota());

  GetPersistentHostQuota(kHost2);
  base::MessageLoop::current()->RunUntilIdle();
  EXPECT_EQ(10, quota());
}
