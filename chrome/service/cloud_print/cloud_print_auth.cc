// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/service/cloud_print/cloud_print_auth.h"

#include "base/bind.h"
#include "base/strings/string_util.h"
#include "chrome/common/cloud_print/cloud_print_constants.h"
#include "chrome/common/cloud_print/cloud_print_helpers.h"
#include "chrome/service/cloud_print/cloud_print_token_store.h"
#include "chrome/service/net/service_url_request_context.h"
#include "chrome/service/service_process.h"
#include "google_apis/gaia/gaia_urls.h"

namespace cloud_print {

CloudPrintAuth::CloudPrintAuth(
    Client* client,
    const GURL& cloud_print_server_url,
    const gaia::OAuthClientInfo& oauth_client_info,
    const std::string& proxy_id)
      : client_(client),
        oauth_client_info_(oauth_client_info),
        cloud_print_server_url_(cloud_print_server_url),
        proxy_id_(proxy_id) {
  DCHECK(client);
}

void CloudPrintAuth::AuthenticateWithToken(
    const std::string& cloud_print_token) {
  VLOG(1) << "CP_AUTH: Authenticating with token";

  client_login_token_ = cloud_print_token;

  // We need to get the credentials of the robot here.
  GURL get_authcode_url = GetUrlForGetAuthCode(cloud_print_server_url_,
                                               oauth_client_info_.client_id,
                                               proxy_id_);
  request_ = CloudPrintURLFetcher::Create();
  request_->StartGetRequest(get_authcode_url,
                            this,
                            kCloudPrintAuthMaxRetryCount,
                            std::string());
}

void CloudPrintAuth::AuthenticateWithRobotToken(
    const std::string& robot_oauth_refresh_token,
    const std::string& robot_email) {
  VLOG(1) << "CP_AUTH: Authenticating with robot token";

  robot_email_ = robot_email;
  refresh_token_ = robot_oauth_refresh_token;
  RefreshAccessToken();
}

void CloudPrintAuth::AuthenticateWithRobotAuthCode(
    const std::string& robot_oauth_auth_code,
    const std::string& robot_email) {
  VLOG(1) << "CP_AUTH: Authenticating with robot auth code";

  robot_email_ = robot_email;
  // Now that we have an auth code we need to get the refresh and access tokens.
  oauth_client_.reset(new gaia::GaiaOAuthClient(
      g_service_process->GetServiceURLRequestContextGetter()));
  oauth_client_->GetTokensFromAuthCode(oauth_client_info_,
                                       robot_oauth_auth_code,
                                       kCloudPrintAuthMaxRetryCount,
                                       this);
}

void CloudPrintAuth::RefreshAccessToken() {
  oauth_client_.reset(new gaia::GaiaOAuthClient(
      g_service_process->GetServiceURLRequestContextGetter()));
  std::vector<std::string> empty_scope_list;  // (Use scope from refresh token.)
  oauth_client_->RefreshToken(oauth_client_info_,
                              refresh_token_,
                              empty_scope_list,
                              kCloudPrintAuthMaxRetryCount,
                              this);
}

void CloudPrintAuth::OnGetTokensResponse(const std::string& refresh_token,
                                         const std::string& access_token,
                                         int expires_in_seconds) {
  refresh_token_ = refresh_token;
  // After saving the refresh token, this is just like having just refreshed
  // the access token. Just call OnRefreshTokenResponse.
  OnRefreshTokenResponse(access_token, expires_in_seconds);
}

void CloudPrintAuth::OnRefreshTokenResponse(const std::string& access_token,
                                            int expires_in_seconds) {
  client_->OnAuthenticationComplete(access_token, refresh_token_,
                                    robot_email_, user_email_);

  // Schedule a task to refresh the access token again when it is about to
  // expire.
  DCHECK(expires_in_seconds > kTokenRefreshGracePeriodSecs);
  base::TimeDelta refresh_delay = base::TimeDelta::FromSeconds(
      expires_in_seconds - kTokenRefreshGracePeriodSecs);
  base::MessageLoop::current()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&CloudPrintAuth::RefreshAccessToken, this),
      refresh_delay);
}

void CloudPrintAuth::OnOAuthError() {
  // Notify client about authentication error.
  client_->OnInvalidCredentials();
}

void CloudPrintAuth::OnNetworkError(int response_code) {
  // Since we specify infinite retries on network errors, this should never
  // be called.
  NOTREACHED() <<
      "OnNetworkError invoked when not expected, response code is " <<
      response_code;
}

CloudPrintURLFetcher::ResponseAction CloudPrintAuth::HandleJSONData(
    const net::URLFetcher* source,
    const GURL& url,
    base::DictionaryValue* json_data,
    bool succeeded) {
  if (!succeeded) {
    VLOG(1) << "CP_AUTH: Creating robot account failed";
    client_->OnInvalidCredentials();
    return CloudPrintURLFetcher::STOP_PROCESSING;
  }

  std::string auth_code;
  if (!json_data->GetString(kOAuthCodeValue, &auth_code)) {
    VLOG(1) << "CP_AUTH: Creating robot account returned invalid json response";
    client_->OnInvalidCredentials();
    return CloudPrintURLFetcher::STOP_PROCESSING;
  }

  json_data->GetString(kXMPPJidValue, &robot_email_);
  // Now that we have an auth code we need to get the refresh and access tokens.
  oauth_client_.reset(new gaia::GaiaOAuthClient(
      g_service_process->GetServiceURLRequestContextGetter()));
  oauth_client_->GetTokensFromAuthCode(oauth_client_info_,
                                       auth_code,
                                       kCloudPrintAPIMaxRetryCount,
                                       this);

  return CloudPrintURLFetcher::STOP_PROCESSING;
}

CloudPrintURLFetcher::ResponseAction CloudPrintAuth::OnRequestAuthError() {
  VLOG(1) << "CP_AUTH: Creating robot account authentication error";
  // Notify client about authentication error.
  client_->OnInvalidCredentials();
  return CloudPrintURLFetcher::STOP_PROCESSING;
}

std::string CloudPrintAuth::GetAuthHeader() {
  DCHECK(!client_login_token_.empty());
  std::string header;
  header = "Authorization: GoogleLogin auth=";
  header += client_login_token_;
  return header;
}

CloudPrintAuth::~CloudPrintAuth() {}

}  // namespace cloud_print
