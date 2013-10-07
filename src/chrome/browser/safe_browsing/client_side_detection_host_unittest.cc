// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/files/file_path.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/synchronization/waitable_event.h"
#include "chrome/browser/safe_browsing/browser_feature_extractor.h"
#include "chrome/browser/safe_browsing/client_side_detection_host.h"
#include "chrome/browser/safe_browsing/client_side_detection_service.h"
#include "chrome/browser/safe_browsing/database_manager.h"
#include "chrome/browser/safe_browsing/safe_browsing_service.h"
#include "chrome/browser/safe_browsing/ui_manager.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/safe_browsing/csd.pb.h"
#include "chrome/common/safe_browsing/safebrowsing_messages.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/mock_render_process_host.h"
#include "content/public/test/test_browser_thread.h"
#include "content/public/test/test_renderer_host.h"
#include "ipc/ipc_test_sink.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

using ::testing::_;
using ::testing::DeleteArg;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::IsNull;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::NotNull;
using ::testing::Pointee;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgumentPointee;
using ::testing::StrictMock;
using content::BrowserThread;
using content::RenderViewHostTester;
using content::WebContents;

namespace {
const bool kFalse = false;
const bool kTrue = true;
}

namespace safe_browsing {
namespace {
// This matcher verifies that the client computed verdict
// (ClientPhishingRequest) which is passed to SendClientReportPhishingRequest
// has the expected fields set.  Note: we can't simply compare the protocol
// buffer strings because the BrowserFeatureExtractor might add features to the
// verdict object before calling SendClientReportPhishingRequest.
MATCHER_P(PartiallyEqualVerdict, other, "") {
  return (other.url() == arg.url() &&
          other.client_score() == arg.client_score() &&
          other.is_phishing() == arg.is_phishing());
}

MATCHER_P(PartiallyEqualMalwareVerdict, other, "") {
  return (other.url() == arg.url() &&
          other.feature_map_size() == arg.feature_map_size());
}

// Test that the callback is NULL when the verdict is not phishing.
MATCHER(CallbackIsNull, "") {
  return arg.is_null();
}

ACTION(QuitUIMessageLoop) {
  EXPECT_TRUE(BrowserThread::CurrentlyOn(BrowserThread::UI));
  base::MessageLoopForUI::current()->Quit();
}

// It's kind of insane that InvokeArgument doesn't work with callbacks, but it
// doesn't seem like it.
ACTION_TEMPLATE(InvokeCallbackArgument,
                HAS_1_TEMPLATE_PARAMS(int, k),
                AND_2_VALUE_PARAMS(p0, p1)) {
  ::std::tr1::get<k>(args).Run(p0, p1);
}

void EmptyUrlCheckCallback(bool processed) {
}

class MockClientSideDetectionService : public ClientSideDetectionService {
 public:
  MockClientSideDetectionService() : ClientSideDetectionService(NULL) {}
  virtual ~MockClientSideDetectionService() {};

  MOCK_METHOD2(SendClientReportPhishingRequest,
               void(ClientPhishingRequest*,
                    const ClientReportPhishingRequestCallback&));
  MOCK_METHOD2(SendClientReportMalwareRequest,
               void(ClientMalwareRequest*,
                    const ClientReportMalwareRequestCallback&));
  MOCK_CONST_METHOD1(IsPrivateIPAddress, bool(const std::string&));
  MOCK_METHOD2(GetValidCachedResult, bool(const GURL&, bool*));
  MOCK_METHOD1(IsInCache, bool(const GURL&));
  MOCK_METHOD0(OverPhishingReportLimit, bool());

 private:
  DISALLOW_COPY_AND_ASSIGN(MockClientSideDetectionService);
};

class MockSafeBrowsingUIManager : public SafeBrowsingUIManager {
 public:
  explicit MockSafeBrowsingUIManager(SafeBrowsingService* service)
      : SafeBrowsingUIManager(service) { }

  MOCK_METHOD1(DoDisplayBlockingPage, void(const UnsafeResource& resource));

  // Helper function which calls OnBlockingPageComplete for this client
  // object.
  void InvokeOnBlockingPageComplete(const UrlCheckCallback& callback) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
    DCHECK(!callback.is_null());
    // Note: this will delete the client object in the case of the CsdClient
    // implementation.
    callback.Run(false);
  }

 protected:
  virtual ~MockSafeBrowsingUIManager() { }

 private:
  DISALLOW_COPY_AND_ASSIGN(MockSafeBrowsingUIManager);
};

class MockSafeBrowsingDatabaseManager : public SafeBrowsingDatabaseManager {
 public:
  explicit MockSafeBrowsingDatabaseManager(SafeBrowsingService* service)
      : SafeBrowsingDatabaseManager(service) { }

  MOCK_METHOD1(MatchCsdWhitelistUrl, bool(const GURL&));

 protected:
  virtual ~MockSafeBrowsingDatabaseManager() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(MockSafeBrowsingDatabaseManager);
};

class MockTestingProfile : public TestingProfile {
 public:
  MockTestingProfile() {}
  virtual ~MockTestingProfile() {}

  MOCK_CONST_METHOD0(IsOffTheRecord, bool());
};

class MockBrowserFeatureExtractor : public BrowserFeatureExtractor {
 public:
  explicit MockBrowserFeatureExtractor(
      WebContents* tab,
      ClientSideDetectionService* service)
      : BrowserFeatureExtractor(tab, service) {}
  virtual ~MockBrowserFeatureExtractor() {}

