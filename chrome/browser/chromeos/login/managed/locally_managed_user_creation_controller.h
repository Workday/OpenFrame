// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_MANAGED_LOCALLY_MANAGED_USER_CREATION_CONTROLLER_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_MANAGED_LOCALLY_MANAGED_USER_CREATION_CONTROLLER_H_

#include <string>

#include "base/files/file_path.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string16.h"
#include "base/timer/timer.h"
#include "chrome/browser/chromeos/login/managed/managed_user_authenticator.h"
#include "chrome/browser/managed_mode/managed_user_registration_utility.h"

class Profile;

namespace chromeos {

// LocallyManagedUserCreationController is used to locally managed user
// creation.
// LMU Creation process:
// 0. Manager is logged in
// 1. Generate ID for new LMU
// 2. Start "transaction" in Local State.
// 3. Create local cryptohome (errors could arise)
// 4. Create user in cloud (errors could arise)
// 5. Store cloud token in cryptohome (actually, error could arise).
// 6. Mark "transaction" as completed.
// 7. End manager session.

class LocallyManagedUserCreationController
    : public ManagedUserAuthenticator::AuthStatusConsumer {
 public:
  enum ErrorCode {
    NO_ERROR,
    CRYPTOHOME_NO_MOUNT,
    CRYPTOHOME_FAILED_MOUNT,
    CRYPTOHOME_FAILED_TPM,
    CLOUD_SERVER_ERROR,
    TOKEN_WRITE_FAILED,
  };

  class StatusConsumer {
   public:
    virtual ~StatusConsumer();

    virtual void OnCreationError(ErrorCode code) = 0;
    virtual void OnCreationTimeout() = 0;
    virtual void OnCreationSuccess() = 0;
  };

  // All UI initialization is deferred till Init() call.
  // |Consumer| is not owned by controller, and it is expected that it wouldn't
  // be deleted before LocallyManagedUserCreationController.
  LocallyManagedUserCreationController(StatusConsumer* consumer,
                                       const std::string& manager_id);
  virtual ~LocallyManagedUserCreationController();

  // Returns the current locally managed user controller if it has been created.
  static LocallyManagedUserCreationController* current_controller() {
    return current_controller_;
  }

  void SetUpCreation(string16 display_name, std::string password);
  void SetManagerProfile(Profile* manager_profile);
  void StartCreation();
  void CancelCreation();
  void FinishCreation();
  std::string GetManagedUserId();

 private:
  // Contains information necessary for new user creation.
  struct UserCreationContext {
    UserCreationContext();
    ~UserCreationContext();

    string16 display_name;
    std::string manager_id;
    std::string user_id;
    std::string password;
    std::string mount_hash;
    std::string master_key;
    bool token_acquired;
    std::string token;
    bool token_succesfully_written;
    Profile* manager_profile;
    scoped_ptr<ManagedUserRegistrationUtility> registration_utility;
  };

  // ManagedUserAuthenticator::StatusConsumer overrides.
  virtual void OnAuthenticationFailure(
      ManagedUserAuthenticator::AuthState error) OVERRIDE;
  virtual void OnMountSuccess(const std::string& mount_hash) OVERRIDE;
  virtual void OnAddKeySuccess() OVERRIDE;

  void CreationTimedOut();
  void RegistrationCallback(const GoogleServiceAuthError& error,
                            const std::string& token);

  void TokenFetched(const std::string& token);

  // Completion callback for StoreManagedUserFiles method.
  // Called on the UI thread.
  void OnManagedUserFilesStored(bool success);

  // Pointer to the current instance of the controller to be used by
  // automation tests.
  static LocallyManagedUserCreationController* current_controller_;

  StatusConsumer* consumer_;

  scoped_refptr<ManagedUserAuthenticator> authenticator_;

  // Creation context. Not null while creating new LMU.
  scoped_ptr<UserCreationContext> creation_context_;

  // Timer for showing warning if creation process takes too long.
  base::OneShotTimer<LocallyManagedUserCreationController> timeout_timer_;

  // Factory of callbacks.
  base::WeakPtrFactory<LocallyManagedUserCreationController> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(LocallyManagedUserCreationController);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_MANAGED_LOCALLY_MANAGED_USER_CREATION_CONTROLLER_H_
