// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/cloud/cloud_external_data_manager_base.h"

#include <map>
#include <string>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/files/scoped_temp_dir.h"
#include "base/message_loop/message_loop_proxy.h"
#include "base/run_loop.h"
#include "base/sequenced_task_runner.h"
#include "base/sha1.h"
#include "base/stl_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "chrome/browser/policy/cloud/cloud_external_data_store.h"
#include "chrome/browser/policy/cloud/mock_cloud_policy_store.h"
#include "chrome/browser/policy/cloud/resource_cache.h"
#include "chrome/browser/policy/external_data_fetcher.h"
#include "chrome/browser/policy/policy_map.h"
#include "chrome/browser/policy/policy_types.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "net/url_request/test_url_fetcher_factory.h"
#include "net/url_request/url_fetcher.h"
#include "net/url_request/url_fetcher_delegate.h"
#include "net/url_request/url_request_test_util.h"
#include "policy/policy_constants.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace policy {

namespace {

// A string policy.
const char kStringPolicy[] = "StringPolicy";
// A policy that may reference up to 10 bytes of external data.
const char k10BytePolicy[] = "10BytePolicy";
// A policy that may reference up to 20 bytes of external data.
const char k20BytePolicy[] = "20BytePolicy";
// A nonexistent policy.
const char kUnknownPolicy[] = "UnknownPolicy";

const char k10BytePolicyURL[] = "http://localhost/10_bytes";
const char k20BytePolicyURL[] = "http://localhost/20_bytes";

const char k10ByteData[] = "10 bytes..";
const char k20ByteData[] = "20 bytes............";

const PolicyDefinitionList::Entry kPolicyDefinitionListEntries[] = {
  { kStringPolicy, base::Value::TYPE_STRING, false, 1, 0 },
  { k10BytePolicy, base::Value::TYPE_DICTIONARY, false, 2, 10 },
  { k20BytePolicy, base::Value::TYPE_DICTIONARY, false, 3, 20 },
};

const PolicyDefinitionList kPolicyDefinitionList = {
  kPolicyDefinitionListEntries,
  kPolicyDefinitionListEntries + arraysize(kPolicyDefinitionListEntries),
};

const char kCacheKey[] = "data";

// A variant of net::FakeURLFetcherFactory that makes it an error to request a
// fetcher for an unknown URL.
class FakeURLFetcherFactory : public net::FakeURLFetcherFactory {
 public:
  FakeURLFetcherFactory();
  virtual ~FakeURLFetcherFactory();

  // net::FakeURLFetcherFactory:
  virtual net::URLFetcher* CreateURLFetcher(
      int id,
      const GURL& url,
      net::URLFetcher::RequestType request_type,
      net::URLFetcherDelegate* delegate) OVERRIDE;

 private:
  DISALLOW_COPY_AND_ASSIGN(FakeURLFetcherFactory);
};

FakeURLFetcherFactory::FakeURLFetcherFactory()
    : net::FakeURLFetcherFactory(NULL) {
}

FakeURLFetcherFactory::~FakeURLFetcherFactory() {
}

net::URLFetcher* FakeURLFetcherFactory::CreateURLFetcher(
    int id,
    const GURL& url,
    net::URLFetcher::RequestType request_type,
    net::URLFetcherDelegate* delegate) {
  net::URLFetcher* fetcher = net::FakeURLFetcherFactory::CreateURLFetcher(
      id, url, request_type, delegate);
  EXPECT_TRUE(fetcher);
  return fetcher;
}

}  // namespace

class CouldExternalDataManagerBaseTest : public testing::Test {
 protected:
  CouldExternalDataManagerBaseTest();

  virtual void SetUp() OVERRIDE;
  virtual void TearDown() OVERRIDE;

  void SetUpExternalDataManager();

  scoped_ptr<base::DictionaryValue> ConstructMetadata(const std::string& url,
                                                      const std::string& hash);
  void SetExternalDataReference(const std::string& policy,
                                scoped_ptr<base::DictionaryValue> metadata);

  ExternalDataFetcher::FetchCallback ConstructFetchCallback(int id);
  void ResetCallbackData();

  void OnFetchDone(int id, scoped_ptr<std::string> data);

  void FetchAll();

  content::TestBrowserThreadBundle thread_bundle_;