  MOCK_METHOD3(ExtractFeatures,
               void(const BrowseInfo* info,
                    ClientPhishingRequest*,
                    const BrowserFeatureExtractor::DoneCallback&));

  MOCK_METHOD2(ExtractMalwareFeatures,
               void(const BrowseInfo* info,
                    ClientMalwareRequest*));
};

}  // namespace

class ClientSideDetectionHostTest : public ChromeRenderViewHostTestHarness {
 public:
  typedef SafeBrowsingUIManager::UnsafeResource UnsafeResource;

  virtual void SetUp() {
    ChromeRenderViewHostTestHarness::SetUp();

    // Inject service classes.
    csd_service_.reset(new StrictMock<MockClientSideDetectionService>());
    // Only used for initializing mock objects.
    SafeBrowsingService* sb_service =
        SafeBrowsingService::CreateSafeBrowsingService();
    database_manager_ =
        new StrictMock<MockSafeBrowsingDatabaseManager>(sb_service);
    ui_manager_ = new StrictMock<MockSafeBrowsingUIManager>(sb_service);
    csd_host_.reset(safe_browsing::ClientSideDetectionHost::Create(
        web_contents()));
    csd_host_->set_client_side_detection_service(csd_service_.get());
    csd_host_->set_safe_browsing_managers(ui_manager_.get(),
                                          database_manager_.get());
    // We need to create this here since we don't call
    // DidNavigateMainFramePostCommit in this test.
    csd_host_->browse_info_.reset(new BrowseInfo);

    // By default this is set to false. Turn it on as if we are in canary or
    // dev channel
    csd_host_->malware_report_enabled_ = true;
  }

  virtual void TearDown() {
    // Delete the host object on the UI thread and release the
    // SafeBrowsingService.
    BrowserThread::DeleteSoon(BrowserThread::UI, FROM_HERE,
                              csd_host_.release());
    database_manager_ = NULL;
    ui_manager_ = NULL;
    base::RunLoop().RunUntilIdle();
    ChromeRenderViewHostTestHarness::TearDown();
  }

  virtual content::BrowserContext* CreateBrowserContext() OVERRIDE {
    // Set custom profile object so that we can mock calls to IsOffTheRecord.
    // This needs to happen before we call the parent SetUp() function.  We use
    // a nice mock because other parts of the code are calling IsOffTheRecord.
    mock_profile_ = new NiceMock<MockTestingProfile>();
    return mock_profile_;
  }

  void OnPhishingDetectionDone(const std::string& verdict_str) {
    csd_host_->OnPhishingDetectionDone(verdict_str);
  }

  void UpdateIPUrlMap(const std::string& ip, const std::string& host) {
    csd_host_->UpdateIPUrlMap(ip, host);
  }

  BrowseInfo* GetBrowseInfo() {
    return csd_host_->browse_info_.get();
  }

  void ExpectPreClassificationChecks(const GURL& url,
                                     const bool* is_private,
                                     const bool* is_incognito,
                                     const bool* match_csd_whitelist,
                                     const bool* get_valid_cached_result,
                                     const bool* is_in_cache,
                                     const bool* over_report_limit) {
    if (is_private) {
      EXPECT_CALL(*csd_service_, IsPrivateIPAddress(_))
          .WillOnce(Return(*is_private));
    }
    if (is_incognito) {
      EXPECT_CALL(*mock_profile_, IsOffTheRecord())
          .WillRepeatedly(Return(*is_incognito));
    }
    if (match_csd_whitelist) {
      EXPECT_CALL(*database_manager_.get(), MatchCsdWhitelistUrl(url))
          .WillOnce(Return(*match_csd_whitelist));
    }
    if (get_valid_cached_result) {
      EXPECT_CALL(*csd_service_, GetValidCachedResult(url, NotNull()))
          .WillOnce(DoAll(SetArgumentPointee<1>(true),
                          Return(*get_valid_cached_result)));
    }
    if (is_in_cache) {
      EXPECT_CALL(*csd_service_, IsInCache(url)).WillOnce(Return(*is_in_cache));
    }
    if (over_report_limit) {
      EXPECT_CALL(*csd_service_, OverPhishingReportLimit())
          .WillOnce(Return(*over_report_limit));
    }
  }

  void WaitAndCheckPreClassificationChecks() {
    // Wait for CheckCsdWhitelist and CheckCache() to be called if at all.
    base::RunLoop().RunUntilIdle();
    EXPECT_TRUE(Mock::VerifyAndClear(csd_service_.get()));
    EXPECT_TRUE(Mock::VerifyAndClear(ui_manager_.get()));
    EXPECT_TRUE(Mock::VerifyAndClear(database_manager_.get()));
    EXPECT_TRUE(Mock::VerifyAndClear(mock_profile_));
  }

  void SetFeatureExtractor(BrowserFeatureExtractor* extractor) {
    csd_host_->feature_extractor_.reset(extractor);
  }

  void SetRedirectChain(const std::vector<GURL>& redirect_chain) {
    csd_host_->browse_info_->url_redirects = redirect_chain;
  }

