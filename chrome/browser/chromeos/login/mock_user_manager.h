// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_MOCK_USER_MANAGER_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_MOCK_USER_MANAGER_H_

#include <string>

#include "base/files/file_path.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/chromeos/login/mock_user_image_manager.h"
#include "chrome/browser/chromeos/login/user.h"
#include "chrome/browser/chromeos/login/user_flow.h"
#include "chrome/browser/chromeos/login/user_image.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace chromeos {

class MockUserManager : public UserManager {
 public:
  MockUserManager();
  virtual ~MockUserManager();

  MOCK_METHOD0(Shutdown, void(void));
  MOCK_CONST_METHOD0(GetUsers, const UserList&(void));
  MOCK_CONST_METHOD0(GetUsersAdmittedForMultiProfile, UserList(void));
  MOCK_CONST_METHOD0(GetLoggedInUsers, const UserList&(void));
  MOCK_METHOD0(GetLRULoggedInUsers, const UserList&(void));
  MOCK_METHOD3(UserLoggedIn, void(
      const std::string&, const std::string&, bool));
  MOCK_METHOD1(SwitchActiveUser, void(const std::string& email));
  MOCK_METHOD0(SessionStarted, void(void));
  MOCK_METHOD0(RestoreActiveSessions, void(void));
  MOCK_METHOD2(RemoveUser, void(const std::string&, RemoveUserDelegate*));
  MOCK_METHOD1(RemoveUserFromList, void(const std::string&));
  MOCK_CONST_METHOD1(IsKnownUser, bool(const std::string&));
  MOCK_CONST_METHOD1(FindUser, const User*(const std::string&));
  MOCK_CONST_METHOD1(FindLocallyManagedUser, const User*(const string16&));
  MOCK_METHOD2(SaveUserOAuthStatus, void(const std::string&,
                                         User::OAuthTokenStatus));
  MOCK_METHOD2(SaveUserDisplayName, void(const std::string&,
                                         const string16&));
  MOCK_CONST_METHOD1(GetUserDisplayName, string16(const std::string&));
  MOCK_METHOD2(SaveUserDisplayEmail, void(const std::string&,
                                          const std::string&));
  MOCK_CONST_METHOD1(GetUserDisplayEmail, std::string(const std::string&));
  MOCK_CONST_METHOD0(IsCurrentUserOwner, bool(void));
  MOCK_CONST_METHOD0(IsCurrentUserNew, bool(void));
  MOCK_CONST_METHOD0(IsCurrentUserNonCryptohomeDataEphemeral, bool(void));
  MOCK_CONST_METHOD0(CanCurrentUserLock, bool(void));
  MOCK_CONST_METHOD0(IsUserLoggedIn, bool(void));
  MOCK_CONST_METHOD0(IsLoggedInAsRegularUser, bool(void));
  MOCK_CONST_METHOD0(IsLoggedInAsDemoUser, bool(void));
  MOCK_CONST_METHOD0(IsLoggedInAsPublicAccount, bool(void));
  MOCK_CONST_METHOD0(IsLoggedInAsGuest, bool(void));
  MOCK_CONST_METHOD0(IsLoggedInAsLocallyManagedUser, bool(void));
  MOCK_CONST_METHOD0(IsLoggedInAsKioskApp, bool(void));
  MOCK_CONST_METHOD0(IsLoggedInAsStub, bool(void));
  MOCK_CONST_METHOD0(IsSessionStarted, bool(void));
  MOCK_CONST_METHOD0(UserSessionsRestored, bool(void));
  MOCK_CONST_METHOD0(HasBrowserRestarted, bool(void));
  MOCK_CONST_METHOD1(IsUserNonCryptohomeDataEphemeral,
                     bool(const std::string&));
  MOCK_METHOD1(AddObserver, void(UserManager::Observer*));
  MOCK_METHOD1(RemoveObserver, void(UserManager::Observer*));
  MOCK_METHOD1(AddSessionStateObserver,
               void(UserManager::UserSessionStateObserver*));
  MOCK_METHOD1(RemoveSessionStateObserver,
               void(UserManager::UserSessionStateObserver*));
  MOCK_METHOD0(NotifyLocalStateChanged, void(void));
  MOCK_CONST_METHOD0(GetMergeSessionState, MergeSessionState(void));
  MOCK_METHOD1(SetMergeSessionState, void(MergeSessionState));
  MOCK_METHOD2(SetUserFlow, void(const std::string&, UserFlow*));
  MOCK_METHOD1(ResetUserFlow, void(const std::string&));
  MOCK_CONST_METHOD1(GetManagerForManagedUser, std::string(
      const std::string& managed_user_id));
  MOCK_METHOD3(CreateLocallyManagedUserRecord, const User*(
      const std::string&,
      const std::string&,
      const string16&));
  MOCK_CONST_METHOD1(GetManagerDisplayNameForManagedUser, string16(
      const std::string&));
  MOCK_CONST_METHOD1(GetManagerUserIdForManagedUser, std::string(
      const std::string&));
  MOCK_CONST_METHOD1(GetManagerDisplayEmailForManagedUser, std::string(
      const std::string&));
  MOCK_METHOD0(GenerateUniqueLocallyManagedUserId, std::string(void));
  MOCK_METHOD1(StartLocallyManagedUserCreationTransaction,
      void(const string16&));
  MOCK_METHOD1(SetLocallyManagedUserCreationTransactionUserId,
      void(const std::string&));
  MOCK_METHOD0(CommitLocallyManagedUserCreationTransaction, void(void));

  MOCK_METHOD2(GetAppModeChromeClientOAuthInfo, bool(std::string*,
                                                     std::string*));
  MOCK_METHOD2(SetAppModeChromeClientOAuthInfo, void(const std::string&,
                                                     const std::string&));
  MOCK_CONST_METHOD0(AreLocallyManagedUsersAllowed, bool(void));

  // You can't mock these functions easily because nobody can create
  // User objects but the UserManagerImpl and us.
  virtual const User* GetLoggedInUser() const OVERRIDE;
  virtual User* GetLoggedInUser() OVERRIDE;
  virtual const User* GetActiveUser() const OVERRIDE;
  virtual User* GetActiveUser() OVERRIDE;

  virtual UserImageManager* GetUserImageManager() OVERRIDE;

  virtual UserFlow* GetCurrentUserFlow() const OVERRIDE;
  virtual UserFlow* GetUserFlow(const std::string&) const OVERRIDE;

  // Sets a new User instance.
  void SetActiveUser(const std::string& email);

  // Creates a new public session user. Users previously created by this
  // MockUserManager become invalid.
  User* CreatePublicAccountUser(const std::string& email);

  User* user_;
  scoped_ptr<MockUserImageManager> user_image_manager_;
  scoped_ptr<UserFlow> user_flow_;
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_MOCK_USER_MANAGER_H_