  base::ScopedTempDir temp_dir_;
  scoped_ptr<ResourceCache> resource_cache_;
  MockCloudPolicyStore cloud_policy_store_;
  scoped_refptr<net::TestURLRequestContextGetter> request_content_getter_;
  FakeURLFetcherFactory fetcher_factory_;

  scoped_ptr<CloudExternalDataManagerBase> external_data_manager_;

  std::map<int, std::string*> callback_data_;

  DISALLOW_COPY_AND_ASSIGN(CouldExternalDataManagerBaseTest);
};

CouldExternalDataManagerBaseTest::CouldExternalDataManagerBaseTest()
    : thread_bundle_(content::TestBrowserThreadBundle::IO_MAINLOOP) {
}

void CouldExternalDataManagerBaseTest::SetUp() {
  ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
  resource_cache_.reset(new ResourceCache(temp_dir_.path()));
  SetUpExternalDataManager();

  // Set |kStringPolicy| to a string value.
  cloud_policy_store_.policy_map_.Set(kStringPolicy,
                                      POLICY_LEVEL_MANDATORY,
                                      POLICY_SCOPE_USER,
                                      new base::StringValue(std::string()),
                                      NULL);
  // Make |k10BytePolicy| reference 10 bytes of external data.
  SetExternalDataReference(
      k10BytePolicy,
      ConstructMetadata(k10BytePolicyURL, base::SHA1HashString(k10ByteData)));
  // Make |k20BytePolicy| reference 20 bytes of external data.
  SetExternalDataReference(
      k20BytePolicy,
      ConstructMetadata(k20BytePolicyURL, base::SHA1HashString(k20ByteData)));
  cloud_policy_store_.NotifyStoreLoaded();

  request_content_getter_ = new net::TestURLRequestContextGetter(
      base::MessageLoopProxy::current());
}

void CouldExternalDataManagerBaseTest::TearDown() {
  base::RunLoop().RunUntilIdle();
  ResetCallbackData();
}

void CouldExternalDataManagerBaseTest::SetUpExternalDataManager() {
  external_data_manager_.reset(new CloudExternalDataManagerBase(
      &kPolicyDefinitionList, base::MessageLoopProxy::current()));
  external_data_manager_->SetExternalDataStore(make_scoped_ptr(
      new CloudExternalDataStore(kCacheKey, resource_cache_.get())));
  external_data_manager_->SetPolicyStore(&cloud_policy_store_);
}

scoped_ptr<base::DictionaryValue>
    CouldExternalDataManagerBaseTest::ConstructMetadata(
        const std::string& url,
        const std::string& hash) {
  scoped_ptr<base::DictionaryValue> metadata(new base::DictionaryValue);
  metadata->SetStringWithoutPathExpansion("url", url);
  metadata->SetStringWithoutPathExpansion("hash", base::HexEncode(hash.c_str(),
                                                                  hash.size()));
  return metadata.Pass();
}

void CouldExternalDataManagerBaseTest::SetExternalDataReference(
    const std::string& policy,
    scoped_ptr<base::DictionaryValue> metadata) {
  cloud_policy_store_.policy_map_.Set(
      policy,
      POLICY_LEVEL_MANDATORY,
      POLICY_SCOPE_USER,
      metadata.release(),
      new ExternalDataFetcher(
          external_data_manager_->weak_factory_.GetWeakPtr(), policy));
}

ExternalDataFetcher::FetchCallback
CouldExternalDataManagerBaseTest::ConstructFetchCallback(int id) {
  return base::Bind(&CouldExternalDataManagerBaseTest::OnFetchDone,
                    base::Unretained(this),
                    id);
}

void CouldExternalDataManagerBaseTest::ResetCallbackData() {
  STLDeleteValues(&callback_data_);
}

void CouldExternalDataManagerBaseTest::OnFetchDone(
    int id,
    scoped_ptr<std::string> data) {
  delete callback_data_[id];
  callback_data_[id] = data.release();
}

void CouldExternalDataManagerBaseTest::FetchAll() {
  external_data_manager_->FetchAll();
}

