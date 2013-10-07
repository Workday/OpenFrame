// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/settings/device_oauth2_token_service.h"

#include <string>
#include <vector>

#include "base/prefs/pref_registry_simple.h"
#include "base/prefs/pref_service.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chromeos/policy/device_cloud_policy_manager_chromeos.h"
#include "chrome/browser/policy/browser_policy_connector.h"
#include "chrome/browser/policy/proto/cloud/device_management_backend.pb.h"
#include "chrome/common/pref_names.h"
#include "chromeos/cryptohome/cryptohome_library.h"
#include "content/public/browser/browser_thread.h"
#include "google_apis/gaia/gaia_urls.h"
#include "google_apis/gaia/google_service_auth_error.h"

namespace {
const char kServiceScopeGetUserInfo[] =
    "https://www.googleapis.com/auth/userinfo.email";
}  // namespace

namespace chromeos {

// A wrapper for the consumer passed to StartRequest, which doesn't call
// through to the target Consumer unless the refresh token validation is
// complete.  Additionally implements the Request interface, so that it
// can be passed back to the caller and directly deleted when cancelling
// the request.
class DeviceOAuth2TokenService::ValidatingConsumer
    : public OAuth2TokenService::Consumer,
      public OAuth2TokenService::Request,
      public gaia::GaiaOAuthClient::Delegate {
 public:
  explicit ValidatingConsumer(DeviceOAuth2TokenService* token_service,
                              Consumer* consumer);
  virtual ~ValidatingConsumer();

  void StartValidation(scoped_ptr<Request> request);

  // OAuth2TokenService::Consumer
  virtual void OnGetTokenSuccess(
      const Request* request,
      const std::string& access_token,
      const base::Time& expiration_time) OVERRIDE;
  virtual void OnGetTokenFailure(
      const Request* request,
      const GoogleServiceAuthError& error) OVERRIDE;

  // gaia::GaiaOAuthClient::Delegate implementation.
  virtual void OnRefreshTokenResponse(const std::string& access_token,
                                      int expires_in_seconds) OVERRIDE;
  virtual void OnGetTokenInfoResponse(scoped_ptr<DictionaryValue> token_info)
      OVERRIDE;
  virtual void OnOAuthError() OVERRIDE;
  virtual void OnNetworkError(int response_code) OVERRIDE;

 private:
  void RefreshTokenIsValid(bool is_valid);
  void InformConsumer();

  DeviceOAuth2TokenService* token_service_;
  Consumer* consumer_;
  scoped_ptr<gaia::GaiaOAuthClient> gaia_oauth_client_;

  // We don't know which will complete first: the validation or the token
  // minting.  So, we need to cache the results so the final callback can
  // take action.

  // RefreshTokenValidationConsumer results
  bool token_validation_done_;
  bool token_is_valid_;

  // The request instance returned by OAuth2TokenService, which we're
  // wrapping.  If the this object is deleted, |request_| will also be
  // deleted and the OAuth2TokenService won't call back on this object.
  scoped_ptr<OAuth2TokenService::Request> request_;

  // OAuth2TokenService::Consumer results
  bool token_fetch_done_;
  std::string access_token_;
  base::Time expiration_time_;
  scoped_ptr<GoogleServiceAuthError> error_;
};

DeviceOAuth2TokenService::ValidatingConsumer::ValidatingConsumer(
    DeviceOAuth2TokenService* token_service,
    Consumer* consumer)
        : token_service_(token_service),
          consumer_(consumer),
          token_validation_done_(false),
          token_is_valid_(false),
          token_fetch_done_(false) {
}

DeviceOAuth2TokenService::ValidatingConsumer::~ValidatingConsumer() {
}

void DeviceOAuth2TokenService::ValidatingConsumer::StartValidation(
    scoped_ptr<Request> request) {
  DCHECK(!gaia_oauth_client_);
  request_ = request.Pass();
  gaia_oauth_client_.reset(new gaia::GaiaOAuthClient(
      g_browser_process->system_request_context()));

  GaiaUrls* gaia_urls = GaiaUrls::GetInstance();
  gaia::OAuthClientInfo client_info;
  client_info.client_id = gaia_urls->oauth2_chrome_client_id();
  client_info.client_secret = gaia_urls->oauth2_chrome_client_secret();

  gaia_oauth_client_->RefreshToken(
      client_info,
      token_service_->GetRefreshToken(),
      std::vector<std::string>(1, kServiceScopeGetUserInfo),
      token_service_->max_refresh_token_validation_retries_,
      this);
}

void DeviceOAuth2TokenService::ValidatingConsumer::OnRefreshTokenResponse(
    const std::string& access_token,
    int expires_in_seconds) {
  gaia_oauth_client_->GetTokenInfo(
      access_token,
      token_service_->max_refresh_token_validation_retries_,
      this);
}

void DeviceOAuth2TokenService::ValidatingConsumer::OnGetTokenInfoResponse(
    scoped_ptr<DictionaryValue> token_info) {
  std::string gaia_robot_id;
  token_info->GetString("email", &gaia_robot_id);

  std::string policy_robot_id = token_service_->GetRobotAccountId();

  if (policy_robot_id == gaia_robot_id) {
    RefreshTokenIsValid(true);
  } else {
    if (gaia_robot_id.empty()) {
      LOG(WARNING) << "Device service account owner in policy is empty.";
    } else {
      LOG(INFO) << "Device service account owner in policy does not match "
                << "refresh token owner \"" << gaia_robot_id << "\".";
    }
    RefreshTokenIsValid(false);
  }
}

void DeviceOAuth2TokenService::ValidatingConsumer::OnOAuthError() {
  RefreshTokenIsValid(false);
}

void DeviceOAuth2TokenService::ValidatingConsumer::OnNetworkError(
    int response_code) {
  RefreshTokenIsValid(false);
}

void DeviceOAuth2TokenService::ValidatingConsumer::OnGetTokenSuccess(
      const Request* request,
      const std::string& access_token,
      const base::Time& expiration_time) {
  token_fetch_done_ = true;
  access_token_ = access_token;
  expiration_time_ = expiration_time;
  if (token_validation_done_)
    InformConsumer();
}

void DeviceOAuth2TokenService::ValidatingConsumer::OnGetTokenFailure(
      const Request* request,
      const GoogleServiceAuthError& error) {
  token_fetch_done_ = true;
  error_.reset(new GoogleServiceAuthError(error.state()));
  if (token_validation_done_)
    InformConsumer();
}

void DeviceOAuth2TokenService::ValidatingConsumer::RefreshTokenIsValid(
    bool is_valid) {
  token_validation_done_ = true;
  token_is_valid_ = is_valid;
  if (token_fetch_done_)
    InformConsumer();
}

void DeviceOAuth2TokenService::ValidatingConsumer::InformConsumer() {
  DCHECK(token_fetch_done_);
  DCHECK(token_validation_done_);
  token_service_->OnValidationComplete(token_is_valid_);
  // Note: this object (which is also the Request instance) may be deleted in
  // these consumer callbacks, so the callbacks must be the last line executed.
  // Also, make copies of the parameters passed to the consumer to avoid invalid
  // memory accesses when the consumer deletes |this| immediately.
  if (!token_is_valid_) {
    consumer_->OnGetTokenFailure(this, GoogleServiceAuthError(
        GoogleServiceAuthError::INVALID_GAIA_CREDENTIALS));
  } else if (error_) {
    GoogleServiceAuthError error_copy = *error_;
    consumer_->OnGetTokenFailure(this, error_copy);
  } else {
    std::string access_token_copy = access_token_;
    base::Time expiration_time_copy = expiration_time_;
    consumer_->OnGetTokenSuccess(this, access_token_copy, expiration_time_copy);
  }
}

DeviceOAuth2TokenService::DeviceOAuth2TokenService(
    net::URLRequestContextGetter* getter,
    PrefService* local_state)
    : refresh_token_is_valid_(false),
      max_refresh_token_validation_retries_(3),
      url_request_context_getter_(getter),
      local_state_(local_state) {
}

DeviceOAuth2TokenService::~DeviceOAuth2TokenService() {
}

net::URLRequestContextGetter* DeviceOAuth2TokenService::GetRequestContext() {
  return url_request_context_getter_.get();
}

scoped_ptr<OAuth2TokenService::Request> DeviceOAuth2TokenService::StartRequest(
    const OAuth2TokenService::ScopeSet& scopes,
    OAuth2TokenService::Consumer* consumer) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));

  if (refresh_token_is_valid_) {
    return OAuth2TokenService::StartRequest(scopes, consumer).Pass();
  } else {
    scoped_ptr<ValidatingConsumer> validating_consumer(
        new ValidatingConsumer(this, consumer));

    scoped_ptr<Request> request = OAuth2TokenService::StartRequest(
        scopes, validating_consumer.get());
    validating_consumer->StartValidation(request.Pass());
    return validating_consumer.PassAs<Request>();
  }
}