  void SetUnsafeResourceToCurrent() {
    UnsafeResource resource;
    resource.url = GURL("http://www.malware.com/");
    resource.original_url = web_contents()->GetURL();
    resource.is_subresource = true;
    resource.threat_type = SB_THREAT_TYPE_URL_MALWARE;
    resource.callback = base::Bind(&EmptyUrlCheckCallback);
    resource.render_process_host_id = web_contents()->GetRenderProcessHost()->
        GetID();
    resource.render_view_id =
        web_contents()->GetRenderViewHost()->GetRoutingID();
    csd_host_->OnSafeBrowsingHit(resource);
    resource.callback.Reset();
    ASSERT_TRUE(csd_host_->DidShowSBInterstitial());
    ASSERT_TRUE(csd_host_->unsafe_resource_.get());
    // Test that the resource above was copied.
    EXPECT_EQ(resource.url, csd_host_->unsafe_resource_->url);
    EXPECT_EQ(resource.original_url, csd_host_->unsafe_resource_->original_url);
    EXPECT_EQ(resource.is_subresource,
              csd_host_->unsafe_resource_->is_subresource);
    EXPECT_EQ(resource.threat_type, csd_host_->unsafe_resource_->threat_type);
    EXPECT_TRUE(csd_host_->unsafe_resource_->callback.is_null());
    EXPECT_EQ(resource.render_process_host_id,
              csd_host_->unsafe_resource_->render_process_host_id);
    EXPECT_EQ(resource.render_view_id,
              csd_host_->unsafe_resource_->render_view_id);
  }

 protected:
  scoped_ptr<ClientSideDetectionHost> csd_host_;
  scoped_ptr<StrictMock<MockClientSideDetectionService> > csd_service_;
  scoped_refptr<StrictMock<MockSafeBrowsingUIManager> > ui_manager_;
  scoped_refptr<StrictMock<MockSafeBrowsingDatabaseManager> > database_manager_;
  MockTestingProfile* mock_profile_;  // We don't own this object
};


TEST_F(ClientSideDetectionHostTest, OnPhishingDetectionDoneInvalidVerdict) {
  // Case 0: renderer sends an invalid verdict string that we're unable to
  // parse.
  MockBrowserFeatureExtractor* mock_extractor = new MockBrowserFeatureExtractor(
      web_contents(),
      csd_service_.get());
  SetFeatureExtractor(mock_extractor);  // The host class takes ownership.
  EXPECT_CALL(*mock_extractor, ExtractFeatures(_, _, _)).Times(0);
  OnPhishingDetectionDone("Invalid Protocol Buffer");
  EXPECT_TRUE(Mock::VerifyAndClear(mock_extractor));
}

TEST_F(ClientSideDetectionHostTest, OnPhishingDetectionDoneNotPhishing) {
  // Case 1: client thinks the page is phishing.  The server does not agree.
  // No interstitial is shown.
  MockBrowserFeatureExtractor* mock_extractor = new MockBrowserFeatureExtractor(
      web_contents(),
      csd_service_.get());
  SetFeatureExtractor(mock_extractor);  // The host class takes ownership.

  ClientSideDetectionService::ClientReportPhishingRequestCallback cb;
  ClientPhishingRequest verdict;
  verdict.set_url("http://phishingurl.com/");
  verdict.set_client_score(1.0f);
  verdict.set_is_phishing(true);

  EXPECT_CALL(*mock_extractor, ExtractFeatures(_, _, _))
      .WillOnce(DoAll(DeleteArg<1>(),
                      InvokeCallbackArgument<2>(true, &verdict)));
  EXPECT_CALL(*csd_service_,
              SendClientReportPhishingRequest(
                  Pointee(PartiallyEqualVerdict(verdict)), _))
      .WillOnce(SaveArg<1>(&cb));
  OnPhishingDetectionDone(verdict.SerializeAsString());
  EXPECT_TRUE(Mock::VerifyAndClear(csd_service_.get()));
  ASSERT_FALSE(cb.is_null());

  // Make sure DoDisplayBlockingPage is not going to be called.
  EXPECT_CALL(*ui_manager_.get(), DoDisplayBlockingPage(_)).Times(0);
  cb.Run(GURL(verdict.url()), false);
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(Mock::VerifyAndClear(ui_manager_.get()));
}

TEST_F(ClientSideDetectionHostTest, OnPhishingDetectionDoneDisabled) {
  // Case 2: client thinks the page is phishing and so does the server but
  // showing the interstitial is disabled => no interstitial is shown.
  MockBrowserFeatureExtractor* mock_extractor = new MockBrowserFeatureExtractor(
      web_contents(),
      csd_service_.get());
  SetFeatureExtractor(mock_extractor);  // The host class takes ownership.

  ClientSideDetectionService::ClientReportPhishingRequestCallback cb;
  ClientPhishingRequest verdict;
  verdict.set_url("http://phishingurl.com/");
  verdict.set_client_score(1.0f);
  verdict.set_is_phishing(true);

  EXPECT_CALL(*mock_extractor, ExtractFeatures(_, _, _))
      .WillOnce(DoAll(DeleteArg<1>(),
                      InvokeCallbackArgument<2>(true, &verdict)));
  EXPECT_CALL(*csd_service_,
              SendClientReportPhishingRequest(
                  Pointee(PartiallyEqualVerdict(verdict)), _))
      .WillOnce(SaveArg<1>(&cb));
  OnPhishingDetectionDone(verdict.SerializeAsString());
  EXPECT_TRUE(Mock::VerifyAndClear(csd_service_.get()));
  ASSERT_FALSE(cb.is_null());

  // Make sure DoDisplayBlockingPage is not going to be called.
  EXPECT_CALL(*ui_manager_.get(), DoDisplayBlockingPage(_)).Times(0);
  cb.Run(GURL(verdict.url()), false);
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(Mock::VerifyAndClear(ui_manager_.get()));
}

