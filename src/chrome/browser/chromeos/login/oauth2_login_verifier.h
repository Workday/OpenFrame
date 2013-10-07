// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_OAUTH2_LOGIN_VERIFIER_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_OAUTH2_LOGIN_VERIFIER_H_

#include <string>

#include "base/basictypes.h"
#include "base/callback_forward.h"
#include "base/compiler_specific.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/oauth2_token_service.h"
#include "google_apis/gaia/gaia_auth_consumer.h"
#include "google_apis/gaia/gaia_auth_fetcher.h"
#include "google_apis/gaia/oauth2_access_token_fetcher.h"
#include "net/url_request/url_request_context_getter.h"

namespace chromeos {

// Given the OAuth2 refresh token, this class will try to exchange it for GAIA
// credentials (SID+LSID) and populate current session's cookie jar.
class OAuth2LoginVerifier : public base::SupportsWeakPtr<OAuth2LoginVerifier>,
                            public GaiaAuthConsumer,
                            public OAuth2TokenService::Consumer {
 public:
  class Delegate {
   public:
    virtual ~Delegate() {}
    // Invoked during exchange of OAuth2 refresh token for GAIA service token.
    virtual void OnOAuthLoginSuccess(
        const ClientLoginResult& gaia_credentials) = 0;
    // Invoked when provided OAuth2 refresh token is invalid.
    virtual void OnOAuthLoginFailure() = 0;
    // Invoked when cookie session is successfully merged.
    virtual void OnSessionMergeSuccess() = 0;
    // Invoked when cookie session can not be merged.
    virtual void OnSessionMergeFailure() = 0;
  };

  OAuth2LoginVerifier(OAuth2LoginVerifier::Delegate* delegate,
                      net::URLRequestContextGetter* system_request_context,
                      net::URLRequestContextGetter* user_request_context);
  virtual ~OAuth2LoginVerifier();

  // Attempts to restore session from OAuth2 refresh token minting all necesarry
  // tokens along the way (OAuth2 access token, SID/LSID, GAIA service token).
  void VerifyProfileTokens(Profile* profile);

 private:
  enum SessionRestoreType {
    RESTORE_UNDEFINED = 0,
    RESTORE_FROM_GAIA_TOKEN = 1,
    RESTORE_FROM_OAUTH2_REFRESH_TOKEN = 2,
  };
  // GaiaAuthConsumer overrides.
  virtual void OnUberAuthTokenSuccess(const std::string& token) OVERRIDE;
  virtual void OnUberAuthTokenFailure(
      const GoogleServiceAuthError& error) OVERRIDE;
  virtual void OnClientLoginSuccess(const ClientLoginResult& result) OVERRIDE;
  virtual void OnClientLoginFailure(
      const GoogleServiceAuthError& error) OVERRIDE;
  virtual void OnMergeSessionSuccess(const std::string& data) OVERRIDE;
  virtual void OnMergeSessionFailure(
      const GoogleServiceAuthError& error) OVERRIDE;

  // OAuth2TokenService::Consumer overrides.
  virtual void OnGetTokenSuccess(const OAuth2TokenService::Request* request,
                                 const std::string& access_token,
                                 const base::Time& expiration_time) OVERRIDE;
  virtual void OnGetTokenFailure(const OAuth2TokenService::Request* request,
                                 const GoogleServiceAuthError& error) OVERRIDE;

  // Starts fetching OAuth1 access token for OAuthLogin call.
  void StartFetchingOAuthLoginAccessToken(Profile* profile);

  // Starts OAuthLogin request for GAIA uber-token.
  void StartOAuthLoginForUberToken();

  // Starts OAuthLogin request.
  void StartOAuthLoginForGaiaCredentials();

  // Attempts to merge session from present |gaia_token_|.
  void StartMergeSession();

  // Decides how to proceed on GAIA |error|. If the error looks temporary,
  // retries |task| after certain delay until max retry count is reached.
  void RetryOnError(const char* operation_id,
                    const GoogleServiceAuthError& error,
                    const base::Closure& task_to_retry,
                    const base::Closure& error_handler);

  OAuth2LoginVerifier::Delegate* delegate_;
  scoped_refptr<net::URLRequestContextGetter> system_request_context_;
  scoped_refptr<net::URLRequestContextGetter> user_request_context_;
  scoped_ptr<GaiaAuthFetcher> gaia_system_fetcher_;
  scoped_ptr<GaiaAuthFetcher> gaia_fetcher_;
  std::string access_token_;
  std::string gaia_token_;
  scoped_ptr<OAuth2TokenService::Request> login_token_request_;
  // The retry counter. Increment this only when failure happened.
  int retry_count_;

  DISALLOW_COPY_AND_ASSIGN(OAuth2LoginVerifier);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_OAUTH2_LOGIN_VERIFIER_H_
