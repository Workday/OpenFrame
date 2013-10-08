// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_PARALLEL_AUTHENTICATOR_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_PARALLEL_AUTHENTICATOR_H_

#include <string>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/gtest_prod_util.h"
#include "base/memory/scoped_ptr.h"
#include "base/synchronization/lock.h"
#include "chrome/browser/chromeos/login/auth_attempt_state.h"
#include "chrome/browser/chromeos/login/auth_attempt_state_resolver.h"
#include "chrome/browser/chromeos/login/authenticator.h"
#include "chrome/browser/chromeos/login/online_attempt.h"
#include "chrome/browser/chromeos/login/test_attempt_state.h"
#include "chrome/browser/chromeos/settings/device_settings_service.h"
#include "google_apis/gaia/gaia_auth_consumer.h"

class LoginFailure;
class Profile;

namespace chromeos {

class LoginStatusConsumer;

// Authenticates a Chromium OS user against cryptohome.
// Relies on the fact that online authentications has been already performed
// (i.e. using_oauth_ is true).
//
// At a high, level, here's what happens:
// AuthenticateToLogin() calls a Cryptohome's method to perform offline login.
// Resultes are stored in a AuthAttemptState owned by ParallelAuthenticator
// and then call Resolve().  Resolve() will attempt to
// determine which AuthState we're in, based on the info at hand.
// It then triggers further action based on the calculated AuthState; this
// further action might include calling back the passed-in LoginStatusConsumer
// to signal that login succeeded or failed, waiting for more outstanding
// operations to complete, or triggering some more Cryptohome method calls.
//
// Typical flows
// -------------
// Add new user: CONTINUE > CONTINUE > CREATE_NEW > CONTINUE > ONLINE_LOGIN
// Login as existing user: CONTINUE > OFFLINE_LOGIN
// Login as existing user (failure): CONTINUE > FAILED_MOUNT
// Change password detected:
//   GAIA online ok: CONTINUE > CONTINUE > NEED_OLD_PW
//     Recreate: CREATE_NEW > CONTINUE > ONLINE_LOGIN
//     Old password failure: NEED_OLD_PW
//     Old password ok: RECOVER_MOUNT > CONTINUE > ONLINE_LOGIN
//
// TODO(nkostylev): Rename ParallelAuthenticator since it is not doing
// offline/online login operations in parallel anymore.
class ParallelAuthenticator : public Authenticator,
                              public AuthAttemptStateResolver {
 public:
  enum AuthState {
    CONTINUE = 0,            // State indeterminate; try again with more info.
    NO_MOUNT = 1,            // Cryptohome doesn't exist yet.
    FAILED_MOUNT = 2,        // Failed to mount existing cryptohome.
    FAILED_REMOVE = 3,       // Failed to remove existing cryptohome.
    FAILED_TMPFS = 4,        // Failed to mount tmpfs for guest user.
    FAILED_TPM = 5,          // Failed to mount/create cryptohome, TPM error.
    CREATE_NEW = 6,          // Need to create cryptohome for a new user.
    RECOVER_MOUNT = 7,       // After RecoverEncryptedData, mount cryptohome.
    POSSIBLE_PW_CHANGE = 8,  // Offline login failed, user may have changed pw.
    NEED_NEW_PW = 9,         // Obsolete (ClientLogin): user changed pw,
                             // we have the old one.
    NEED_OLD_PW = 10,        // User changed pw, and we have the new one
                             // (GAIA auth is OK).
    HAVE_NEW_PW = 11,        // Obsolete (ClientLogin): We have verified new pw,
                             // time to migrate key.
    OFFLINE_LOGIN = 12,      // Login succeeded offline.
    DEMO_LOGIN = 13,         // Logged in as the demo user.
    ONLINE_LOGIN = 14,       // Offline and online login succeeded.
    UNLOCK = 15,             // Screen unlock succeeded.
    ONLINE_FAILED = 16,      // Obsolete (ClientLogin): Online login disallowed,
                             // but offline succeeded.
    GUEST_LOGIN = 17,        // Logged in guest mode.
    PUBLIC_ACCOUNT_LOGIN = 18,        // Logged into a public account.
    LOCALLY_MANAGED_USER_LOGIN = 19,  // Logged in as a locally managed user.
    LOGIN_FAILED = 20,       // Login denied.
    OWNER_REQUIRED = 21      // Login is restricted to the owner only.
  };

  explicit ParallelAuthenticator(LoginStatusConsumer* consumer);

  // Authenticator overrides.
  virtual void CompleteLogin(Profile* profile,
                             const UserContext& user_context) OVERRIDE;

  // Given |user_context|, this method attempts to authenticate to your
  // Chrome OS device. As soon as we have successfully mounted the encrypted
  // home directory for the user, we will call consumer_->OnLoginSuccess()
  // with the username.
  // Upon failure to login consumer_->OnLoginFailure() is called
  // with an error message.
  //
  // Uses |profile| when doing URL fetches.
  virtual void AuthenticateToLogin(Profile* profile,
                                   const UserContext& user_context) OVERRIDE;

  // Given |user_context|, this method attempts to authenticate to the cached
  // user_context. This will never contact the server even if it's online.
  // The auth result is sent to LoginStatusConsumer in a same way as
  // AuthenticateToLogin does.
  virtual void AuthenticateToUnlock(
      const UserContext& user_context) OVERRIDE;

  // Initiates locally managed user login.
  // Creates cryptohome if missing or mounts existing one and
  // notifies consumer on the success/failure.
  virtual void LoginAsLocallyManagedUser(
      const UserContext& user_context) OVERRIDE;

  // Initiates retail mode login.
  // Mounts tmpfs and notifies consumer on the success/failure.
  virtual void LoginRetailMode() OVERRIDE;

  // Initiates incognito ("browse without signing in") login.
  // Mounts tmpfs and notifies consumer on the success/failure.
  virtual void LoginOffTheRecord() OVERRIDE;

  // Initiates login into the public account identified by |username|.
  // Mounts an ephemeral cryptohome and notifies consumer on the
  // success/failure.
  virtual void LoginAsPublicAccount(const std::string& username) OVERRIDE;

  // These methods must be called on the UI thread, as they make DBus calls
  // and also call back to the login UI.
  virtual void OnRetailModeLoginSuccess() OVERRIDE;
  virtual void OnLoginSuccess(bool request_pending) OVERRIDE;
  virtual void OnLoginFailure(const LoginFailure& error) OVERRIDE;
  virtual void RecoverEncryptedData(
      const std::string& old_password) OVERRIDE;
  virtual void ResyncEncryptedData() OVERRIDE;

  // AuthAttemptStateResolver overrides.
  // Attempts to make a decision and call back |consumer_| based on
  // the state we have gathered at the time of call.  If a decision
  // can't be made, defers until the next time this is called.
  // When a decision is made, will call back to |consumer_| on the UI thread.
  //
  // Must be called on the UI thread.
  virtual void Resolve() OVERRIDE;

  void OnOffTheRecordLoginSuccess();
  void OnPasswordChangeDetected();

 protected:
  virtual ~ParallelAuthenticator();

 private:
  friend class ParallelAuthenticatorTest;
  FRIEND_TEST_ALL_PREFIXES(ParallelAuthenticatorTest,
                           ResolveOwnerNeededDirectFailedMount);
  FRIEND_TEST_ALL_PREFIXES(ParallelAuthenticatorTest, ResolveOwnerNeededMount);
  FRIEND_TEST_ALL_PREFIXES(ParallelAuthenticatorTest,
                           ResolveOwnerNeededFailedMount);

  // Returns the AuthState we're in, given the status info we have at
  // the time of call.
  // Must be called on the IO thread.
  AuthState ResolveState();

  // Helper for ResolveState().
  // Given that some cryptohome operation has failed, determine which of the
  // possible failure states we're in.
  // Must be called on the IO thread.
  AuthState ResolveCryptohomeFailureState();

  // Helper for ResolveState().
  // Given that some cryptohome operation has succeeded, determine which of
  // the possible states we're in.
  // Must be called on the IO thread.
  AuthState ResolveCryptohomeSuccessState();

  // Helper for ResolveState().
  // Given that some online auth operation has succeeded, determine which of
  // the possible success states we're in.
  // Must be called on the IO thread.
  AuthState ResolveOnlineSuccessState(AuthState offline_state);

  // Used to disable oauth, used for testing.
  void set_using_oauth(bool value) {
    using_oauth_ = value;
  }

  // Used for testing.
  void set_attempt_state(TestAttemptState* new_state) {  // takes ownership.
    current_state_.reset(new_state);
  }

  // Sets an online attempt for testing.
  void set_online_attempt(OnlineAttempt* attempt) {
    current_online_.reset(attempt);
  }

  // Used for testing to set the expected state of an owner check.
  void SetOwnerState(bool owner_check_finished, bool check_result);

  // checks if the current mounted home contains the owner case and either
  // continues or fails the log-in. Used for policy lost mitigation "safe-mode".
  // Returns true if the owner check has been successful or if it is not needed.
  bool VerifyOwner();

  // Handles completion of the ownership check and continues login.
  void OnOwnershipChecked(DeviceSettingsService::OwnershipStatus status,
                          bool is_owner);

  // Signal login completion status for cases when a new user is added via
  // an external authentication provider (i.e. GAIA extension).
  void ResolveLoginCompletionStatus();

  scoped_ptr<AuthAttemptState> current_state_;
  scoped_ptr<OnlineAttempt> current_online_;
  bool migrate_attempted_;
  bool remove_attempted_;
  bool ephemeral_mount_attempted_;
  bool check_key_attempted_;

  // When the user has changed her password, but gives us the old one, we will
  // be able to mount her cryptohome, but online authentication will fail.
  // This allows us to present the same behavior to the caller, regardless
  // of the order in which we receive these results.
  bool already_reported_success_;
  base::Lock success_lock_;  // A lock around |already_reported_success_|.

  // Flags signaling whether the owner verification has been done and the result
  // of it.
  bool owner_is_verified_;
  bool user_can_login_;

  // True if we use OAuth-based authentication flow.
  bool using_oauth_;

  DISALLOW_COPY_AND_ASSIGN(ParallelAuthenticator);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_PARALLEL_AUTHENTICATOR_H_