TEST_F(ClientSideDetectionHostTest, OnPhishingDetectionDoneShowInterstitial) {
  // Case 3: client thinks the page is phishing and so does the server.
  // We show an interstitial.
  MockBrowserFeatureExtractor* mock_extractor = new MockBrowserFeatureExtractor(
      web_contents(),
      csd_service_.get());
  SetFeatureExtractor(mock_extractor);  // The host class takes ownership.

  ClientSideDetectionService::ClientReportPhishingRequestCallback cb;
  GURL phishing_url("http://phishingurl.com/");
  ClientPhishingRequest verdict;
  verdict.set_url(phishing_url.spec());
  verdict.set_client_score(1.0f);
  verdict.set_is_phishing(true);

  EXPECT_CALL(*mock_extractor, ExtractFeatures(_, _, _))
      .WillOnce(DoAll(DeleteArg<1>(),
                      InvokeCallbackArgument<2>(true, &verdict)));
  EXPECT_CALL(*csd_service_,
              SendClientReportPhishingRequest(
                  Pointee(PartiallyEqualVerdict(verdict)), _))
      .WillOnce(SaveArg<1>(&cb));
  OnPhishingDetectionDone(verdict.SerializeAsString());
  EXPECT_TRUE(Mock::VerifyAndClear(csd_service_.get()));
  ASSERT_FALSE(cb.is_null());

  UnsafeResource resource;
  EXPECT_CALL(*ui_manager_.get(), DoDisplayBlockingPage(_))
      .WillOnce(SaveArg<0>(&resource));
  cb.Run(phishing_url, true);

  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(Mock::VerifyAndClear(ui_manager_.get()));
  EXPECT_EQ(phishing_url, resource.url);
  EXPECT_EQ(phishing_url, resource.original_url);
  EXPECT_FALSE(resource.is_subresource);
  EXPECT_EQ(SB_THREAT_TYPE_CLIENT_SIDE_PHISHING_URL, resource.threat_type);
  EXPECT_EQ(web_contents()->GetRenderProcessHost()->GetID(),
            resource.render_process_host_id);
  EXPECT_EQ(web_contents()->GetRenderViewHost()->GetRoutingID(),
            resource.render_view_id);

  // Make sure the client object will be deleted.
  BrowserThread::PostTask(
      BrowserThread::IO,
      FROM_HERE,
      base::Bind(&MockSafeBrowsingUIManager::InvokeOnBlockingPageComplete,
                 ui_manager_, resource.callback));
}

TEST_F(ClientSideDetectionHostTest, OnPhishingDetectionDoneMultiplePings) {
  // Case 4 & 5: client thinks a page is phishing then navigates to
  // another page which is also considered phishing by the client
  // before the server responds with a verdict.  After a while the
  // server responds for both requests with a phishing verdict.  Only
  // a single interstitial is shown for the second URL.
  MockBrowserFeatureExtractor* mock_extractor = new MockBrowserFeatureExtractor(
      web_contents(),
      csd_service_.get());
  SetFeatureExtractor(mock_extractor);  // The host class takes ownership.

  ClientSideDetectionService::ClientReportPhishingRequestCallback cb;
  GURL phishing_url("http://phishingurl.com/");
  ClientPhishingRequest verdict;
  verdict.set_url(phishing_url.spec());
  verdict.set_client_score(1.0f);
  verdict.set_is_phishing(true);

  EXPECT_CALL(*mock_extractor, ExtractFeatures(_, _, _))
      .WillOnce(DoAll(DeleteArg<1>(),
                      InvokeCallbackArgument<2>(true, &verdict)));
  EXPECT_CALL(*csd_service_,
              SendClientReportPhishingRequest(
                  Pointee(PartiallyEqualVerdict(verdict)), _))
      .WillOnce(SaveArg<1>(&cb));
  OnPhishingDetectionDone(verdict.SerializeAsString());
  EXPECT_TRUE(Mock::VerifyAndClear(csd_service_.get()));
  ASSERT_FALSE(cb.is_null());

  // Set this back to a normal browser feature extractor since we're using
  // NavigateAndCommit() and it's easier to use the real thing than setting up
  // mock expectations.
  SetFeatureExtractor(new BrowserFeatureExtractor(web_contents(),
                                                  csd_service_.get()));
  GURL other_phishing_url("http://other_phishing_url.com/bla");
  ExpectPreClassificationChecks(other_phishing_url, &kFalse, &kFalse, &kFalse,
                                &kFalse, &kFalse, &kFalse);
  // We navigate away.  The callback cb should be revoked.
  NavigateAndCommit(other_phishing_url);
  // Wait for the pre-classification checks to finish for other_phishing_url.
  WaitAndCheckPreClassificationChecks();

  ClientSideDetectionService::ClientReportPhishingRequestCallback cb_other;
  verdict.set_url(other_phishing_url.spec());
  verdict.set_client_score(0.8f);
  EXPECT_CALL(*csd_service_,
              SendClientReportPhishingRequest(
                  Pointee(PartiallyEqualVerdict(verdict)), _))
      .WillOnce(DoAll(DeleteArg<0>(),
                      SaveArg<1>(&cb_other),
                      QuitUIMessageLoop()));
  std::vector<GURL> redirect_chain;
  redirect_chain.push_back(other_phishing_url);
  SetRedirectChain(redirect_chain);
  OnPhishingDetectionDone(verdict.SerializeAsString());
  base::MessageLoop::current()->Run();
  EXPECT_TRUE(Mock::VerifyAndClear(csd_service_.get()));
  ASSERT_FALSE(cb_other.is_null());

  // We expect that the interstitial is shown for the second phishing URL and
  // not for the first phishing URL.
  UnsafeResource resource;
  EXPECT_CALL(*ui_manager_.get(), DoDisplayBlockingPage(_))
      .WillOnce(SaveArg<0>(&resource));

  cb.Run(phishing_url, true);  // Should have no effect.
  cb_other.Run(other_phishing_url, true);  // Should show interstitial.

  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(Mock::VerifyAndClear(ui_manager_.get()));
  EXPECT_EQ(other_phishing_url, resource.url);
  EXPECT_EQ(other_phishing_url, resource.original_url);
  EXPECT_FALSE(resource.is_subresource);
  EXPECT_EQ(SB_THREAT_TYPE_CLIENT_SIDE_PHISHING_URL, resource.threat_type);
  EXPECT_EQ(web_contents()->GetRenderProcessHost()->GetID(),
            resource.render_process_host_id);
  EXPECT_EQ(web_contents()->GetRenderViewHost()->GetRoutingID(),
            resource.render_view_id);

  // Make sure the client object will be deleted.
  BrowserThread::PostTask(
      BrowserThread::IO,
      FROM_HERE,
      base::Bind(&MockSafeBrowsingUIManager::InvokeOnBlockingPageComplete,
                 ui_manager_, resource.callback));
}

