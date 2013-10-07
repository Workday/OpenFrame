// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/captive_portal/captive_portal_detector.h"

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/run_loop.h"
#include "base/time/time.h"
#include "chrome/browser/captive_portal/testing_utils.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "net/base/net_errors.h"
#include "net/url_request/url_fetcher.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace captive_portal {

namespace {

class CaptivePortalClient {
 public:
  explicit CaptivePortalClient(CaptivePortalDetector* captive_portal_detector)
      : captive_portal_detector_(captive_portal_detector),
        num_results_received_(0) {
  }

  void OnPortalDetectionCompleted(
      const CaptivePortalDetector::Results& results) {
    results_ = results;
    ++num_results_received_;
  }

  const CaptivePortalDetector::Results& captive_portal_results() const {
    return results_;
  }

  int num_results_received() const { return num_results_received_; }

 private:
  CaptivePortalDetector* captive_portal_detector_;

  CaptivePortalDetector::Results results_;
  int num_results_received_;

  DISALLOW_COPY_AND_ASSIGN(CaptivePortalClient);
};

}  // namespace

class CaptivePortalDetectorTest : public testing::Test,
                                  public CaptivePortalDetectorTestBase {
 public:
  CaptivePortalDetectorTest() : detector_(profile_.GetRequestContext()) {
    set_detector(&detector_);
  }

  virtual ~CaptivePortalDetectorTest() {}

  void RunTest(const CaptivePortalDetector::Results& expected_results,
               int net_error,
               int status_code,
               const char* response_headers) {
    ASSERT_FALSE(FetchingURL());

    GURL url(CaptivePortalDetector::kDefaultURL);
    CaptivePortalClient client(detector());

    detector()->DetectCaptivePortal(url,
        base::Bind(&CaptivePortalClient::OnPortalDetectionCompleted,
                   base::Unretained(&client)));

    ASSERT_TRUE(FetchingURL());
    base::RunLoop().RunUntilIdle();

    CompleteURLFetch(net_error, status_code, response_headers);

    EXPECT_FALSE(FetchingURL());
    EXPECT_EQ(1, client.num_results_received());
    EXPECT_EQ(expected_results.result, client.captive_portal_results().result);
    EXPECT_EQ(expected_results.response_code,
              client.captive_portal_results().response_code);
    EXPECT_EQ(expected_results.retry_after_delta,
              client.captive_portal_results().retry_after_delta);
  }

  void RunCancelTest() {
    ASSERT_FALSE(FetchingURL());

    GURL url(CaptivePortalDetector::kDefaultURL);
    CaptivePortalClient client(detector());

    detector()->DetectCaptivePortal(url,
        base::Bind(&CaptivePortalClient::OnPortalDetectionCompleted,
                   base::Unretained(&client)));

    ASSERT_TRUE(FetchingURL());
    base::RunLoop().RunUntilIdle();

    detector()->Cancel();

    ASSERT_FALSE(FetchingURL());
    EXPECT_EQ(0, client.num_results_received());
  }

 private:
  content::TestBrowserThreadBundle thread_bundle_;

  // Definition order does matter.
  TestingProfile profile_;
  CaptivePortalDetector detector_;
};

// Test that the CaptivePortalDetector returns the expected result
// codes in response to a variety of probe results.
TEST_F(CaptivePortalDetectorTest, CaptivePortalResultCodes) {
  CaptivePortalDetector::Results results;
  results.result = RESULT_INTERNET_CONNECTED;
  results.response_code = 204;

  RunTest(results, net::OK, 204, NULL);

  // The server may return an HTTP error when it's acting up.
  results.result = RESULT_NO_RESPONSE;
  results.response_code = 500;
  RunTest(results, net::OK, 500, NULL);

  // Generic network error case.
  results.result = RESULT_NO_RESPONSE;
  results.response_code = net::URLFetcher::RESPONSE_CODE_INVALID;
  RunTest(results, net::ERR_TIMED_OUT, net::URLFetcher::RESPONSE_CODE_INVALID,
          NULL);

  // In the general captive portal case, the portal will return a page with a
  // 200 status.
  results.result = RESULT_BEHIND_CAPTIVE_PORTAL;
  results.response_code = 200;
  RunTest(results, net::OK, 200, NULL);

  // Some captive portals return 511 instead, to advertise their captive
  // portal-ness.
  results.result = RESULT_BEHIND_CAPTIVE_PORTAL;
  results.response_code = 511;
  RunTest(results, net::OK, 511, NULL);
}

// Check a Retry-After header that contains a delay in seconds.
TEST_F(CaptivePortalDetectorTest, CaptivePortalRetryAfterSeconds) {
  const char* retry_after = "HTTP/1.1 503 OK\nRetry-After: 101\n\n";
  CaptivePortalDetector::Results results;

  // Check that Retry-After headers work both on the first request to return a
  // result and on subsequent requests.
  results.result = RESULT_NO_RESPONSE;
  results.response_code = 503;
  results.retry_after_delta = base::TimeDelta::FromSeconds(101);
  RunTest(results, net::OK, 503, retry_after);

  results.result = RESULT_INTERNET_CONNECTED;
  results.response_code = 204;
  results.retry_after_delta = base::TimeDelta();
  RunTest(results, net::OK, 204, NULL);
}

// Check a Retry-After header that contains a date.
TEST_F(CaptivePortalDetectorTest, CaptivePortalRetryAfterDate) {
  const char* retry_after =
      "HTTP/1.1 503 OK\nRetry-After: Tue, 17 Apr 2012 18:02:51 GMT\n\n";
  CaptivePortalDetector::Results results;

  // base has a function to get a time in the right format from a string, but
  // not the other way around.
  base::Time start_time;
  ASSERT_TRUE(base::Time::FromString("Tue, 17 Apr 2012 18:02:00 GMT",
                                     &start_time));
  base::Time retry_after_time;
  ASSERT_TRUE(base::Time::FromString("Tue, 17 Apr 2012 18:02:51 GMT",
                                     &retry_after_time));

  SetTime(start_time);

  results.result = RESULT_NO_RESPONSE;
  results.response_code = 503;
  results.retry_after_delta = retry_after_time - start_time;
  RunTest(results, net::OK, 503, retry_after);
}

// Check invalid Retry-After headers are ignored.
TEST_F(CaptivePortalDetectorTest, CaptivePortalRetryAfterInvalid) {
  const char* retry_after = "HTTP/1.1 503 OK\nRetry-After: Christmas\n\n";
  CaptivePortalDetector::Results results;

  results.result = RESULT_NO_RESPONSE;
  results.response_code = 503;
  RunTest(results, net::OK, 503, retry_after);
}

TEST_F(CaptivePortalDetectorTest, Cancel) {
  RunCancelTest();
  CaptivePortalDetector::Results results;
  results.result = RESULT_INTERNET_CONNECTED;
  results.response_code = 204;
  RunTest(results, net::OK, 204, NULL);
}

}  // namespace captive_portal