// Verifies that when no valid external data reference has been set for a
// policy, the attempt to retrieve the external data fails immediately.
TEST_F(CouldExternalDataManagerBaseTest, FailToFetchInvalid) {
  external_data_manager_->Connect(request_content_getter_);

  // Attempt to retrieve external data for |kStringPolicy|, which is a string
  // policy that does not reference any external data.
  external_data_manager_->Fetch(kStringPolicy, ConstructFetchCallback(0));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, callback_data_.size());
  EXPECT_TRUE(callback_data_.find(0) != callback_data_.end());
  EXPECT_FALSE(callback_data_[0]);
  ResetCallbackData();

  // Attempt to retrieve external data for |kUnknownPolicy|, which is not a
  // known policy.
  external_data_manager_->Fetch(kUnknownPolicy, ConstructFetchCallback(1));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, callback_data_.size());
  EXPECT_TRUE(callback_data_.find(1) != callback_data_.end());
  EXPECT_FALSE(callback_data_[1]);
  ResetCallbackData();

  // Set an invalid external data reference for |k10BytePolicy|.
  SetExternalDataReference(k10BytePolicy,
                           ConstructMetadata(std::string(), std::string()));
  cloud_policy_store_.NotifyStoreLoaded();

  // Attempt to retrieve external data for |k10BytePolicy|, which now has an
  // invalid reference.
  external_data_manager_->Fetch(k10BytePolicy, ConstructFetchCallback(2));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, callback_data_.size());
  EXPECT_TRUE(callback_data_.find(2) != callback_data_.end());
  EXPECT_FALSE(callback_data_[2]);
  ResetCallbackData();
}

// Verifies that external data referenced by a policy is downloaded and cached
// when first requested. Subsequent requests are served from the cache without
// further download attempts.
TEST_F(CouldExternalDataManagerBaseTest, DownloadAndCache) {
  // Serve valid external data for |k10BytePolicy|.
  fetcher_factory_.SetFakeResponse(k10BytePolicyURL, k10ByteData, true);
  external_data_manager_->Connect(request_content_getter_);

  // Retrieve external data for |k10BytePolicy|. Verify that a download happens
  // and the callback is invoked with the downloaded data.
  external_data_manager_->Fetch(k10BytePolicy, ConstructFetchCallback(0));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, callback_data_.size());
  ASSERT_TRUE(callback_data_[0]);
  EXPECT_EQ(k10ByteData, *callback_data_[0]);
  ResetCallbackData();

  // Stop serving external data for |k10BytePolicy|.
  fetcher_factory_.ClearFakeResponses();

  // Retrieve external data for |k10BytePolicy| again. Verify that no download
  // is attempted but the callback is still invoked with the expected data,
  // served from the cache.
  external_data_manager_->Fetch(k10BytePolicy, ConstructFetchCallback(1));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, callback_data_.size());
  ASSERT_TRUE(callback_data_[1]);
  EXPECT_EQ(k10ByteData, *callback_data_[1]);
  ResetCallbackData();

  // Explicitly tell the external_data_manager_ to not make any download
  // attempts.
  external_data_manager_->Disconnect();

  // Retrieve external data for |k10BytePolicy| again. Verify that even though
  // downloads are not allowed, the callback is still invoked with the expected
  // data, served from the cache.
  external_data_manager_->Fetch(k10BytePolicy, ConstructFetchCallback(2));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, callback_data_.size());
  ASSERT_TRUE(callback_data_[2]);
  EXPECT_EQ(k10ByteData, *callback_data_[2]);
  ResetCallbackData();

  // Verify that the downloaded data is present in the cache.
  external_data_manager_.reset();
  base::RunLoop().RunUntilIdle();
  std::string data;
  EXPECT_TRUE(CloudExternalDataStore(kCacheKey, resource_cache_.get()).Load(
      k10BytePolicy, base::SHA1HashString(k10ByteData), 10, &data));
  EXPECT_EQ(k10ByteData, data);
}