TEST_F(ClientSideDetectionHostTest,
       OnPhishingDetectionDoneVerdictNotPhishing) {
  // Case 6: renderer sends a verdict string that isn't phishing.
  MockBrowserFeatureExtractor* mock_extractor = new MockBrowserFeatureExtractor(
      web_contents(),
      csd_service_.get());
  SetFeatureExtractor(mock_extractor);  // The host class takes ownership.

  ClientPhishingRequest verdict;
  verdict.set_url("http://not-phishing.com/");
  verdict.set_client_score(0.1f);
  verdict.set_is_phishing(false);

  EXPECT_CALL(*mock_extractor, ExtractFeatures(_, _, _)).Times(0);
  OnPhishingDetectionDone(verdict.SerializeAsString());
  EXPECT_TRUE(Mock::VerifyAndClear(mock_extractor));
}

TEST_F(ClientSideDetectionHostTest,
       OnPhishingDetectionDoneVerdictNotPhishingButSBMatch) {
  // Case 7: renderer sends a verdict string that isn't phishing but the URL
  // was on the regular phishing or malware lists.
  GURL url("http://not-phishing.com/");
  ClientPhishingRequest verdict;
  verdict.set_url(url.spec());
  verdict.set_client_score(0.1f);
  verdict.set_is_phishing(false);

  // First we have to navigate to the URL to set the unique page ID.
  ExpectPreClassificationChecks(url, &kFalse, &kFalse, &kFalse, &kFalse,
                                &kFalse, &kFalse);
  NavigateAndCommit(url);
  WaitAndCheckPreClassificationChecks();
  SetUnsafeResourceToCurrent();

  EXPECT_CALL(*csd_service_,
              SendClientReportPhishingRequest(
                  Pointee(PartiallyEqualVerdict(verdict)), CallbackIsNull()))
      .WillOnce(DoAll(DeleteArg<0>(), QuitUIMessageLoop()));
  std::vector<GURL> redirect_chain;
  redirect_chain.push_back(url);
  SetRedirectChain(redirect_chain);
  OnPhishingDetectionDone(verdict.SerializeAsString());
  base::MessageLoop::current()->Run();
  EXPECT_TRUE(Mock::VerifyAndClear(csd_service_.get()));
}

