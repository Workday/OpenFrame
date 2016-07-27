// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/user_manager/fake_user_manager.h"

#include "base/callback.h"
#include "base/task_runner.h"
#include "components/user_manager/user_type.h"
#include "ui/base/resource/resource_bundle.h"

namespace {

class FakeTaskRunner : public base::TaskRunner {
 public:
  bool PostDelayedTask(const tracked_objects::Location& from_here,
                       const base::Closure& task,
                       base::TimeDelta delay) override {
    task.Run();
    return true;
  }
  bool RunsTasksOnCurrentThread() const override { return true; }

 protected:
  ~FakeTaskRunner() override {}
};

}  // namespace

namespace user_manager {

FakeUserManager::FakeUserManager()
    : UserManagerBase(new FakeTaskRunner(), new FakeTaskRunner()),
      primary_user_(nullptr) {}

FakeUserManager::~FakeUserManager() {
}

const user_manager::User* FakeUserManager::AddUser(
    const AccountId& account_id) {
  return AddUserWithAffiliation(account_id, false);
}

const user_manager::User* FakeUserManager::AddUserWithAffiliation(
    const AccountId& account_id,
    bool is_affiliated) {
  user_manager::User* user = user_manager::User::CreateRegularUser(account_id);
  user->set_affiliation(is_affiliated);
  users_.push_back(user);
  return user;
}

void FakeUserManager::RemoveUserFromList(const AccountId& account_id) {
  user_manager::UserList::iterator it = users_.begin();
  // TODO (alemate): Chenge this to GetAccountId(), once a real AccountId is
  // passed. crbug.com/546876
  while (it != users_.end() && (*it)->GetEmail() != account_id.GetUserEmail())
    ++it;
  if (it != users_.end()) {
    delete *it;
    users_.erase(it);
  }
}

const user_manager::UserList& FakeUserManager::GetUsers() const {
  return users_;
}

user_manager::UserList FakeUserManager::GetUsersAllowedForMultiProfile() const {
  user_manager::UserList result;
  for (user_manager::UserList::const_iterator it = users_.begin();
       it != users_.end(); ++it) {
    if ((*it)->GetType() == user_manager::USER_TYPE_REGULAR &&
        !(*it)->is_logged_in())
      result.push_back(*it);
  }
  return result;
}

const user_manager::UserList& FakeUserManager::GetLoggedInUsers() const {
  return logged_in_users_;
}

void FakeUserManager::UserLoggedIn(const AccountId& account_id,
                                   const std::string& username_hash,
                                   bool browser_restart) {
  for (user_manager::UserList::const_iterator it = users_.begin();
       it != users_.end(); ++it) {
    if ((*it)->username_hash() == username_hash) {
      (*it)->set_is_logged_in(true);
      (*it)->set_profile_is_created();
      logged_in_users_.push_back(*it);

      if (!primary_user_)
        primary_user_ = *it;
      break;
    }
  }
}

user_manager::User* FakeUserManager::GetActiveUserInternal() const {
  if (!users_.empty()) {
    if (active_account_id_.is_valid()) {
      for (user_manager::UserList::const_iterator it = users_.begin();
           it != users_.end(); ++it) {
        // TODO (alemate): Chenge this to GetAccountId(), once a real AccountId
        // is
        // passed. crbug.com/546876
        if ((*it)->GetEmail() == active_account_id_.GetUserEmail())
          return *it;
      }
    }
    return users_[0];
  }
  return nullptr;
}

const user_manager::User* FakeUserManager::GetActiveUser() const {
  return GetActiveUserInternal();
}

user_manager::User* FakeUserManager::GetActiveUser() {
  return GetActiveUserInternal();
}

void FakeUserManager::SwitchActiveUser(const AccountId& account_id) {}

void FakeUserManager::SaveUserDisplayName(const AccountId& account_id,
                                          const base::string16& display_name) {
  for (user_manager::UserList::iterator it = users_.begin(); it != users_.end();
       ++it) {
    // TODO (alemate): Chenge this to GetAccountId(), once a real AccountId is
    // passed. crbug.com/546876
    if ((*it)->GetEmail() == account_id.GetUserEmail()) {
      (*it)->set_display_name(display_name);
      return;
    }
  }
}

const user_manager::UserList& FakeUserManager::GetLRULoggedInUsers() const {
  return users_;
}

user_manager::UserList FakeUserManager::GetUnlockUsers() const {
  return users_;
}

const AccountId& FakeUserManager::GetOwnerAccountId() const {
  return owner_account_id_;
}

bool FakeUserManager::IsKnownUser(const AccountId& account_id) const {
  return true;
}

const user_manager::User* FakeUserManager::FindUser(
    const AccountId& account_id) const {
  const user_manager::UserList& users = GetUsers();
  for (user_manager::UserList::const_iterator it = users.begin();
       it != users.end(); ++it) {
    // TODO (alemate): Chenge this to GetAccountId(), once a real AccountId is
    // passed. crbug.com/546876
    if ((*it)->GetEmail() == account_id.GetUserEmail())
      return *it;
  }
  return nullptr;
}

user_manager::User* FakeUserManager::FindUserAndModify(
    const AccountId& account_id) {
  return nullptr;
}

const user_manager::User* FakeUserManager::GetLoggedInUser() const {
  return nullptr;
}

user_manager::User* FakeUserManager::GetLoggedInUser() {
  return nullptr;
}

const user_manager::User* FakeUserManager::GetPrimaryUser() const {
  return primary_user_;
}

base::string16 FakeUserManager::GetUserDisplayName(
    const AccountId& account_id) const {
  return base::string16();
}

std::string FakeUserManager::GetUserDisplayEmail(
    const AccountId& account_id) const {
  return std::string();
}

bool FakeUserManager::IsCurrentUserOwner() const {
  return false;
}

bool FakeUserManager::IsCurrentUserNew() const {
  return false;
}

bool FakeUserManager::IsCurrentUserNonCryptohomeDataEphemeral() const {
  return false;
}

bool FakeUserManager::CanCurrentUserLock() const {
  return false;
}

bool FakeUserManager::IsUserLoggedIn() const {
  return logged_in_users_.size() > 0;
}

bool FakeUserManager::IsLoggedInAsUserWithGaiaAccount() const {
  return true;
}

bool FakeUserManager::IsLoggedInAsPublicAccount() const {
  return false;
}

bool FakeUserManager::IsLoggedInAsGuest() const {
  return false;
}

bool FakeUserManager::IsLoggedInAsSupervisedUser() const {
  return false;
}

bool FakeUserManager::IsLoggedInAsKioskApp() const {
  const user_manager::User* active_user = GetActiveUser();
  return active_user
             ? active_user->GetType() == user_manager::USER_TYPE_KIOSK_APP
             : false;
}

bool FakeUserManager::IsLoggedInAsStub() const {
  return false;
}

bool FakeUserManager::IsSessionStarted() const {
  return false;
}

bool FakeUserManager::IsUserNonCryptohomeDataEphemeral(
    const AccountId& account_id) const {
  return false;
}

bool FakeUserManager::AreSupervisedUsersAllowed() const {
  return true;
}

bool FakeUserManager::AreEphemeralUsersEnabled() const {
  return false;
}

const std::string& FakeUserManager::GetApplicationLocale() const {
  static const std::string default_locale("en-US");
  return default_locale;
}

PrefService* FakeUserManager::GetLocalState() const {
  return nullptr;
}

bool FakeUserManager::IsEnterpriseManaged() const {
  return false;
}

bool FakeUserManager::IsDemoApp(const AccountId& account_id) const {
  return false;
}

bool FakeUserManager::IsKioskApp(const AccountId& account_id) const {
  return false;
}

bool FakeUserManager::IsPublicAccountMarkedForRemoval(
    const AccountId& account_id) const {
  return false;
}

}  // namespace user_manager
