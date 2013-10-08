// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "chrome/browser/signin/profile_oauth2_token_service_request.h"

#include <set>
#include <string>
#include <vector>
#include "base/threading/thread.h"
#include "chrome/browser/signin/oauth2_token_service.h"
#include "chrome/browser/signin/profile_oauth2_token_service.h"
#include "chrome/browser/signin/profile_oauth2_token_service_factory.h"
#include "chrome/browser/signin/token_service_factory.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/test_browser_thread.h"
#include "google_apis/gaia/google_service_auth_error.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

class TestingOAuth2TokenServiceConsumer : public OAuth2TokenService::Consumer {
 public:
  TestingOAuth2TokenServiceConsumer();
  virtual ~TestingOAuth2TokenServiceConsumer();

  virtual void OnGetTokenSuccess(const OAuth2TokenService::Request* request,
                                 const std::string& access_token,
                                 const base::Time& expiration_time) OVERRIDE;
  virtual void OnGetTokenFailure(const OAuth2TokenService::Request* request,
                                 const GoogleServiceAuthError& error) OVERRIDE;

  std::string last_token_;
  int number_of_successful_tokens_;
  GoogleServiceAuthError last_error_;
  int number_of_errors_;
};

TestingOAuth2TokenServiceConsumer::TestingOAuth2TokenServiceConsumer()
    : number_of_successful_tokens_(0),
      last_error_(GoogleServiceAuthError::AuthErrorNone()),
      number_of_errors_(0) {
}

TestingOAuth2TokenServiceConsumer::~TestingOAuth2TokenServiceConsumer() {
}

void TestingOAuth2TokenServiceConsumer::OnGetTokenSuccess(
    const OAuth2TokenService::Request* request,
    const std::string& token,
    const base::Time& expiration_date) {
  last_token_ = token;
  ++number_of_successful_tokens_;
}

void TestingOAuth2TokenServiceConsumer::OnGetTokenFailure(
    const OAuth2TokenService::Request* request,
    const GoogleServiceAuthError& error) {
  last_error_ = error;
  ++number_of_errors_;
}

class MockProfileOAuth2TokenService : public ProfileOAuth2TokenService {
 public:
  class Request : public OAuth2TokenService::Request,
                  public base::SupportsWeakPtr<Request> {
   public:
    Request(OAuth2TokenService::Consumer* consumer,
            GoogleServiceAuthError error,
            std::string access_token);
    virtual ~Request();

    void InformConsumer() const;

   private:
    OAuth2TokenService::Consumer* consumer_;
    GoogleServiceAuthError error_;
    std::string access_token_;
    base::Time expiration_date_;
  };

  MockProfileOAuth2TokenService();
  virtual ~MockProfileOAuth2TokenService();

  virtual scoped_ptr<OAuth2TokenService::Request> StartRequest(
      const std::set<std::string>& scopes,
      OAuth2TokenService::Consumer* consumer) OVERRIDE;

  void SetExpectation(bool success, std::string oauth2_access_token);

 private:
  static void InformConsumer(
      base::WeakPtr<MockProfileOAuth2TokenService::Request> request);

  bool success_;
  std::string oauth2_access_token_;
};

MockProfileOAuth2TokenService::Request::Request(
    OAuth2TokenService::Consumer* consumer,
    GoogleServiceAuthError error,
    std::string access_token)
    : consumer_(consumer),
      error_(error),
      access_token_(access_token) {
}

MockProfileOAuth2TokenService::Request::~Request() {
}

void MockProfileOAuth2TokenService::Request::InformConsumer() const {
  if (error_.state() == GoogleServiceAuthError::NONE)
    consumer_->OnGetTokenSuccess(this, access_token_, expiration_date_);
  else
    consumer_->OnGetTokenFailure(this, error_);
}

MockProfileOAuth2TokenService::MockProfileOAuth2TokenService()
    : success_(true),
      oauth2_access_token_(std::string("success token")) {
}

MockProfileOAuth2TokenService::~MockProfileOAuth2TokenService() {
}

void MockProfileOAuth2TokenService::SetExpectation(bool success,
                                            std::string oauth2_access_token) {
  success_ = success;
  oauth2_access_token_ = oauth2_access_token;
}

// static
void MockProfileOAuth2TokenService::InformConsumer(
    base::WeakPtr<MockProfileOAuth2TokenService::Request> request) {
  if (request.get())
    request->InformConsumer();
}