TEST_F(ClientSideDetectionHostTest, UpdateIPUrlMap) {
  BrowseInfo* browse_info = GetBrowseInfo();

  // Empty IP or host are skipped
  UpdateIPUrlMap("250.10.10.10", std::string());
  ASSERT_EQ(0U, browse_info->ips.size());
  UpdateIPUrlMap(std::string(), "http://google.com/a");
  ASSERT_EQ(0U, browse_info->ips.size());
  UpdateIPUrlMap(std::string(), std::string());
  ASSERT_EQ(0U, browse_info->ips.size());

  std::set<std::string> expected_urls;
  for (int i = 0; i < 20; i++) {
    std::string url = base::StringPrintf("http://%d.com/", i);
    expected_urls.insert(url);
    UpdateIPUrlMap("250.10.10.10", url);
  }
  ASSERT_EQ(1U, browse_info->ips.size());
  ASSERT_EQ(20U, browse_info->ips["250.10.10.10"].size());
  EXPECT_EQ(expected_urls, browse_info->ips["250.10.10.10"]);

  // Add more urls for this ip, it exceeds max limit and won't be added
  UpdateIPUrlMap("250.10.10.10", "http://21.com/");
  ASSERT_EQ(1U, browse_info->ips.size());
  ASSERT_EQ(20U, browse_info->ips["250.10.10.10"].size());
  EXPECT_EQ(expected_urls, browse_info->ips["250.10.10.10"]);

  // Add 199 more IPs
  for (int i = 0; i < 199; i++) {
    std::string ip = base::StringPrintf("%d.%d.%d.256", i, i, i);
    expected_urls.clear();
    expected_urls.insert("test.com/");
    UpdateIPUrlMap(ip, "test.com/");
    ASSERT_EQ(1U, browse_info->ips[ip].size());
    EXPECT_EQ(expected_urls, browse_info->ips[ip]);
  }
  ASSERT_EQ(200U, browse_info->ips.size());

  // Exceeding max ip limit 200, these won't be added
  UpdateIPUrlMap("250.250.250.250", "goo.com/");
  UpdateIPUrlMap("250.250.250.250", "bar.com/");
  UpdateIPUrlMap("250.250.0.250", "foo.com/");
  ASSERT_EQ(200U, browse_info->ips.size());

  // Add url to existing IPs succeed
  UpdateIPUrlMap("100.100.100.256", "more.com/");
  ASSERT_EQ(2U, browse_info->ips["100.100.100.256"].size());
  expected_urls.clear();
  expected_urls.insert("test.com/");
  expected_urls.insert("more.com/");
  EXPECT_EQ(expected_urls, browse_info->ips["100.100.100.256"]);
}

TEST_F(ClientSideDetectionHostTest,
       OnPhishingDetectionDoneVerdictNotPhishingNotMalwareIP) {
  // Case 7: renderer sends a verdict string that isn't phishing and not matches
  // malware bad IP list
  MockBrowserFeatureExtractor* mock_extractor = new MockBrowserFeatureExtractor(
      web_contents(),
      csd_service_.get());
  SetFeatureExtractor(mock_extractor);  // The host class takes ownership.

  ClientPhishingRequest verdict;
  verdict.set_url("http://not-phishing.com/");
  verdict.set_client_score(0.1f);
  verdict.set_is_phishing(false);

  ClientMalwareRequest malware_verdict;
  malware_verdict.set_url("http://not-phishing.com/");

  EXPECT_CALL(*mock_extractor, ExtractMalwareFeatures(_, _))
      .WillOnce(SetArgumentPointee<1>(malware_verdict));
  EXPECT_CALL(*csd_service_,
              SendClientReportMalwareRequest(_, _)).Times(0);
  EXPECT_CALL(*mock_extractor, ExtractFeatures(_, _, _)).Times(0);

  OnPhishingDetectionDone(verdict.SerializeAsString());
  EXPECT_TRUE(Mock::VerifyAndClear(mock_extractor));
}

TEST_F(ClientSideDetectionHostTest,
       OnPhishingDetectionDoneVerdictNotPhishingButMalwareIP) {
  // Case 8: renderer sends a verdict string that isn't phishing but matches
  // malware bad IP list
  MockBrowserFeatureExtractor* mock_extractor = new MockBrowserFeatureExtractor(
      web_contents(),
      csd_service_.get());
  SetFeatureExtractor(mock_extractor);  // The host class takes ownership.

  ClientPhishingRequest verdict;
  verdict.set_url("http://not-phishing.com/");
  verdict.set_client_score(0.1f);
  verdict.set_is_phishing(false);

  ClientMalwareRequest malware_verdict;
  malware_verdict.set_url("http://not-phishing.com/");
  ClientMalwareRequest::Feature* feature = malware_verdict.add_feature_map();
  feature->set_name("malwareip1.2.3.4");
  feature->set_value(1.0);
  feature->add_metainfo("badip.com");

  EXPECT_CALL(*mock_extractor, ExtractMalwareFeatures(_, _))
      .WillOnce(SetArgumentPointee<1>(malware_verdict));
  EXPECT_CALL(*csd_service_,
              SendClientReportMalwareRequest(
                  Pointee(PartiallyEqualMalwareVerdict(malware_verdict)), _))
      .WillOnce(DeleteArg<0>());
  EXPECT_CALL(*mock_extractor, ExtractFeatures(_, _, _)).Times(0);

  OnPhishingDetectionDone(verdict.SerializeAsString());
  EXPECT_TRUE(Mock::VerifyAndClear(mock_extractor));
}