// Verifies that a request to download and cache all external data referenced by
// policies is carried out correctly. Subsequent requests for the data are
// served from the cache without further download attempts.
TEST_F(CouldExternalDataManagerBaseTest, DownloadAndCacheAll) {
  // Serve valid external data for |k10BytePolicy| and |k20BytePolicy|.
  fetcher_factory_.SetFakeResponse(k10BytePolicyURL, k10ByteData, true);
  fetcher_factory_.SetFakeResponse(k20BytePolicyURL, k20ByteData, true);
  external_data_manager_->Connect(request_content_getter_);

  // Request that external data referenced by all policies be downloaded.
  FetchAll();
  base::RunLoop().RunUntilIdle();

  // Stop serving external data for |k10BytePolicy| and |k20BytePolicy|.
  fetcher_factory_.ClearFakeResponses();

  // Retrieve external data for |k10BytePolicy| and |k20BytePolicy|. Verify that
  // no downloads are attempted but the callbacks are still invoked with the
  // expected data, served from the cache.
  external_data_manager_->Fetch(k10BytePolicy, ConstructFetchCallback(0));
  external_data_manager_->Fetch(k20BytePolicy, ConstructFetchCallback(1));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(2u, callback_data_.size());
  ASSERT_TRUE(callback_data_[0]);
  EXPECT_EQ(k10ByteData, *callback_data_[0]);
  ASSERT_TRUE(callback_data_[1]);
  EXPECT_EQ(k20ByteData, *callback_data_[1]);
  ResetCallbackData();

  // Explicitly tell the external_data_manager_ to not make any download
  // attempts.
  external_data_manager_->Disconnect();

  // Retrieve external data for |k10BytePolicy| and |k20BytePolicy|. Verify that
  // even though downloads are not allowed, the callbacks are still invoked with
  // the expected data, served from the cache.
  external_data_manager_->Fetch(k10BytePolicy, ConstructFetchCallback(2));
  external_data_manager_->Fetch(k20BytePolicy, ConstructFetchCallback(3));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(2u, callback_data_.size());
  ASSERT_TRUE(callback_data_[2]);
  EXPECT_EQ(k10ByteData, *callback_data_[2]);
  ASSERT_TRUE(callback_data_[3]);
  EXPECT_EQ(k20ByteData, *callback_data_[3]);
  ResetCallbackData();

  // Verify that the downloaded data is present in the cache.
  external_data_manager_.reset();
  base::RunLoop().RunUntilIdle();
  CloudExternalDataStore cache(kCacheKey, resource_cache_.get());
  std::string data;
  EXPECT_TRUE(cache.Load(k10BytePolicy, base::SHA1HashString(k10ByteData), 10,
                         &data));
  EXPECT_EQ(k10ByteData, data);
  EXPECT_TRUE(cache.Load(k20BytePolicy, base::SHA1HashString(k20ByteData), 20,
                         &data));
  EXPECT_EQ(k20ByteData, data);
}

// Verifies that when the external data referenced by a policy is not present in
// the cache and downloads are not allowed, a request to retrieve the data is
// enqueued and carried out when downloads become possible.
TEST_F(CouldExternalDataManagerBaseTest, DownloadAfterConnect) {
  // Attempt to retrieve external data for |k10BytePolicy|. Verify that the
  // callback is not invoked as the request remains pending.
  external_data_manager_->Fetch(k10BytePolicy, ConstructFetchCallback(0));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_data_.empty());
  ResetCallbackData();

  // Serve valid external data for |k10BytePolicy| and allow the
  // external_data_manager_ to perform downloads.
  fetcher_factory_.SetFakeResponse(k10BytePolicyURL, k10ByteData, true);
  external_data_manager_->Connect(request_content_getter_);

  // Verify that a download happens and the callback is invoked with the
  // downloaded data.
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, callback_data_.size());
  ASSERT_TRUE(callback_data_[0]);
  EXPECT_EQ(k10ByteData, *callback_data_[0]);
  ResetCallbackData();
}

