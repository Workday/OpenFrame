// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CONTENT_BROWSER_WALLET_WALLET_SIGNIN_HELPER_H_
#define COMPONENTS_AUTOFILL_CONTENT_BROWSER_WALLET_WALLET_SIGNIN_HELPER_H_

#include <string>

#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "net/url_request/url_fetcher_delegate.h"

namespace net {
class URLFetcher;
class URLRequestContextGetter;
class URLRequestStatus;
}

class GoogleServiceAuthError;

namespace autofill {
namespace wallet {

class WalletSigninHelperDelegate;

// Authenticates the user against the Online Wallet service.
// This class is not thread-safe.  An instance may be used on any thread, but
// should not be accessed from multiple threads.
class WalletSigninHelper : public net::URLFetcherDelegate {
 public:
  // Constructs a helper that works with a given |delegate| and uses a given
  // |getter| to obtain a context for URL. Both |delegate| and |getter| shall
  // remain valid over the entire lifetime of the created instance.
  WalletSigninHelper(WalletSigninHelperDelegate* delegate,
                     net::URLRequestContextGetter* getter);

  virtual ~WalletSigninHelper();

  // Initiates an attempt to passively sign the user into the Online Wallet.
  // A passive sign-in is a non-interactive refresh of content area cookies,
  // and it succeeds as long as the Online Wallet service could safely accept
  // or refresh the existing area cookies, and the user doesn't need to be
  // fully reauthenticated with the service.
  // Either OnPassiveSigninSuccess or OnPassiveSigninFailure will be called
  // on the original thread.
  void StartPassiveSignin();

  // Initiates a fetch of the user name of a signed-in user.
  // Either OnUserNameFetchSuccess or OnUserNameFetchFailure will
  // be called on the original thread.
  void StartUserNameFetch();

  // Initiates the fetch of the user's Google Wallet cookie.
  void StartWalletCookieValueFetch();

 protected:
  // Sign-in helper states (for tests).
  enum State {
    IDLE,
    PASSIVE_EXECUTING_SIGNIN,
    PASSIVE_FETCHING_USERINFO,
    USERNAME_FETCHING_USERINFO,
  };

  // (For tests) Current state of the sign-in helper.
  State state() const { return state_; }

  // (For tests) URL used to fetch the currently signed-in user info.
  std::string GetGetAccountInfoUrlForTesting() const;

 private:
  // Called if a service authentication error occurs.
  void OnServiceError(const GoogleServiceAuthError& error);

  // Called if any other error occurs.
  void OnOtherError();

  // URLFetcherDelegate implementation.
  virtual void OnURLFetchComplete(const net::URLFetcher* fetcher) OVERRIDE;

  // Initiates fetching of the currently signed-in user information.
  void StartFetchingUserNameFromSession();

  // Processes the user information received from the server by url_fetcher_
  // and calls the delegate callbacks on success/failure.
  void ProcessGetAccountInfoResponseAndFinish();

  // Attempts to parse a response from the Online Wallet sign-in.
  // Returns true if the response is correct and the sign-in has succeeded.
  // Otherwise, it calls OnServiceError() and returns false.
  bool ParseSignInResponse();

  // Attempts to parse the GetAccountInfo response from the server.
  // Returns true on success; the obtained email address is stored into |email|.
  bool ParseGetAccountInfoResponse(const net::URLFetcher* fetcher,
                                   std::string* email);

  // Callback for when the Google Wallet cookie has been retrieved.
  void ReturnWalletCookieValue(const std::string& cookie_value);

  // Should be valid throughout the lifetime of the instance.
  WalletSigninHelperDelegate* const delegate_;

  // URLRequestContextGetter to be used for URLFetchers.
  net::URLRequestContextGetter* const getter_;

  // While passive login/merge session URL fetches are going on:
  scoped_ptr<net::URLFetcher> url_fetcher_;

  // User account name (email) fetched from OnGetUserInfoSuccess().
  std::string username_;

  // Current internal state of the helper.
  State state_;

  base::WeakPtrFactory<WalletSigninHelper> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(WalletSigninHelper);
};

}  // namespace wallet
}  // namespace autofill

#endif  // COMPONENTS_AUTOFILL_CONTENT_BROWSER_WALLET_WALLET_SIGNIN_HELPER_H_