void DeviceOAuth2TokenService::OnValidationComplete(
    bool refresh_token_is_valid) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  refresh_token_is_valid_ = refresh_token_is_valid;
}

// static
void DeviceOAuth2TokenService::RegisterPrefs(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(prefs::kDeviceRobotAnyApiRefreshToken,
                               std::string());
}

void DeviceOAuth2TokenService::SetAndSaveRefreshToken(
    const std::string& refresh_token) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  std::string encrypted_refresh_token =
      CryptohomeLibrary::Get()->EncryptWithSystemSalt(refresh_token);

  local_state_->SetString(prefs::kDeviceRobotAnyApiRefreshToken,
                          encrypted_refresh_token);
}

std::string DeviceOAuth2TokenService::GetRefreshToken() {
  if (refresh_token_.empty()) {
    std::string encrypted_refresh_token =
        local_state_->GetString(prefs::kDeviceRobotAnyApiRefreshToken);

    refresh_token_ = CryptohomeLibrary::Get()->DecryptWithSystemSalt(
        encrypted_refresh_token);
  }
  return refresh_token_;
}

std::string DeviceOAuth2TokenService::GetRobotAccountId() {
  policy::BrowserPolicyConnector* connector =
      g_browser_process->browser_policy_connector();
  if (connector)
    return connector->GetDeviceCloudPolicyManager()->GetRobotAccountId();
  return std::string();
}

}  // namespace chromeos
