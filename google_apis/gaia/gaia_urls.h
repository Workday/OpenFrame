// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GOOGLE_APIS_GAIA_GAIA_URLS_H_
#define GOOGLE_APIS_GAIA_GAIA_URLS_H_

#include <string>

#include "base/memory/singleton.h"
#include "url/gurl.h"

// A signleton that provides all the URLs that are used for connecting to GAIA.
class GaiaUrls {
 public:
  static GaiaUrls* GetInstance();

  // The URLs for different calls in the Google Accounts programmatic login API.
  const std::string& captcha_url_prefix() const;

  const GURL& gaia_url() const;
  const std::string& client_login_url() const;
  const std::string& service_login_url() const;
  const std::string& service_logout_url() const;
  const std::string& issue_auth_token_url() const;
  const std::string& get_user_info_url() const;
  const std::string& token_auth_url() const;
  const std::string& merge_session_url() const;
  const std::string& get_oauth_token_url() const;
  const std::string& oauth_get_access_token_url() const;
  const std::string& oauth_wrap_bridge_url() const;
  const std::string& oauth_user_info_url() const;
  const std::string& oauth_revoke_token_url() const;
  const std::string& oauth1_login_url() const;

  const std::string& oauth1_login_scope() const;
  const std::string& oauth_wrap_bridge_user_info_scope() const;

  const std::string& oauth2_chrome_client_id() const;
  const std::string& oauth2_chrome_client_secret() const;
  const std::string& client_login_to_oauth2_url() const;
  const std::string& oauth2_auth_url() const;
  const std::string& oauth2_token_url() const;
  const std::string& oauth2_issue_token_url() const;
  const std::string& oauth2_token_info_url() const;
  const std::string& oauth2_revoke_url() const;

  const std::string& gaia_login_form_realm() const;

 private:
  GaiaUrls();
  ~GaiaUrls();

  friend struct DefaultSingletonTraits<GaiaUrls>;

  std::string captcha_url_prefix_;

  GURL gaia_url_;
  std::string lso_origin_url_;
  std::string google_apis_origin_url_;
  std::string client_login_url_;
  std::string service_login_url_;
  std::string service_logout_url_;
  std::string issue_auth_token_url_;
  std::string get_user_info_url_;
  std::string token_auth_url_;
  std::string merge_session_url_;
  std::string get_oauth_token_url_;
  std::string oauth_get_access_token_url_;
  std::string oauth_wrap_bridge_url_;
  std::string oauth_user_info_url_;
  std::string oauth_revoke_token_url_;
  std::string oauth1_login_url_;

  std::string oauth1_login_scope_;
  std::string oauth_wrap_bridge_user_info_scope_;

  std::string oauth2_chrome_client_id_;
  std::string oauth2_chrome_client_secret_;
  std::string client_login_to_oauth2_url_;
  std::string oauth2_auth_url_;
  std::string oauth2_token_url_;
  std::string oauth2_issue_token_url_;
  std::string oauth2_token_info_url_;
  std::string oauth2_revoke_url_;

  std::string gaia_login_form_realm_;

  DISALLOW_COPY_AND_ASSIGN(GaiaUrls);
};

#endif  // GOOGLE_APIS_GAIA_GAIA_URLS_H_