TEST_F(ClientSideDetectionHostTest,
       OnPhishingDetectionDoneVerdictPhishingAndMalwareIP) {
  // Case 9: renderer sends a verdict string that is phishing and matches
  // malware bad IP list
  MockBrowserFeatureExtractor* mock_extractor = new MockBrowserFeatureExtractor(
      web_contents(),
      csd_service_.get());
  SetFeatureExtractor(mock_extractor);  // The host class takes ownership.

  ClientSideDetectionService::ClientReportPhishingRequestCallback cb;
  ClientPhishingRequest verdict;
  verdict.set_url("http://not-phishing.com/");
  verdict.set_client_score(0.1f);
  verdict.set_is_phishing(true);

  ClientMalwareRequest malware_verdict;
  malware_verdict.set_url("http://not-phishing.com/");
  ClientMalwareRequest::Feature* feature = malware_verdict.add_feature_map();
  feature->set_name("malwareip1.2.3.4");
  feature->set_value(1.0);
  feature->add_metainfo("badip.com");

  EXPECT_CALL(*mock_extractor, ExtractMalwareFeatures(_, _))
      .WillOnce(SetArgumentPointee<1>(malware_verdict));
  EXPECT_CALL(*csd_service_,
              SendClientReportMalwareRequest(
                  Pointee(PartiallyEqualMalwareVerdict(malware_verdict)), _))
      .WillOnce(DeleteArg<0>());

  EXPECT_CALL(*mock_extractor, ExtractFeatures(_, _, _))
      .WillOnce(DoAll(DeleteArg<1>(),
                      InvokeCallbackArgument<2>(true, &verdict)));

  EXPECT_CALL(*csd_service_,
              SendClientReportPhishingRequest(
                  Pointee(PartiallyEqualVerdict(verdict)), _))
      .WillOnce(SaveArg<1>(&cb));
  OnPhishingDetectionDone(verdict.SerializeAsString());
  EXPECT_TRUE(Mock::VerifyAndClear(mock_extractor));
  EXPECT_TRUE(Mock::VerifyAndClear(csd_service_.get()));
  ASSERT_FALSE(cb.is_null());
}

TEST_F(ClientSideDetectionHostTest, NavigationCancelsShouldClassifyUrl) {
  // Test that canceling pending should classify requests works as expected.

  GURL first_url("http://first.phishy.url.com");
  GURL second_url("http://second.url.com/");
  // The first few checks are done synchronously so check that they have been
  // done for the first URL, while the second URL has all the checks done.  We
  // need to manually set up the IsPrivateIPAddress mock since if the same mock
  // expectation is specified twice, gmock will only use the last instance of
  // it, meaning the first will never be matched.
  EXPECT_CALL(*csd_service_, IsPrivateIPAddress(_))
      .WillOnce(Return(false))
      .WillOnce(Return(false));
  ExpectPreClassificationChecks(first_url, NULL, &kFalse, &kFalse, NULL,
                                NULL, NULL);
  ExpectPreClassificationChecks(second_url, NULL, &kFalse, &kFalse, &kFalse,
                                &kFalse, &kFalse);

  NavigateAndCommit(first_url);
  // Don't flush the message loop, as we want to navigate to a different
  // url before the final pre-classification checks are run.
  NavigateAndCommit(second_url);
  WaitAndCheckPreClassificationChecks();
}

