// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/safe_browsing/browser_feature_extractor.h"

#include <map>
#include <string>
#include <vector>

#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "chrome/browser/history/history_backend.h"
#include "chrome/browser/history/history_service.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/safe_browsing/browser_features.h"
#include "chrome/browser/safe_browsing/client_side_detection_service.h"
#include "chrome/browser/safe_browsing/ui_manager.h"
#include "chrome/common/safe_browsing/csd.pb.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/page_transition_types.h"
#include "content/public/common/referrer.h"
#include "content/public/test/test_browser_thread.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

using content::WebContentsTester;
using testing::Return;
using testing::StrictMock;

namespace safe_browsing {
namespace {
class MockClientSideDetectionService : public ClientSideDetectionService {
 public:
  MockClientSideDetectionService() : ClientSideDetectionService(NULL) {}
  virtual ~MockClientSideDetectionService() {};

  MOCK_CONST_METHOD1(IsBadIpAddress, bool(const std::string&));
};
}  // namespace

class BrowserFeatureExtractorTest : public ChromeRenderViewHostTestHarness {
 protected:
  virtual void SetUp() {
    ChromeRenderViewHostTestHarness::SetUp();
    ASSERT_TRUE(profile()->CreateHistoryService(
        true /* delete_file */, false /* no_db */));
    service_.reset(new StrictMock<MockClientSideDetectionService>());
    extractor_.reset(
        new BrowserFeatureExtractor(web_contents(), service_.get()));
    num_pending_ = 0;
    browse_info_.reset(new BrowseInfo);
  }

  virtual void TearDown() {
    extractor_.reset();
    profile()->DestroyHistoryService();
    ChromeRenderViewHostTestHarness::TearDown();
    ASSERT_EQ(0, num_pending_);
  }

  HistoryService* history_service() {
    return HistoryServiceFactory::GetForProfile(profile(),
                                                Profile::EXPLICIT_ACCESS);
  }

  void SetRedirectChain(const std::vector<GURL>& redirect_chain,
                        bool new_host) {
    browse_info_->url_redirects = redirect_chain;
    if (new_host) {
      browse_info_->host_redirects = redirect_chain;
    }
  }

  // Wrapper around NavigateAndCommit that also sets the redirect chain to
  // a sane value.
  void SimpleNavigateAndCommit(const GURL& url) {
    std::vector<GURL> redirect_chain;
    redirect_chain.push_back(url);
    SetRedirectChain(redirect_chain, true);
    NavigateAndCommit(url, GURL(), content::PAGE_TRANSITION_LINK);
  }

  // This is similar to NavigateAndCommit that is in WebContentsTester, but
  // allows us to specify the referrer and page_transition_type.
  void NavigateAndCommit(const GURL& url,
                         const GURL& referrer,
                         content::PageTransition type) {
    web_contents()->GetController().LoadURL(
        url, content::Referrer(referrer, WebKit::WebReferrerPolicyDefault),
        type, std::string());

    static int page_id = 0;
    content::RenderViewHost* rvh =
        WebContentsTester::For(web_contents())->GetPendingRenderViewHost();
    if (!rvh) {
      rvh = web_contents()->GetRenderViewHost();
    }
    WebContentsTester::For(web_contents())->ProceedWithCrossSiteNavigation();
    WebContentsTester::For(web_contents())->TestDidNavigateWithReferrer(
        rvh, ++page_id, url,
        content::Referrer(referrer, WebKit::WebReferrerPolicyDefault), type);
  }

  bool ExtractFeatures(ClientPhishingRequest* request) {
    StartExtractFeatures(request);
    base::MessageLoop::current()->Run();
    EXPECT_EQ(1U, success_.count(request));
    return success_.count(request) ? success_[request] : false;
  }