// Verifies that when the external data referenced by a policy is not present in
// the cache and cannot be downloaded at this time, a request to retrieve the
// data is enqueued to be retried later.
TEST_F(CouldExternalDataManagerBaseTest, DownloadError) {
  // Make attempts to download the external data for |k20BytePolicy| fail with
  // an error.
  fetcher_factory_.SetFakeResponse(k20BytePolicyURL, std::string(), false);
  external_data_manager_->Connect(request_content_getter_);

  // Attempt to retrieve external data for |k20BytePolicy|. Verify that the
  // callback is not invoked as the download attempt fails and the request
  // remains pending.
  external_data_manager_->Fetch(k20BytePolicy, ConstructFetchCallback(0));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_data_.empty());
  ResetCallbackData();

  // Modify the external data reference for |k20BytePolicy|, allowing the
  // download to be retried immediately.
  SetExternalDataReference(
      k20BytePolicy,
      ConstructMetadata(k20BytePolicyURL, base::SHA1HashString(k10ByteData)));
  cloud_policy_store_.NotifyStoreLoaded();

  // Attempt to retrieve external data for |k20BytePolicy| again. Verify that
  // no callback is invoked still as the download attempt fails again and the
  // request remains pending.
  external_data_manager_->Fetch(k20BytePolicy, ConstructFetchCallback(1));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_data_.empty());
  ResetCallbackData();

  // Modify the external data reference for |k20BytePolicy|, allowing the
  // download to be retried immediately.
  SetExternalDataReference(
      k20BytePolicy,
      ConstructMetadata(k20BytePolicyURL, base::SHA1HashString(k20ByteData)));
  cloud_policy_store_.NotifyStoreLoaded();

  // Serve external data for |k20BytePolicy| that does not match the hash
  // specified in its current external data reference.
  fetcher_factory_.SetFakeResponse(k20BytePolicyURL, k10ByteData, true);

  // Attempt to retrieve external data for |k20BytePolicy| again. Verify that
  // no callback is invoked still as the downloaded succeeds but returns data
  // that does not match the external data reference.
  external_data_manager_->Fetch(k20BytePolicy, ConstructFetchCallback(2));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_data_.empty());
  ResetCallbackData();

  // Modify the external data reference for |k20BytePolicy|, allowing the
  // download to be retried immediately. The external data reference now matches
  // the data being served.
  SetExternalDataReference(
      k20BytePolicy,
      ConstructMetadata(k20BytePolicyURL, base::SHA1HashString(k10ByteData)));
  cloud_policy_store_.NotifyStoreLoaded();

  // Attempt to retrieve external data for |k20BytePolicy| again. Verify that
  // the current callback and the three previously enqueued callbacks are
  // invoked with the downloaded data now.
  external_data_manager_->Fetch(k20BytePolicy, ConstructFetchCallback(3));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(4u, callback_data_.size());
  ASSERT_TRUE(callback_data_[0]);
  EXPECT_EQ(k10ByteData, *callback_data_[0]);
  ASSERT_TRUE(callback_data_[1]);
  EXPECT_EQ(k10ByteData, *callback_data_[1]);
  ASSERT_TRUE(callback_data_[2]);
  EXPECT_EQ(k10ByteData, *callback_data_[2]);
  ASSERT_TRUE(callback_data_[3]);
  EXPECT_EQ(k10ByteData, *callback_data_[3]);
  ResetCallbackData();
}

// Verifies that when the external data referenced by a policy is present in the
// cache, a request to retrieve it is served from the cache without any download
// attempts.
TEST_F(CouldExternalDataManagerBaseTest, LoadFromCache) {
  // Store valid external data for |k10BytePolicy| in the cache.
  external_data_manager_.reset();
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(CloudExternalDataStore(kCacheKey, resource_cache_.get()).Store(
      k10BytePolicy, base::SHA1HashString(k10ByteData), k10ByteData));

  // Instantiate an external_data_manager_ that uses the primed cache.
  SetUpExternalDataManager();
  external_data_manager_->Connect(request_content_getter_);

  // Retrieve external data for |k10BytePolicy|. Verify that no download is
  // attempted but the callback is still invoked with the expected data, served
  // from the cache.
  external_data_manager_->Fetch(k10BytePolicy, ConstructFetchCallback(0));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, callback_data_.size());
  ASSERT_TRUE(callback_data_[0]);
  EXPECT_EQ(k10ByteData, *callback_data_[0]);
  ResetCallbackData();
}