TEST_F(ClientSideDetectionHostTest, ShouldClassifyUrl) {
  // Navigate the tab to a page.  We should see a StartPhishingDetection IPC.
  GURL url("http://host.com/");
  ExpectPreClassificationChecks(url, &kFalse, &kFalse, &kFalse, &kFalse,
                                &kFalse, &kFalse);
  NavigateAndCommit(url);
  WaitAndCheckPreClassificationChecks();

  const IPC::Message* msg = process()->sink().GetFirstMessageMatching(
      SafeBrowsingMsg_StartPhishingDetection::ID);
  ASSERT_TRUE(msg);
  Tuple1<GURL> actual_url;
  SafeBrowsingMsg_StartPhishingDetection::Read(msg, &actual_url);
  EXPECT_EQ(url, actual_url.a);
  EXPECT_EQ(rvh()->GetRoutingID(), msg->routing_id());
  process()->sink().ClearMessages();

  // Now try an in-page navigation.  This should not trigger an IPC.
  EXPECT_CALL(*csd_service_, IsPrivateIPAddress(_)).Times(0);
  url = GURL("http://host.com/#foo");
  ExpectPreClassificationChecks(url, NULL, NULL, NULL, NULL, NULL, NULL);
  NavigateAndCommit(url);
  WaitAndCheckPreClassificationChecks();

  msg = process()->sink().GetFirstMessageMatching(
      SafeBrowsingMsg_StartPhishingDetection::ID);
  ASSERT_FALSE(msg);

  // Check that XHTML is supported, in addition to the default HTML type.
  // Note: for this test to work correctly, the new URL must be on the
  // same domain as the previous URL, otherwise it will create a new
  // RenderViewHost that won't have the mime type set.
  url = GURL("http://host.com/xhtml");
  rvh_tester()->SetContentsMimeType("application/xhtml+xml");
  ExpectPreClassificationChecks(url, &kFalse, &kFalse, &kFalse, &kFalse,
                                &kFalse, &kFalse);
  NavigateAndCommit(url);
  WaitAndCheckPreClassificationChecks();
  msg = process()->sink().GetFirstMessageMatching(
      SafeBrowsingMsg_StartPhishingDetection::ID);
  ASSERT_TRUE(msg);
  SafeBrowsingMsg_StartPhishingDetection::Read(msg, &actual_url);
  EXPECT_EQ(url, actual_url.a);
  EXPECT_EQ(rvh()->GetRoutingID(), msg->routing_id());
  process()->sink().ClearMessages();

  // Navigate to a new host, which should cause another IPC.
  url = GURL("http://host2.com/");
  ExpectPreClassificationChecks(url, &kFalse, &kFalse, &kFalse, &kFalse,
                                &kFalse, &kFalse);
  NavigateAndCommit(url);
  WaitAndCheckPreClassificationChecks();
  msg = process()->sink().GetFirstMessageMatching(
      SafeBrowsingMsg_StartPhishingDetection::ID);
  ASSERT_TRUE(msg);
  SafeBrowsingMsg_StartPhishingDetection::Read(msg, &actual_url);
  EXPECT_EQ(url, actual_url.a);
  EXPECT_EQ(rvh()->GetRoutingID(), msg->routing_id());
  process()->sink().ClearMessages();

  // If the mime type is not one that we support, no IPC should be triggered.
  // Note: for this test to work correctly, the new URL must be on the
  // same domain as the previous URL, otherwise it will create a new
  // RenderViewHost that won't have the mime type set.
  url = GURL("http://host2.com/image.jpg");
  rvh_tester()->SetContentsMimeType("image/jpeg");
  ExpectPreClassificationChecks(url, NULL, NULL, NULL, NULL, NULL, NULL);
  NavigateAndCommit(url);
  WaitAndCheckPreClassificationChecks();
  msg = process()->sink().GetFirstMessageMatching(
      SafeBrowsingMsg_StartPhishingDetection::ID);
  ASSERT_FALSE(msg);

  // If IsPrivateIPAddress returns true, no IPC should be triggered.
  url = GURL("http://host3.com/");
  ExpectPreClassificationChecks(url, &kTrue, NULL, NULL, NULL, NULL, NULL);
  NavigateAndCommit(url);
  WaitAndCheckPreClassificationChecks();
  msg = process()->sink().GetFirstMessageMatching(
      SafeBrowsingMsg_StartPhishingDetection::ID);
  ASSERT_FALSE(msg);

  // If the tab is incognito there should be no IPC.  Also, we shouldn't
  // even check the csd-whitelist.
  url = GURL("http://host4.com/");
  ExpectPreClassificationChecks(url, &kFalse, &kTrue, NULL, NULL, NULL, NULL);
  NavigateAndCommit(url);
  WaitAndCheckPreClassificationChecks();
  msg = process()->sink().GetFirstMessageMatching(
      SafeBrowsingMsg_StartPhishingDetection::ID);
  ASSERT_FALSE(msg);

  // If the URL is on the csd whitelist, no IPC should be triggered.
  url = GURL("http://host5.com/");
  ExpectPreClassificationChecks(url, &kFalse, &kFalse, &kTrue, NULL, NULL,
                                NULL);
  NavigateAndCommit(url);
  WaitAndCheckPreClassificationChecks();
  msg = process()->sink().GetFirstMessageMatching(
      SafeBrowsingMsg_StartPhishingDetection::ID);
  ASSERT_FALSE(msg);

  // If item is in the cache but it isn't valid, we will classify regardless
  // of whether we are over the reporting limit.
  url = GURL("http://host6.com/");
  ExpectPreClassificationChecks(url, &kFalse, &kFalse, &kFalse, &kFalse, &kTrue,
                                NULL);
  NavigateAndCommit(url);
  WaitAndCheckPreClassificationChecks();
  msg = process()->sink().GetFirstMessageMatching(
      SafeBrowsingMsg_StartPhishingDetection::ID);
  ASSERT_TRUE(msg);
  SafeBrowsingMsg_StartPhishingDetection::Read(msg, &actual_url);
  EXPECT_EQ(url, actual_url.a);
  EXPECT_EQ(rvh()->GetRoutingID(), msg->routing_id());
  process()->sink().ClearMessages();

  // If the url isn't in the cache and we are over the reporting limit, we
  // don't do classification.
  url = GURL("http://host7.com/");
  ExpectPreClassificationChecks(url, &kFalse, &kFalse, &kFalse, &kFalse,
                                &kFalse, &kTrue);
  NavigateAndCommit(url);
  WaitAndCheckPreClassificationChecks();
  msg = process()->sink().GetFirstMessageMatching(
      SafeBrowsingMsg_StartPhishingDetection::ID);
  ASSERT_FALSE(msg);

  // If result is cached, we will try and display the blocking page directly
  // with no start classification message.
  url = GURL("http://host8.com/");
  ExpectPreClassificationChecks(url, &kFalse, &kFalse, &kFalse, &kTrue, NULL,
                                NULL);

  UnsafeResource resource;
  EXPECT_CALL(*ui_manager_.get(), DoDisplayBlockingPage(_))
      .WillOnce(SaveArg<0>(&resource));

  NavigateAndCommit(url);
  // Wait for CheckCsdWhitelist and CheckCache() to be called.
  base::RunLoop().RunUntilIdle();
  // Now we check that all expected functions were indeed called on the two
  // service objects.
  EXPECT_TRUE(Mock::VerifyAndClear(csd_service_.get()));
  EXPECT_TRUE(Mock::VerifyAndClear(ui_manager_.get()));
  EXPECT_EQ(url, resource.url);
  EXPECT_EQ(url, resource.original_url);
  resource.callback.Reset();
  msg = process()->sink().GetFirstMessageMatching(
      SafeBrowsingMsg_StartPhishingDetection::ID);
  ASSERT_FALSE(msg);
}

}  // namespace safe_browsing
