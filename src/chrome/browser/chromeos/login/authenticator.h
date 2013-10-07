// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_AUTHENTICATOR_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_AUTHENTICATOR_H_

#include <string>

#include "base/basictypes.h"
#include "base/memory/ref_counted.h"
#include "chrome/browser/chromeos/login/login_status_consumer.h"
#include "google_apis/gaia/gaia_auth_consumer.h"

class Profile;

namespace chromeos {

struct UserContext;

// An interface for objects that will authenticate a Chromium OS user.
// When authentication successfully completes, will call
// consumer_->OnLoginSuccess() on the UI thread.
// On failure, will call consumer_->OnLoginFailure() on the UI thread.
// On password change detected, will call
// consumer_->OnPasswordChangeDetected() on the UI thread.
class Authenticator : public base::RefCountedThreadSafe<Authenticator> {
 public:
  explicit Authenticator(LoginStatusConsumer* consumer);

  // Given externally authenticated username and password (part of
  // |user_context|), this method attempts to complete authentication process.
  virtual void CompleteLogin(Profile* profile,
                             const UserContext& user_context) = 0;

  // Given a user credentials in |user_context|,
  // this method attempts to authenticate to login.
  // Must be called on the UI thread.
  virtual void AuthenticateToLogin(Profile* profile,
                                   const UserContext& user_context) = 0;

  // Given a user credentials in |user_context|, this method attempts to
  // authenticate to unlock the computer.
  // Must be called on the UI thread.
  virtual void AuthenticateToUnlock(
      const UserContext& user_context) = 0;

  // Initiates locally managed user login.
  virtual void LoginAsLocallyManagedUser(
      const UserContext& user_context) = 0;

  // Initiates retail mode login.
  virtual void LoginRetailMode() = 0;

  // Initiates incognito ("browse without signing in") login.
  virtual void LoginOffTheRecord() = 0;

  // Initiates login into the public account identified by |username|.
  virtual void LoginAsPublicAccount(const std::string& username) = 0;

  // Completes retail mode login.
  virtual void OnRetailModeLoginSuccess() = 0;

  // Notifies caller that login was successful.
  // |request_pending| is true if we still plan to call consumer_ with the
  // results of more requests.
  // Must be called on the UI thread.
  virtual void OnLoginSuccess(bool request_pending) = 0;

  // Must be called on the UI thread.
  virtual void OnLoginFailure(const LoginFailure& error) = 0;

  // Call these methods on the UI thread.
  // If a password logs the user in online, but cannot be used to
  // mount his cryptohome, we expect that a password change has
  // occurred.
  // Call this method to migrate the user's encrypted data
  // forward to use his new password.  |old_password| is the password
  // his data was last encrypted with.
  virtual void RecoverEncryptedData(
      const std::string& old_password) = 0;

  // Call this method to erase the user's encrypted data
  // and create a new cryptohome.
  virtual void ResyncEncryptedData() = 0;

  // Profile (usually off the record ) that was used to perform the last
  // authentication process.
  Profile* authentication_profile() { return authentication_profile_; }

  // Sets consumer explicitly.
  void SetConsumer(LoginStatusConsumer* consumer);

 protected:
  virtual ~Authenticator();

  LoginStatusConsumer* consumer_;
  Profile* authentication_profile_;

 private:
  friend class base::RefCountedThreadSafe<Authenticator>;

  DISALLOW_COPY_AND_ASSIGN(Authenticator);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_AUTHENTICATOR_H_