  void StartExtractFeatures(ClientPhishingRequest* request) {
    success_.erase(request);
    ++num_pending_;
    extractor_->ExtractFeatures(
        browse_info_.get(),
        request,
        base::Bind(&BrowserFeatureExtractorTest::ExtractFeaturesDone,
                   base::Unretained(this)));
  }

  void GetFeatureMap(const ClientPhishingRequest& request,
                     std::map<std::string, double>* features) {
    for (int i = 0; i < request.non_model_feature_map_size(); ++i) {
      const ClientPhishingRequest::Feature& feature =
          request.non_model_feature_map(i);
      EXPECT_EQ(0U, features->count(feature.name()));
      (*features)[feature.name()] = feature.value();
    }
  }

  void ExtractMalwareFeatures(ClientMalwareRequest* request) {
    extractor_->ExtractMalwareFeatures(
        browse_info_.get(), request);
  }

  void GetMalwareFeatureMap(
      const ClientMalwareRequest& request,
      std::map<std::string, std::set<std::string> >* features) {
    for (int i = 0; i < request.feature_map_size(); ++i) {
      const ClientMalwareRequest::Feature& feature =
          request.feature_map(i);
      EXPECT_EQ(0U, features->count(feature.name()));
      std::set<std::string> meta_infos;
      for (int j = 0; j < feature.metainfo_size(); ++j) {
        meta_infos.insert(feature.metainfo(j));
      }
      (*features)[feature.name()] = meta_infos;
    }
  }

  int num_pending_;
  scoped_ptr<BrowserFeatureExtractor> extractor_;
  std::map<ClientPhishingRequest*, bool> success_;
  scoped_ptr<BrowseInfo> browse_info_;
  scoped_ptr<MockClientSideDetectionService> service_;