scoped_ptr<OAuth2TokenService::Request>
    MockProfileOAuth2TokenService::StartRequest(
        const std::set<std::string>& scopes,
        OAuth2TokenService::Consumer* consumer) {
  scoped_ptr<Request> request;
  if (success_) {
    request.reset(new MockProfileOAuth2TokenService::Request(
        consumer,
        GoogleServiceAuthError(GoogleServiceAuthError::NONE),
        oauth2_access_token_));
  } else {
    request.reset(new MockProfileOAuth2TokenService::Request(
        consumer,
        GoogleServiceAuthError(GoogleServiceAuthError::SERVICE_UNAVAILABLE),
        std::string()));
  }
  base::MessageLoop::current()->PostTask(FROM_HERE, base::Bind(
    &MockProfileOAuth2TokenService::InformConsumer, request->AsWeakPtr()));
  return request.PassAs<OAuth2TokenService::Request>();
}

static BrowserContextKeyedService* CreateOAuth2TokenService(
    content::BrowserContext* profile) {
  MockProfileOAuth2TokenService* mock = new MockProfileOAuth2TokenService();
  mock->Initialize(static_cast<Profile*>(profile));
  return mock;
}

class ProfileOAuth2TokenServiceRequestTest : public testing::Test {
 public:
  virtual void SetUp() OVERRIDE;

 protected:
  base::MessageLoop ui_loop_;
  scoped_ptr<content::TestBrowserThread> ui_thread_;

  scoped_ptr<Profile> profile_;
  TestingOAuth2TokenServiceConsumer consumer_;
  MockProfileOAuth2TokenService* oauth2_service_;

  scoped_ptr<ProfileOAuth2TokenServiceRequest> request_;
};

void ProfileOAuth2TokenServiceRequestTest::SetUp() {
  ui_thread_.reset(new content::TestBrowserThread(content::BrowserThread::UI,
                                                  &ui_loop_));
  profile_.reset(new TestingProfile());
  ProfileOAuth2TokenServiceFactory::GetInstance()->SetTestingFactory(
      profile_.get(), &CreateOAuth2TokenService);
  oauth2_service_ = (MockProfileOAuth2TokenService*)
      ProfileOAuth2TokenServiceFactory::GetForProfile(profile_.get());
}

TEST_F(ProfileOAuth2TokenServiceRequestTest,
       Failure) {
  oauth2_service_->SetExpectation(false, std::string());
  scoped_ptr<ProfileOAuth2TokenServiceRequest> request(
      ProfileOAuth2TokenServiceRequest::CreateAndStart(
          profile_.get(),
          std::set<std::string>(),
          &consumer_));
  ui_loop_.RunUntilIdle();
  EXPECT_EQ(0, consumer_.number_of_successful_tokens_);
  EXPECT_EQ(1, consumer_.number_of_errors_);
}

TEST_F(ProfileOAuth2TokenServiceRequestTest,
       Success) {
  scoped_ptr<ProfileOAuth2TokenServiceRequest> request(
      ProfileOAuth2TokenServiceRequest::CreateAndStart(
          profile_.get(),
          std::set<std::string>(),
          &consumer_));
  ui_loop_.RunUntilIdle();
  EXPECT_EQ(1, consumer_.number_of_successful_tokens_);
  EXPECT_EQ("success token", consumer_.last_token_);
  EXPECT_EQ(0, consumer_.number_of_errors_);
}

TEST_F(ProfileOAuth2TokenServiceRequestTest,
       RequestDeletionBeforeServiceComplete) {
  scoped_ptr<ProfileOAuth2TokenServiceRequest> request(
      ProfileOAuth2TokenServiceRequest::CreateAndStart(
          profile_.get(),
          std::set<std::string>(),
          &consumer_));
  request.reset();
  ui_loop_.RunUntilIdle();
  EXPECT_EQ(0, consumer_.number_of_successful_tokens_);
  EXPECT_EQ(0, consumer_.number_of_errors_);
}

TEST_F(ProfileOAuth2TokenServiceRequestTest,
       RequestDeletionAfterServiceComplete) {
  scoped_ptr<ProfileOAuth2TokenServiceRequest> request(
      ProfileOAuth2TokenServiceRequest::CreateAndStart(
          profile_.get(),
          std::set<std::string>(),
          &consumer_));
  ui_loop_.RunUntilIdle();
  request.reset();
  EXPECT_EQ(1, consumer_.number_of_successful_tokens_);
  EXPECT_EQ(0, consumer_.number_of_errors_);
}

}  // namespace
