// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_LOGIN_PERFORMER_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_LOGIN_PERFORMER_H_

#include <string>

#include "base/basictypes.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/chromeos/login/authenticator.h"
#include "chrome/browser/chromeos/login/login_status_consumer.h"
#include "chrome/browser/chromeos/login/online_attempt_host.h"
#include "chrome/browser/chromeos/login/user.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "google_apis/gaia/google_service_auth_error.h"

namespace chromeos {

// This class encapsulates sign in operations.
// Sign in is performed in a way that offline auth is executed first.
// Once offline auth is OK - user homedir is mounted, UI is launched.
// At this point LoginPerformer |delegate_| is destroyed and it releases
// LP instance ownership. LP waits for online login result.
// If auth is succeeded, cookie fetcher is executed, LP instance deletes itself.
//
// If |delegate_| is not NULL it will handle error messages, password input.
class LoginPerformer : public LoginStatusConsumer,
                       public OnlineAttemptHost::Delegate {
 public:
  typedef enum AuthorizationMode {
    // Authorization performed internally by Chrome.
    AUTH_MODE_INTERNAL,
    // Authorization performed by an extension.
    AUTH_MODE_EXTENSION
  } AuthorizationMode;

  // Delegate class to get notifications from the LoginPerformer.
  class Delegate : public LoginStatusConsumer {
   public:
    virtual ~Delegate() {}
    virtual void WhiteListCheckFailed(const std::string& email) = 0;
    virtual void PolicyLoadFailed() = 0;
    virtual void OnOnlineChecked(const std::string& email, bool success) = 0;
  };

  explicit LoginPerformer(Delegate* delegate);
  virtual ~LoginPerformer();

  // LoginStatusConsumer implementation:
  virtual void OnLoginFailure(const LoginFailure& error) OVERRIDE;
  virtual void OnRetailModeLoginSuccess(
      const UserContext& user_context) OVERRIDE;
  virtual void OnLoginSuccess(
      const UserContext& user_context,
      bool pending_requests,
      bool using_oauth) OVERRIDE;
  virtual void OnOffTheRecordLoginSuccess() OVERRIDE;
  virtual void OnPasswordChangeDetected() OVERRIDE;

  // Performs a login for |user_context|.
  // If auth_mode is AUTH_MODE_EXTENSION, there are no further auth checks,
  // AUTH_MODE_INTERNAL will perform auth checks.
  void PerformLogin(const UserContext& user_context,
                    AuthorizationMode auth_mode);

  // Performs locally managed user login with a given |user_context|.
  void LoginAsLocallyManagedUser(const UserContext& user_context);

  // Performs retail mode login.
  void LoginRetailMode();

  // Performs actions to prepare guest mode login.
  void LoginOffTheRecord();

  // Performs a login into the public account identified by |username|.
  void LoginAsPublicAccount(const std::string& username);

  // Migrates cryptohome using |old_password| specified.
  void RecoverEncryptedData(const std::string& old_password);

  // Reinitializes cryptohome with the new password.
  void ResyncEncryptedData();

  // Returns latest auth error.
  const GoogleServiceAuthError& error() const {
    return last_login_failure_.error();
  }

  // True if password change has been detected.
  bool password_changed() { return password_changed_; }

  // Number of times we've been called with OnPasswordChangeDetected().
  // If user enters incorrect old password, same LoginPerformer instance will
  // be called so callback count makes it possible to distinguish initial
  // "password changed detected" event from further attempts to enter old
  // password for cryptohome migration (when > 1).
  int password_changed_callback_count() {
    return password_changed_callback_count_;
  }

  void set_delegate(Delegate* delegate) { delegate_ = delegate; }

  AuthorizationMode auth_mode() const { return auth_mode_; }

 protected:
  // Implements OnlineAttemptHost::Delegate.
  virtual void OnChecked(const std::string& username, bool success) OVERRIDE;

 private:
  // Starts login completion of externally authenticated user.
  void StartLoginCompletion();

  // Starts authentication.
  void StartAuthentication();

  // Used for logging in.
  scoped_refptr<Authenticator> authenticator_;

  // Used to make auxiliary online check.
  OnlineAttemptHost online_attempt_host_;

  // Represents last login failure that was encountered when communicating to
  // sign-in server. LoginFailure.LoginFailureNone() by default.
  LoginFailure last_login_failure_;

  // User credentials for the current login attempt.
  UserContext user_context_;

  // Notifications receiver.
  Delegate* delegate_;

  // True if password change has been detected.
  // Once correct password is entered homedir migration is executed.
  bool password_changed_;
  int password_changed_callback_count_;

  // Authorization mode type.
  AuthorizationMode auth_mode_;

  base::WeakPtrFactory<LoginPerformer> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(LoginPerformer);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_LOGIN_PERFORMER_H_
