// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PASSWORD_MANAGER_PASSWORD_STORE_X_H_
#define CHROME_BROWSER_PASSWORD_MANAGER_PASSWORD_STORE_X_H_

#include <vector>

#include "base/memory/scoped_ptr.h"
#include "base/time/time.h"
#include "chrome/browser/password_manager/password_store_default.h"

class LoginDatabase;
class PrefService;
class Profile;

namespace user_prefs {
class PrefRegistrySyncable;
}

// PasswordStoreX is used on Linux and other non-Windows, non-Mac OS X
// operating systems. It uses a "native backend" to actually store the password
// data when such a backend is available, and otherwise falls back to using the
// login database like PasswordStoreDefault. It also handles automatically
// migrating password data to a native backend from the login database.
//
// There are currently native backends for GNOME Keyring and KWallet.
class PasswordStoreX : public PasswordStoreDefault {
 public:
  // NativeBackends more or less implement the PaswordStore interface, but
  // with return values rather than implicit consumer notification.
  class NativeBackend {
   public:
    typedef std::vector<content::PasswordForm*> PasswordFormList;

    virtual ~NativeBackend() {}

    virtual bool Init() = 0;

    virtual bool AddLogin(const content::PasswordForm& form) = 0;
    virtual bool UpdateLogin(const content::PasswordForm& form) = 0;
    virtual bool RemoveLogin(const content::PasswordForm& form) = 0;
    virtual bool RemoveLoginsCreatedBetween(const base::Time& delete_begin,
                                            const base::Time& delete_end) = 0;
    virtual bool GetLogins(const content::PasswordForm& form,
                           PasswordFormList* forms) = 0;
    virtual bool GetLoginsCreatedBetween(const base::Time& get_begin,
                                         const base::Time& get_end,
                                         PasswordFormList* forms) = 0;
    virtual bool GetAutofillableLogins(PasswordFormList* forms) = 0;
    virtual bool GetBlacklistLogins(PasswordFormList* forms) = 0;
  };

  // Takes ownership of |login_db| and |backend|. |backend| may be NULL in which
  // case this PasswordStoreX will act the same as PasswordStoreDefault.
  PasswordStoreX(LoginDatabase* login_db,
                 Profile* profile,
                 NativeBackend* backend);

#if !defined(OS_MACOSX) && !defined(OS_CHROMEOS) && defined(OS_POSIX)
  // Registers the pref setting used for the methods below.
  static void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

  // Returns true if passwords have been tagged with the local profile id.
  static bool PasswordsUseLocalProfileId(PrefService* prefs);

  // Sets the persistent bit indicating that passwords have been tagged with the
  // local profile id. This cannot be unset; passwords get migrated only once.
  // The caller promises that |prefs| will not be deleted any time soon.
  static void SetPasswordsUseLocalProfileId(PrefService* prefs);
#endif  // !defined(OS_MACOSX) && !defined(OS_CHROMEOS) && defined(OS_POSIX)

 private:
  friend class PasswordStoreXTest;

  virtual ~PasswordStoreX();

  // Implements PasswordStore interface.
  virtual void AddLoginImpl(const content::PasswordForm& form) OVERRIDE;
  virtual void UpdateLoginImpl(
      const content::PasswordForm& form) OVERRIDE;
  virtual void RemoveLoginImpl(
      const content::PasswordForm& form) OVERRIDE;
  virtual void RemoveLoginsCreatedBetweenImpl(
      const base::Time& delete_begin, const base::Time& delete_end) OVERRIDE;
  virtual void GetLoginsImpl(
      const content::PasswordForm& form,
      const ConsumerCallbackRunner& callback_runner) OVERRIDE;
  virtual void GetAutofillableLoginsImpl(GetLoginsRequest* request) OVERRIDE;
  virtual void GetBlacklistLoginsImpl(GetLoginsRequest* request) OVERRIDE;
  virtual bool FillAutofillableLogins(
      std::vector<content::PasswordForm*>* forms) OVERRIDE;
  virtual bool FillBlacklistLogins(
      std::vector<content::PasswordForm*>* forms) OVERRIDE;

  // Sort logins by origin, like the ORDER BY clause in login_database.cc.
  void SortLoginsByOrigin(NativeBackend::PasswordFormList* list);

  // Check to see whether migration is necessary, and perform it if so.
  void CheckMigration();

  // Return true if we should try using the native backend.
  bool use_native_backend() { return !!backend_.get(); }

  // Return true if we can fall back on the default store, warning the first
  // time we call it when falling back is necessary. See |allow_fallback_|.
  bool allow_default_store();

  // Synchronously migrates all the passwords stored in the login database to
  // the native backend. If successful, the login database will be left with no
  // stored passwords, and the number of passwords migrated will be returned.
  // (This might be 0 if migration was not necessary.) Returns < 0 on failure.
  ssize_t MigrateLogins();

  // The native backend in use, or NULL if none.
  scoped_ptr<NativeBackend> backend_;
  // Whether we have already attempted migration to the native store.
  bool migration_checked_;
  // Whether we should allow falling back to the default store. If there is
  // nothing to migrate, then the first attempt to use the native store will
  // be the first time we try to use it and we should allow falling back. If
  // we have migrated successfully, then we do not allow falling back.
  bool allow_fallback_;

  DISALLOW_COPY_AND_ASSIGN(PasswordStoreX);
};

#endif  // CHROME_BROWSER_PASSWORD_MANAGER_PASSWORD_STORE_X_H_
