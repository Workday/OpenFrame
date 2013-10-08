// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/managed_mode/managed_user_refresh_token_fetcher.h"

#include "base/callback.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "chrome/browser/signin/oauth2_token_service.h"
#include "google_apis/gaia/gaia_constants.h"
#include "google_apis/gaia/gaia_oauth_client.h"
#include "google_apis/gaia/gaia_urls.h"
#include "google_apis/gaia/google_service_auth_error.h"
#include "google_apis/gaia/oauth2_api_call_flow.h"
#include "net/base/escape.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/http/http_status_code.h"
#include "net/url_request/url_fetcher.h"
#include "net/url_request/url_request_status.h"

using base::Time;
using gaia::GaiaOAuthClient;
using GaiaConstants::kChromeSyncManagedOAuth2Scope;
using net::URLFetcher;
using net::URLFetcherDelegate;
using net::URLRequestContextGetter;

namespace {

const int kNumRetries = 1;

static const char kIssueTokenBodyFormat[] =
    "client_id=%s"
    "&scope=%s"
    "&response_type=code"
    "&profile_id=%s"
    "&device_name=%s";

static const char kAuthorizationHeaderFormat[] =
    "Authorization: Bearer %s";

static const char kCodeKey[] = "code";

class ManagedUserRefreshTokenFetcherImpl
    : public ManagedUserRefreshTokenFetcher,
      public OAuth2TokenService::Consumer,
      public URLFetcherDelegate,
      public GaiaOAuthClient::Delegate {
 public:
  ManagedUserRefreshTokenFetcherImpl(OAuth2TokenService* oauth2_token_service,
                              URLRequestContextGetter* context);
  virtual ~ManagedUserRefreshTokenFetcherImpl();

  // ManagedUserRefreshTokenFetcher implementation:
  virtual void Start(const std::string& managed_user_id,
                     const std::string& device_name,
                     const TokenCallback& callback) OVERRIDE;

 protected:
  // OAuth2TokenService::Consumer implementation:
  virtual void OnGetTokenSuccess(const OAuth2TokenService::Request* request,
                                 const std::string& access_token,
                                 const Time& expiration_time) OVERRIDE;
  virtual void OnGetTokenFailure(const OAuth2TokenService::Request* request,
                                 const GoogleServiceAuthError& error) OVERRIDE;

  // net::URLFetcherDelegate implementation.
  virtual void OnURLFetchComplete(const URLFetcher* source) OVERRIDE;

  // GaiaOAuthClient::Delegate implementation:
  virtual void OnGetTokensResponse(const std::string& refresh_token,
                                   const std::string& access_token,
                                   int expires_in_seconds) OVERRIDE;
  virtual void OnRefreshTokenResponse(const std::string& access_token,
                                      int expires_in_seconds) OVERRIDE;
  virtual void OnOAuthError() OVERRIDE;
  virtual void OnNetworkError(int response_code) OVERRIDE;

 private:
  // Requests an access token, which is the first thing we need. This is where
  // we restart when the returned access token has expired.
  void StartFetching();

  void DispatchNetworkError(int error_code);
  void DispatchGoogleServiceAuthError(const GoogleServiceAuthError& error,
                                      const std::string& token);
  OAuth2TokenService* oauth2_token_service_;
  URLRequestContextGetter* context_;

  std::string device_name_;
  std::string managed_user_id_;
  TokenCallback callback_;

  scoped_ptr<OAuth2TokenService::Request> access_token_request_;
  std::string access_token_;
  bool access_token_expired_;
  scoped_ptr<URLFetcher> url_fetcher_;
  scoped_ptr<GaiaOAuthClient> gaia_oauth_client_;
};

ManagedUserRefreshTokenFetcherImpl::ManagedUserRefreshTokenFetcherImpl(
    OAuth2TokenService* oauth2_token_service,
    URLRequestContextGetter* context)
    : oauth2_token_service_(oauth2_token_service),
      context_(context),
      access_token_expired_(false) {}

ManagedUserRefreshTokenFetcherImpl::~ManagedUserRefreshTokenFetcherImpl() {}

void ManagedUserRefreshTokenFetcherImpl::Start(
    const std::string& managed_user_id,
    const std::string& device_name,
    const TokenCallback& callback) {
  DCHECK(callback_.is_null());
  managed_user_id_ = managed_user_id;
  device_name_ = device_name;
  callback_ = callback;
  StartFetching();
}

void ManagedUserRefreshTokenFetcherImpl::StartFetching() {
  OAuth2TokenService::ScopeSet scopes;
  scopes.insert(GaiaUrls::GetInstance()->oauth1_login_scope());
  access_token_request_ = oauth2_token_service_->StartRequest(scopes, this);
}

void ManagedUserRefreshTokenFetcherImpl::OnGetTokenSuccess(
    const OAuth2TokenService::Request* request,
    const std::string& access_token,
    const Time& expiration_time) {
  DCHECK_EQ(access_token_request_.get(), request);
  access_token_ = access_token;

  GURL url(GaiaUrls::GetInstance()->oauth2_issue_token_url());
  // GaiaOAuthClient uses id 0, so we use 1 to distinguish the requests in
  // unit tests.
  const int id = 1;

  url_fetcher_.reset(URLFetcher::Create(id, url, URLFetcher::POST, this));

  url_fetcher_->SetRequestContext(context_);
  url_fetcher_->SetLoadFlags(net::LOAD_DO_NOT_SEND_COOKIES |
                             net::LOAD_DO_NOT_SAVE_COOKIES);
  url_fetcher_->SetAutomaticallyRetryOnNetworkChanges(kNumRetries);
  url_fetcher_->AddExtraRequestHeader(
      base::StringPrintf(kAuthorizationHeaderFormat, access_token.c_str()));

  std::string body = base::StringPrintf(
      kIssueTokenBodyFormat,
      net::EscapeUrlEncodedData(
          GaiaUrls::GetInstance()->oauth2_chrome_client_id(), true).c_str(),
      net::EscapeUrlEncodedData(kChromeSyncManagedOAuth2Scope, true).c_str(),
      net::EscapeUrlEncodedData(managed_user_id_, true).c_str(),
      net::EscapeUrlEncodedData(device_name_, true).c_str());
  url_fetcher_->SetUploadData("application/x-www-form-urlencoded", body);

  url_fetcher_->Start();
}

void ManagedUserRefreshTokenFetcherImpl::OnGetTokenFailure(
    const OAuth2TokenService::Request* request,
    const GoogleServiceAuthError& error) {
  DCHECK_EQ(access_token_request_.get(), request);
  callback_.Run(error, std::string());
  callback_.Reset();
}

void ManagedUserRefreshTokenFetcherImpl::OnURLFetchComplete(
    const URLFetcher* source) {
  const net::URLRequestStatus& status = source->GetStatus();
  if (!status.is_success()) {
    DispatchNetworkError(status.error());
    return;
  }

  int response_code = source->GetResponseCode();
  if (response_code == net::HTTP_UNAUTHORIZED && !access_token_expired_) {
    access_token_expired_ = true;
    oauth2_token_service_->InvalidateToken(OAuth2TokenService::ScopeSet(),
                                           access_token_);
    StartFetching();
    return;
  }

  if (response_code != net::HTTP_OK) {
    // TODO(bauerb): We should return the HTTP response code somehow.
    DLOG(WARNING) << "HTTP error " << response_code;
    DispatchGoogleServiceAuthError(
        GoogleServiceAuthError(GoogleServiceAuthError::CONNECTION_FAILED),
        std::string());
    return;
  }

  std::string response_body;
  source->GetResponseAsString(&response_body);
  scoped_ptr<base::Value> value(base::JSONReader::Read(response_body));
  DictionaryValue* dict = NULL;
  if (!value.get() || !value->GetAsDictionary(&dict)) {
    DispatchNetworkError(net::ERR_INVALID_RESPONSE);
    return;
  }
  std::string auth_code;
  if (!dict->GetString(kCodeKey, &auth_code)) {
    DispatchNetworkError(net::ERR_INVALID_RESPONSE);
    return;
  }

  gaia::OAuthClientInfo client_info;
  GaiaUrls* urls = GaiaUrls::GetInstance();
  client_info.client_id = urls->oauth2_chrome_client_id();
  client_info.client_secret = urls->oauth2_chrome_client_secret();
  gaia_oauth_client_.reset(new gaia::GaiaOAuthClient(context_));
  gaia_oauth_client_->GetTokensFromAuthCode(client_info, auth_code, kNumRetries,
                                            this);
}

void ManagedUserRefreshTokenFetcherImpl::OnGetTokensResponse(
    const std::string& refresh_token,
    const std::string& access_token,
    int expires_in_seconds) {
  // TODO(bauerb): It would be nice if we could pass the access token as well,
  // so we don't need to fetch another one immediately.
  DispatchGoogleServiceAuthError(GoogleServiceAuthError::AuthErrorNone(),
                                 refresh_token);
}

void ManagedUserRefreshTokenFetcherImpl::OnRefreshTokenResponse(
    const std::string& access_token,
    int expires_in_seconds) {
  NOTREACHED();
}

void ManagedUserRefreshTokenFetcherImpl::OnOAuthError() {
  DispatchGoogleServiceAuthError(
      GoogleServiceAuthError(GoogleServiceAuthError::CONNECTION_FAILED),
      std::string());
}

void ManagedUserRefreshTokenFetcherImpl::OnNetworkError(int response_code) {
  // TODO(bauerb): We should return the HTTP response code somehow.
  DLOG(WARNING) << "HTTP error " << response_code;
  DispatchGoogleServiceAuthError(
      GoogleServiceAuthError(GoogleServiceAuthError::CONNECTION_FAILED),
      std::string());
}

void ManagedUserRefreshTokenFetcherImpl::DispatchNetworkError(int error_code) {
  DispatchGoogleServiceAuthError(
      GoogleServiceAuthError::FromConnectionError(error_code), std::string());
}

void ManagedUserRefreshTokenFetcherImpl::DispatchGoogleServiceAuthError(
    const GoogleServiceAuthError& error,
    const std::string& token) {
  callback_.Run(error, token);
  callback_.Reset();
}

}  // namespace

// static
scoped_ptr<ManagedUserRefreshTokenFetcher>
ManagedUserRefreshTokenFetcher::Create(OAuth2TokenService* oauth2_token_service,
                                       URLRequestContextGetter* context) {
  scoped_ptr<ManagedUserRefreshTokenFetcher> fetcher(
      new ManagedUserRefreshTokenFetcherImpl(oauth2_token_service, context));
  return fetcher.Pass();
}

ManagedUserRefreshTokenFetcher::~ManagedUserRefreshTokenFetcher() {}