 private:
  void ExtractFeaturesDone(bool success, ClientPhishingRequest* request) {
    ASSERT_EQ(0U, success_.count(request));
    success_[request] = success;
    if (--num_pending_ == 0) {
      base::MessageLoop::current()->Quit();
    }
  }
};

TEST_F(BrowserFeatureExtractorTest, UrlNotInHistory) {
  ClientPhishingRequest request;
  SimpleNavigateAndCommit(GURL("http://www.google.com"));
  request.set_url("http://www.google.com/");
  request.set_client_score(0.5);
  EXPECT_FALSE(ExtractFeatures(&request));
}

TEST_F(BrowserFeatureExtractorTest, RequestNotInitialized) {
  ClientPhishingRequest request;
  request.set_url("http://www.google.com/");
  // Request is missing the score value.
  SimpleNavigateAndCommit(GURL("http://www.google.com"));
  EXPECT_FALSE(ExtractFeatures(&request));
}

TEST_F(BrowserFeatureExtractorTest, UrlInHistory) {
  history_service()->AddPage(GURL("http://www.foo.com/bar.html"),
                             base::Time::Now(),
                             history::SOURCE_BROWSED);
  history_service()->AddPage(GURL("https://www.foo.com/gaa.html"),
                             base::Time::Now(),
                             history::SOURCE_BROWSED);  // same host HTTPS.
  history_service()->AddPage(GURL("http://www.foo.com/gaa.html"),
                             base::Time::Now(),
                             history::SOURCE_BROWSED);  // same host HTTP.
  history_service()->AddPage(GURL("http://bar.foo.com/gaa.html"),
                             base::Time::Now(),
                             history::SOURCE_BROWSED);  // different host.
  history_service()->AddPage(GURL("http://www.foo.com/bar.html?a=b"),
                             base::Time::Now() - base::TimeDelta::FromHours(23),
                             NULL, 0, GURL(), history::RedirectList(),
                             content::PAGE_TRANSITION_LINK,
                             history::SOURCE_BROWSED, false);
  history_service()->AddPage(GURL("http://www.foo.com/bar.html"),
                             base::Time::Now() - base::TimeDelta::FromHours(25),
                             NULL, 0, GURL(), history::RedirectList(),
                             content::PAGE_TRANSITION_TYPED,
                             history::SOURCE_BROWSED, false);
  history_service()->AddPage(GURL("https://www.foo.com/goo.html"),
                             base::Time::Now() - base::TimeDelta::FromDays(5),
                             NULL, 0, GURL(), history::RedirectList(),
                             content::PAGE_TRANSITION_TYPED,
                             history::SOURCE_BROWSED, false);

  SimpleNavigateAndCommit(GURL("http://www.foo.com/bar.html"));

  ClientPhishingRequest request;
  request.set_url("http://www.foo.com/bar.html");
  request.set_client_score(0.5);
  EXPECT_TRUE(ExtractFeatures(&request));
  std::map<std::string, double> features;
  GetFeatureMap(request, &features);

  EXPECT_EQ(12U, features.size());
  EXPECT_DOUBLE_EQ(2.0, features[features::kUrlHistoryVisitCount]);
  EXPECT_DOUBLE_EQ(1.0,
                   features[features::kUrlHistoryVisitCountMoreThan24hAgo]);
  EXPECT_DOUBLE_EQ(1.0, features[features::kUrlHistoryTypedCount]);
  EXPECT_DOUBLE_EQ(1.0, features[features::kUrlHistoryLinkCount]);
  EXPECT_DOUBLE_EQ(4.0, features[features::kHttpHostVisitCount]);
  EXPECT_DOUBLE_EQ(2.0, features[features::kHttpsHostVisitCount]);
  EXPECT_DOUBLE_EQ(1.0, features[features::kFirstHttpHostVisitMoreThan24hAgo]);
  EXPECT_DOUBLE_EQ(1.0, features[features::kFirstHttpsHostVisitMoreThan24hAgo]);

  request.Clear();
  request.set_url("http://bar.foo.com/gaa.html");
  request.set_client_score(0.5);
  EXPECT_TRUE(ExtractFeatures(&request));
  features.clear();
  GetFeatureMap(request, &features);
  // We have less features because we didn't Navigate to this page, so we don't
  // have Referrer, IsFirstNavigation, HasSSLReferrer, etc.
  EXPECT_EQ(7U, features.size());
  EXPECT_DOUBLE_EQ(1.0, features[features::kUrlHistoryVisitCount]);
  EXPECT_DOUBLE_EQ(0.0,
                   features[features::kUrlHistoryVisitCountMoreThan24hAgo]);
  EXPECT_DOUBLE_EQ(0.0, features[features::kUrlHistoryTypedCount]);
  EXPECT_DOUBLE_EQ(1.0, features[features::kUrlHistoryLinkCount]);
  EXPECT_DOUBLE_EQ(1.0, features[features::kHttpHostVisitCount]);
  EXPECT_DOUBLE_EQ(0.0, features[features::kHttpsHostVisitCount]);
  EXPECT_DOUBLE_EQ(0.0, features[features::kFirstHttpHostVisitMoreThan24hAgo]);
  EXPECT_FALSE(features.count(features::kFirstHttpsHostVisitMoreThan24hAgo));
}

TEST_F(BrowserFeatureExtractorTest, MultipleRequestsAtOnce) {
  history_service()->AddPage(GURL("http://www.foo.com/bar.html"),
                             base::Time::Now(),
                             history::SOURCE_BROWSED);
  SimpleNavigateAndCommit(GURL("http:/www.foo.com/bar.html"));
  ClientPhishingRequest request;
  request.set_url("http://www.foo.com/bar.html");
  request.set_client_score(0.5);
  StartExtractFeatures(&request);

  SimpleNavigateAndCommit(GURL("http://www.foo.com/goo.html"));
  ClientPhishingRequest request2;
  request2.set_url("http://www.foo.com/goo.html");
  request2.set_client_score(1.0);
  StartExtractFeatures(&request2);

  base::MessageLoop::current()->Run();
  EXPECT_TRUE(success_[&request]);
  // Success is false because the second URL is not in the history and we are
  // not able to distinguish between a missing URL in the history and an error.
  EXPECT_FALSE(success_[&request2]);
}

TEST_F(BrowserFeatureExtractorTest, BrowseFeatures) {
  history_service()->AddPage(GURL("http://www.foo.com/"),
                             base::Time::Now(),
                             history::SOURCE_BROWSED);
  history_service()->AddPage(GURL("http://www.foo.com/page.html"),
                             base::Time::Now(),
                             history::SOURCE_BROWSED);
  history_service()->AddPage(GURL("http://www.bar.com/"),
                             base::Time::Now(),
                             history::SOURCE_BROWSED);
  history_service()->AddPage(GURL("http://www.bar.com/other_page.html"),
                             base::Time::Now(),
                             history::SOURCE_BROWSED);
  history_service()->AddPage(GURL("http://www.baz.com/"),
                             base::Time::Now(),
                             history::SOURCE_BROWSED);

  ClientPhishingRequest request;
  request.set_url("http://www.foo.com/");
  request.set_client_score(0.5);
  std::vector<GURL> redirect_chain;
  redirect_chain.push_back(GURL("http://somerandomwebsite.com/"));
  redirect_chain.push_back(GURL("http://www.foo.com/"));
  SetRedirectChain(redirect_chain, true);
  browse_info_->http_status_code = 200;
  NavigateAndCommit(GURL("http://www.foo.com/"),
                    GURL("http://google.com/"),
                    content::PageTransitionFromInt(
                        content::PAGE_TRANSITION_AUTO_BOOKMARK |
                        content::PAGE_TRANSITION_FORWARD_BACK));

  EXPECT_TRUE(ExtractFeatures(&request));
  std::map<std::string, double> features;
  GetFeatureMap(request, &features);

  EXPECT_EQ(1.0,
            features[base::StringPrintf("%s=%s",
                                        features::kReferrer,
                                        "http://google.com/")]);
  EXPECT_EQ(1.0,
            features[base::StringPrintf("%s[0]=%s",
                                        features::kRedirect,
                                        "http://somerandomwebsite.com/")]);
  // We shouldn't have a feature for the last redirect in the chain, since it
  // should always be the URL that we navigated to.
  EXPECT_EQ(0.0,
            features[base::StringPrintf("%s[1]=%s",
                                        features::kRedirect,
                                        "http://foo.com/")]);
  EXPECT_EQ(0.0, features[features::kHasSSLReferrer]);
  EXPECT_EQ(2.0, features[features::kPageTransitionType]);
  EXPECT_EQ(1.0, features[features::kIsFirstNavigation]);
  EXPECT_EQ(200.0, features[features::kHttpStatusCode]);

  request.Clear();
  request.set_url("http://www.foo.com/page.html");
  request.set_client_score(0.5);
  redirect_chain.clear();
  redirect_chain.push_back(GURL("http://www.foo.com/redirect"));
  redirect_chain.push_back(GURL("http://www.foo.com/second_redirect"));
  redirect_chain.push_back(GURL("http://www.foo.com/page.html"));
  SetRedirectChain(redirect_chain, false);
  browse_info_->http_status_code = 404;
  NavigateAndCommit(GURL("http://www.foo.com/page.html"),
                    GURL("http://www.foo.com"),
                    content::PageTransitionFromInt(
                        content::PAGE_TRANSITION_TYPED |
                        content::PAGE_TRANSITION_CHAIN_START |
                        content::PAGE_TRANSITION_CLIENT_REDIRECT));

  EXPECT_TRUE(ExtractFeatures(&request));
  features.clear();
  GetFeatureMap(request, &features);

  EXPECT_EQ(1,
            features[base::StringPrintf("%s=%s",
                                        features::kReferrer,
                                        "http://www.foo.com/")]);
  EXPECT_EQ(1.0,
            features[base::StringPrintf("%s[0]=%s",
                                        features::kRedirect,
                                        "http://www.foo.com/redirect")]);
  EXPECT_EQ(1.0,
            features[base::StringPrintf("%s[1]=%s",
                                        features::kRedirect,
                                        "http://www.foo.com/second_redirect")]);
  EXPECT_EQ(0.0, features[features::kHasSSLReferrer]);
  EXPECT_EQ(1.0, features[features::kPageTransitionType]);
  EXPECT_EQ(0.0, features[features::kIsFirstNavigation]);
  EXPECT_EQ(1.0,
            features[base::StringPrintf("%s%s=%s",
                                        features::kHostPrefix,
                                        features::kReferrer,
                                        "http://google.com/")]);
  EXPECT_EQ(1.0,
            features[base::StringPrintf("%s%s[0]=%s",
                                        features::kHostPrefix,
                                        features::kRedirect,
                                        "http://somerandomwebsite.com/")]);
  EXPECT_EQ(2.0,
            features[base::StringPrintf("%s%s",
                                        features::kHostPrefix,
                                        features::kPageTransitionType)]);
  EXPECT_EQ(1.0,
            features[base::StringPrintf("%s%s",
                                        features::kHostPrefix,
                                        features::kIsFirstNavigation)]);
  EXPECT_EQ(404.0, features[features::kHttpStatusCode]);

  request.Clear();
  request.set_url("http://www.bar.com/");
  request.set_client_score(0.5);
  redirect_chain.clear();
  redirect_chain.push_back(GURL("http://www.foo.com/page.html"));
  redirect_chain.push_back(GURL("http://www.bar.com/"));
  SetRedirectChain(redirect_chain, true);
  NavigateAndCommit(GURL("http://www.bar.com/"),
                    GURL("http://www.foo.com/page.html"),
                    content::PageTransitionFromInt(
                        content::PAGE_TRANSITION_LINK |
                        content::PAGE_TRANSITION_CHAIN_END |
                        content::PAGE_TRANSITION_CLIENT_REDIRECT));

  EXPECT_TRUE(ExtractFeatures(&request));
  features.clear();
  GetFeatureMap(request, &features);

  EXPECT_EQ(1.0,
            features[base::StringPrintf("%s=%s",
                                        features::kReferrer,
                                        "http://www.foo.com/page.html")]);
  EXPECT_EQ(1.0,
            features[base::StringPrintf("%s[0]=%s",
                                        features::kRedirect,
                                        "http://www.foo.com/page.html")]);
  EXPECT_EQ(0.0, features[features::kHasSSLReferrer]);
  EXPECT_EQ(0.0, features[features::kPageTransitionType]);
  EXPECT_EQ(0.0, features[features::kIsFirstNavigation]);

  // Should not have host features.
  EXPECT_EQ(0U,
            features.count(base::StringPrintf("%s%s",
                                              features::kHostPrefix,
                                              features::kPageTransitionType)));
  EXPECT_EQ(0U,
            features.count(base::StringPrintf("%s%s",
                                              features::kHostPrefix,
                                              features::kIsFirstNavigation)));

  request.Clear();
  request.set_url("http://www.bar.com/other_page.html");
  request.set_client_score(0.5);
  redirect_chain.clear();
  redirect_chain.push_back(GURL("http://www.bar.com/other_page.html"));
  SetRedirectChain(redirect_chain, false);
  NavigateAndCommit(GURL("http://www.bar.com/other_page.html"),
                    GURL("http://www.bar.com/"),
                    content::PAGE_TRANSITION_LINK);

  EXPECT_TRUE(ExtractFeatures(&request));
  features.clear();
  GetFeatureMap(request, &features);

  EXPECT_EQ(1.0,
            features[base::StringPrintf("%s=%s",
                                        features::kReferrer,
                                        "http://www.bar.com/")]);
  EXPECT_EQ(0.0, features[features::kHasSSLReferrer]);
  EXPECT_EQ(0.0, features[features::kPageTransitionType]);
  EXPECT_EQ(0.0, features[features::kIsFirstNavigation]);
  EXPECT_EQ(1.0,
            features[base::StringPrintf("%s%s=%s",
                                        features::kHostPrefix,
                                        features::kReferrer,
                                        "http://www.foo.com/page.html")]);
  EXPECT_EQ(1.0,
            features[base::StringPrintf("%s%s[0]=%s",
                                        features::kHostPrefix,
                                        features::kRedirect,
                                        "http://www.foo.com/page.html")]);
  EXPECT_EQ(0.0,
            features[base::StringPrintf("%s%s",
                                        features::kHostPrefix,
                                        features::kPageTransitionType)]);
  EXPECT_EQ(0.0,
            features[base::StringPrintf("%s%s",
                                        features::kHostPrefix,
                                        features::kIsFirstNavigation)]);
  request.Clear();
  request.set_url("http://www.baz.com/");
  request.set_client_score(0.5);
  redirect_chain.clear();
  redirect_chain.push_back(GURL("https://bankofamerica.com"));
  redirect_chain.push_back(GURL("http://www.baz.com/"));
  SetRedirectChain(redirect_chain, true);
  NavigateAndCommit(GURL("http://www.baz.com"),
                    GURL("https://bankofamerica.com"),
                    content::PAGE_TRANSITION_GENERATED);

  std::set<std::string> urls;
  urls.insert("http://test.com");
  browse_info_->ips.insert(std::make_pair("193.5.163.8", urls));
  browse_info_->ips.insert(std::make_pair("23.94.78.1", urls));
  EXPECT_CALL(*service_, IsBadIpAddress("193.5.163.8")).WillOnce(Return(true));
  EXPECT_CALL(*service_, IsBadIpAddress("23.94.78.1")).WillOnce(Return(false));

  EXPECT_TRUE(ExtractFeatures(&request));
  features.clear();
  GetFeatureMap(request, &features);

  EXPECT_EQ(1.0,
            features[base::StringPrintf("%s[0]=%s",
                                        features::kRedirect,
                                        features::kSecureRedirectValue)]);
  EXPECT_EQ(1.0, features[features::kHasSSLReferrer]);
  EXPECT_EQ(5.0, features[features::kPageTransitionType]);
  // Should not have redirect or host features.
  EXPECT_EQ(0U,
            features.count(base::StringPrintf("%s%s",
                                              features::kHostPrefix,
                                              features::kPageTransitionType)));
  EXPECT_EQ(0U,
            features.count(base::StringPrintf("%s%s",
                                              features::kHostPrefix,
                                              features::kIsFirstNavigation)));
  EXPECT_EQ(5.0, features[features::kPageTransitionType]);
  EXPECT_EQ(1.0, features[std::string(features::kBadIpFetch) + "193.5.163.8"]);
  EXPECT_FALSE(features.count(std::string(features::kBadIpFetch) +
                              "23.94.78.1"));
}

TEST_F(BrowserFeatureExtractorTest, SafeBrowsingFeatures) {
  SimpleNavigateAndCommit(GURL("http://www.foo.com/malware.html"));
  ClientPhishingRequest request;
  request.set_url("http://www.foo.com/malware.html");
  request.set_client_score(0.5);

  browse_info_->unsafe_resource.reset(
      new SafeBrowsingUIManager::UnsafeResource);
  browse_info_->unsafe_resource->url = GURL("http://www.malware.com/");
  browse_info_->unsafe_resource->original_url = GURL("http://www.good.com/");
  browse_info_->unsafe_resource->is_subresource = true;
  browse_info_->unsafe_resource->threat_type = SB_THREAT_TYPE_URL_MALWARE;

  ExtractFeatures(&request);
  std::map<std::string, double> features;
  GetFeatureMap(request, &features);
  EXPECT_TRUE(features.count(base::StringPrintf(
      "%s%s",
      features::kSafeBrowsingMaliciousUrl,
      "http://www.malware.com/")));
  EXPECT_TRUE(features.count(base::StringPrintf(
      "%s%s",
       features::kSafeBrowsingOriginalUrl,
        "http://www.good.com/")));
  EXPECT_DOUBLE_EQ(1.0, features[features::kSafeBrowsingIsSubresource]);
  EXPECT_DOUBLE_EQ(2.0, features[features::kSafeBrowsingThreatType]);
}

TEST_F(BrowserFeatureExtractorTest, MalwareFeatures) {
  ClientMalwareRequest request;
  request.set_url("http://www.foo.com/");

  std::set<std::string> bad_urls;
  bad_urls.insert("http://bad.com");
  bad_urls.insert("http://evil.com");
  browse_info_->ips.insert(std::make_pair("193.5.163.8", bad_urls));
  browse_info_->ips.insert(std::make_pair("92.92.92.92", bad_urls));
  std::set<std::string> good_urls;
  good_urls.insert("http://ok.com");
  browse_info_->ips.insert(std::make_pair("23.94.78.1", good_urls));
  EXPECT_CALL(*service_, IsBadIpAddress("193.5.163.8")).WillOnce(Return(true));
  EXPECT_CALL(*service_, IsBadIpAddress("92.92.92.92")).WillOnce(Return(true));
  EXPECT_CALL(*service_, IsBadIpAddress("23.94.78.1")).WillOnce(Return(false));

  ExtractMalwareFeatures(&request);
  std::map<std::string, std::set<std::string> > features;
  GetMalwareFeatureMap(request, &features);

  EXPECT_EQ(2U, features.size());
  std::string feature_name = base::StringPrintf("%s%s", features::kBadIpFetch,
                                                "193.5.163.8");
  EXPECT_TRUE(features.count(feature_name));
  std::set<std::string> urls = features[feature_name];
  EXPECT_EQ(2U, urls.size());
  EXPECT_TRUE(urls.find("http://bad.com") != urls.end());
  EXPECT_TRUE(urls.find("http://evil.com") != urls.end());
  feature_name = base::StringPrintf("%s%s", features::kBadIpFetch,
                                    "92.92.92.92");
  EXPECT_TRUE(features.count(feature_name));
  urls = features[feature_name];
  EXPECT_EQ(2U, urls.size());
  EXPECT_TRUE(urls.find("http://bad.com") != urls.end());
  EXPECT_TRUE(urls.find("http://evil.com") != urls.end());
}

TEST_F(BrowserFeatureExtractorTest, MalwareFeatures_ExceedLimit) {
  ClientMalwareRequest request;
  request.set_url("http://www.foo.com/");

  std::set<std::string> bad_urls;
  bad_urls.insert("http://bad.com");
  std::vector<std::string> ips;
  for (int i = 0; i < 7; ++i) {  // Add 7 ips
    std::string ip = base::StringPrintf("%d.%d.%d.%d", i, i, i, i);
    ips.push_back(ip);
    browse_info_->ips.insert(std::make_pair(ip, bad_urls));
  }

  // First ip is good, then check the next 5 bad ips.
  // Not check the 7th as reached limit.
  EXPECT_CALL(*service_, IsBadIpAddress(ips[0])).WillOnce(Return(false));
  for (int i = 1; i < 6; ++i) {
    EXPECT_CALL(*service_, IsBadIpAddress(ips[i])).WillOnce(Return(true));
  }

  ExtractMalwareFeatures(&request);
  std::map<std::string, std::set<std::string> > features;
  GetMalwareFeatureMap(request, &features);

  // Only keep 5 ips.
  EXPECT_EQ(5U, features.size());
}

}  // namespace safe_browsing