// Verifies that cache entries which do not correspond to the external data
// referenced by any policy are pruned on startup.
TEST_F(CouldExternalDataManagerBaseTest, PruneCacheOnStartup) {
  external_data_manager_.reset();
  base::RunLoop().RunUntilIdle();
  scoped_ptr<CloudExternalDataStore>
      cache(new CloudExternalDataStore(kCacheKey, resource_cache_.get()));
  // Store valid external data for |k10BytePolicy| in the cache.
  EXPECT_TRUE(cache->Store(k10BytePolicy,
                           base::SHA1HashString(k10ByteData),
                           k10ByteData));
  // Store external data for |k20BytePolicy| that does not match the hash in its
  // external data reference.
  EXPECT_TRUE(cache->Store(k20BytePolicy,
                           base::SHA1HashString(k10ByteData),
                           k10ByteData));
  // Store external data for |kUnknownPolicy|, which is not a known policy and
  // therefore, cannot be referencing any external data.
  EXPECT_TRUE(cache->Store(kUnknownPolicy,
                           base::SHA1HashString(k10ByteData),
                           k10ByteData));
  cache.reset();

  // Instantiate and destroy an ExternalDataManager that uses the primed cache.
  SetUpExternalDataManager();
  external_data_manager_.reset();
  base::RunLoop().RunUntilIdle();

  cache.reset(new CloudExternalDataStore(kCacheKey, resource_cache_.get()));
  std::string data;
  // Verify that the valid external data for |k10BytePolicy| is still in the
  // cache.
  EXPECT_TRUE(cache->Load(k10BytePolicy, base::SHA1HashString(k10ByteData),
                          10, &data));
  EXPECT_EQ(k10ByteData, data);
  // Verify that the external data for |k20BytePolicy| and |kUnknownPolicy| has
  // been pruned from the cache.
  EXPECT_FALSE(cache->Load(k20BytePolicy, base::SHA1HashString(k10ByteData),
                           20, &data));
  EXPECT_FALSE(cache->Load(kUnknownPolicy, base::SHA1HashString(k10ByteData),
                           20, &data));
}

// Verifies that when the external data referenced by a policy is present in the
// cache and the reference changes, the old data is pruned from the cache.
TEST_F(CouldExternalDataManagerBaseTest, PruneCacheOnChange) {
  // Store valid external data for |k20BytePolicy| in the cache.
  external_data_manager_.reset();
  base::RunLoop().RunUntilIdle();
  scoped_ptr<CloudExternalDataStore>
      cache(new CloudExternalDataStore(kCacheKey, resource_cache_.get()));
  EXPECT_TRUE(cache->Store(k20BytePolicy,
                           base::SHA1HashString(k20ByteData),
                           k20ByteData));
  cache.reset();

  // Instantiate an ExternalDataManager that uses the primed cache.
  SetUpExternalDataManager();
  external_data_manager_->Connect(request_content_getter_);

  // Modify the external data reference for |k20BytePolicy|.
  SetExternalDataReference(
      k20BytePolicy,
      ConstructMetadata(k20BytePolicyURL, base::SHA1HashString(k10ByteData)));
  cloud_policy_store_.NotifyStoreLoaded();

  // Verify that the old external data for |k20BytePolicy| has been pruned from
  // the cache.
  external_data_manager_.reset();
  base::RunLoop().RunUntilIdle();
  cache.reset(new CloudExternalDataStore(kCacheKey, resource_cache_.get()));
  std::string data;
  EXPECT_FALSE(cache->Load(k20BytePolicy, base::SHA1HashString(k20ByteData), 20,
                           &data));
}

// Verifies that corrupt cache entries are detected and deleted when accessed.
TEST_F(CouldExternalDataManagerBaseTest, CacheCorruption) {
  external_data_manager_.reset();
  base::RunLoop().RunUntilIdle();
  scoped_ptr<CloudExternalDataStore>
      cache(new CloudExternalDataStore(kCacheKey, resource_cache_.get()));
  // Store external data for |k10BytePolicy| that exceeds the maximal external
  // data size allowed for that policy.
  EXPECT_TRUE(cache->Store(k10BytePolicy,
                           base::SHA1HashString(k20ByteData),
                           k20ByteData));
  // Store external data for |k20BytePolicy| that is corrupted and does not
  // match the expected hash.
  EXPECT_TRUE(cache->Store(k20BytePolicy,
                           base::SHA1HashString(k20ByteData),
                           k10ByteData));
  cache.reset();

  SetUpExternalDataManager();
  // Serve external data for |k10BytePolicy| that exceeds the maximal external
  // data size allowed for that policy.
  fetcher_factory_.SetFakeResponse(k10BytePolicyURL, k20ByteData, true);
  external_data_manager_->Connect(request_content_getter_);

  // Modify the external data reference for |k10BytePolicy| to match the
  // external data being served.
  SetExternalDataReference(
      k10BytePolicy,
      ConstructMetadata(k10BytePolicyURL, base::SHA1HashString(k20ByteData)));
  cloud_policy_store_.NotifyStoreLoaded();

  // Retrieve external data for |k10BytePolicy|. Verify that the callback is
  // not invoked as the cached and downloaded external data exceed the maximal
  // size allowed for this policy and the request remains pending.
  external_data_manager_->Fetch(k10BytePolicy, ConstructFetchCallback(0));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_data_.empty());
  ResetCallbackData();

  // Serve valid external data for |k20BytePolicy|.
  fetcher_factory_.SetFakeResponse(k20BytePolicyURL, k20ByteData, true);

  // Retrieve external data for |k20BytePolicy|. Verify that the callback is
  // invoked with the valid downloaded data, not the invalid data in the cache.
  external_data_manager_->Fetch(k20BytePolicy, ConstructFetchCallback(1));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, callback_data_.size());
  ASSERT_TRUE(callback_data_[1]);
  EXPECT_EQ(k20ByteData, *callback_data_[1]);
  ResetCallbackData();

  external_data_manager_.reset();
  base::RunLoop().RunUntilIdle();
  cache.reset(new CloudExternalDataStore(kCacheKey, resource_cache_.get()));
  std::string data;
  // Verify that the invalid external data for |k10BytePolicy| has been pruned
  // from the cache. Load() will return |false| in two cases:
  // 1) The cache entry for |k10BytePolicy| has been pruned.
  // 2) The cache entry for |k10BytePolicy| still exists but the cached data
  //    does not match the expected hash or exceeds the maximum size allowed.
  // To test for the former, Load() is called with a maximum data size and hash
  // that would allow the data originally written to the cache to be loaded.
  // When this fails, it is certain that the original data is no longer present
  // in the cache.
  EXPECT_FALSE(cache->Load(k10BytePolicy, base::SHA1HashString(k20ByteData), 20,
                           &data));
  // Verify that the invalid external data for |k20BytePolicy| has been replaced
  // with the downloaded valid data in the cache.
  EXPECT_TRUE(cache->Load(k20BytePolicy, base::SHA1HashString(k20ByteData), 20,
                          &data));
  EXPECT_EQ(k20ByteData, data);
}

// Verifies that when the external data reference for a policy changes while a
// download of the external data for that policy is pending, the download is
// immediately retried using the new reference.
TEST_F(CouldExternalDataManagerBaseTest, PolicyChangeWhileDownloadPending) {
  // Make attempts to download the external data for |k10BytePolicy| and
  // |k20BytePolicy| fail with an error.
  fetcher_factory_.SetFakeResponse(k10BytePolicyURL, std::string(), false);
  fetcher_factory_.SetFakeResponse(k20BytePolicyURL, std::string(), false);
  external_data_manager_->Connect(request_content_getter_);

  // Attempt to retrieve external data for |k10BytePolicy| and |k20BytePolicy|.
  // Verify that no callbacks are invoked as the download attempts fail and the
  // requests remain pending.
  external_data_manager_->Fetch(k10BytePolicy, ConstructFetchCallback(0));
  external_data_manager_->Fetch(k20BytePolicy, ConstructFetchCallback(1));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_data_.empty());
  ResetCallbackData();

  // Modify the external data reference for |k10BytePolicy| to be invalid.
  // Verify that the callback is invoked as the policy no longer has a valid
  // external data reference.
  cloud_policy_store_.policy_map_.Erase(k10BytePolicy);
  cloud_policy_store_.NotifyStoreLoaded();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, callback_data_.size());
  EXPECT_TRUE(callback_data_.find(0) != callback_data_.end());
  EXPECT_FALSE(callback_data_[0]);
  ResetCallbackData();

  // Serve valid external data for |k20BytePolicy|.
  fetcher_factory_.ClearFakeResponses();
  fetcher_factory_.SetFakeResponse(k20BytePolicyURL, k10ByteData, true);

  // Modify the external data reference for |k20BytePolicy| to match the
  // external data now being served. Verify that the callback is invoked with
  // the downloaded data.
  SetExternalDataReference(
      k20BytePolicy,
      ConstructMetadata(k20BytePolicyURL, base::SHA1HashString(k10ByteData)));
  cloud_policy_store_.NotifyStoreLoaded();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, callback_data_.size());
  ASSERT_TRUE(callback_data_[1]);
  EXPECT_EQ(k10ByteData, *callback_data_[1]);
  ResetCallbackData();
}

}  // namespace policy
