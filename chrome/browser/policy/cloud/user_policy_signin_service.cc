// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/cloud/user_policy_signin_service.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/callback.h"
#include "base/prefs/pref_service.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/policy/cloud/cloud_policy_client_registration_helper.h"
#include "chrome/browser/policy/cloud/user_cloud_policy_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/signin/signin_manager.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "chrome/browser/signin/token_service.h"
#include "chrome/browser/signin/token_service_factory.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_source.h"
#include "google_apis/gaia/gaia_constants.h"

namespace policy {

UserPolicySigninService::UserPolicySigninService(
    Profile* profile) : UserPolicySigninServiceBase(profile) {
  if (profile->GetPrefs()->GetBoolean(prefs::kDisableCloudPolicyOnSignin))
    return;

  // Listen for an OAuth token to become available so we can register a client
  // if for some reason the client is not already registered (for example, if
  // the policy load failed during initial signin).
  registrar()->Add(this,
                   chrome::NOTIFICATION_TOKEN_AVAILABLE,
                   content::Source<TokenService>(
                       TokenServiceFactory::GetForProfile(profile)));

  // TokenService should not yet have loaded its tokens since this happens in
  // the background after PKS initialization - so this service should always be
  // created before the oauth token is available.
  DCHECK(!TokenServiceFactory::GetForProfile(profile)->HasOAuthLoginToken());
}

UserPolicySigninService::~UserPolicySigninService() {}

void UserPolicySigninService::Shutdown() {
  // Stop any pending registration helper activity. We do this here instead of
  // in the destructor because we want to shutdown the registration helper
  // before UserCloudPolicyManager shuts down the CloudPolicyClient.
  registration_helper_.reset();
  UserPolicySigninServiceBase::Shutdown();
}

void UserPolicySigninService::RegisterPolicyClient(
    const std::string& username,
    const std::string& oauth2_refresh_token,
    const PolicyRegistrationCallback& callback) {
  DCHECK(!oauth2_refresh_token.empty());

  // Create a new CloudPolicyClient for fetching the DMToken.
  scoped_ptr<CloudPolicyClient> policy_client = PrepareToRegister(username);
  if (!policy_client) {
    callback.Run(policy_client.Pass());
    return;
  }

  // Fire off the registration process. Callback keeps the CloudPolicyClient
  // alive for the length of the registration process.
  registration_helper_.reset(new CloudPolicyClientRegistrationHelper(
      profile()->GetRequestContext(),
      policy_client.get(),
      ShouldForceLoadPolicy(),
      enterprise_management::DeviceRegisterRequest::BROWSER));
  registration_helper_->StartRegistrationWithLoginToken(
      oauth2_refresh_token,
      base::Bind(&UserPolicySigninService::CallPolicyRegistrationCallback,
                 base::Unretained(this),
                 base::Passed(&policy_client),
                 callback));
}

void UserPolicySigninService::CallPolicyRegistrationCallback(
    scoped_ptr<CloudPolicyClient> client,
    PolicyRegistrationCallback callback) {
  registration_helper_.reset();
  if (!client->is_registered()) {
    // Registration failed, so free the client and pass NULL to the callback.
    client.reset();
  }
  callback.Run(client.Pass());
}

void UserPolicySigninService::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {

  if (profile()->IsManaged()) {
    registrar()->RemoveAll();
    return;
  }

  // If using a TestingProfile with no SigninManager or UserCloudPolicyManager,
  // skip initialization.
  if (!GetManager() || !SigninManagerFactory::GetForProfile(profile())) {
    DVLOG(1) << "Skipping initialization for tests due to missing components.";
    return;
  }

  switch (type) {
    case chrome::NOTIFICATION_TOKEN_AVAILABLE: {
      const TokenService::TokenAvailableDetails& token_details =
          *(content::Details<const TokenService::TokenAvailableDetails>(
              details).ptr());
      if (token_details.service() ==
              GaiaConstants::kGaiaOAuth2LoginRefreshToken) {
        SigninManager* signin_manager =
            SigninManagerFactory::GetForProfile(profile());
        std::string username = signin_manager->GetAuthenticatedUsername();
        // Should not have GAIA tokens if the user isn't signed in.
        DCHECK(!username.empty());
        // TokenService now has a refresh token (implying that the user is
        // signed in) so initialize the UserCloudPolicyManager.
        InitializeForSignedInUser(username);
      }
      break;
    }
    default:
      UserPolicySigninServiceBase::Observe(type, source, details);
  }
}

void UserPolicySigninService::InitializeUserCloudPolicyManager(
    scoped_ptr<CloudPolicyClient> client) {
  UserPolicySigninServiceBase::InitializeUserCloudPolicyManager(client.Pass());
  ProhibitSignoutIfNeeded();
}

void UserPolicySigninService::ShutdownUserCloudPolicyManager() {
  UserCloudPolicyManager* manager = GetManager();
  if (manager) {
    // Allow the user to signout again.
    SigninManagerFactory::GetForProfile(profile())->ProhibitSignout(false);
  }
  UserPolicySigninServiceBase::ShutdownUserCloudPolicyManager();
}

void UserPolicySigninService::OnInitializationCompleted(
    CloudPolicyService* service) {
  UserCloudPolicyManager* manager = GetManager();
  DCHECK_EQ(service, manager->core()->service());
  DCHECK(service->IsInitializationComplete());
  // The service is now initialized - if the client is not yet registered, then
  // it means that there is no cached policy and so we need to initiate a new
  // client registration.
  DVLOG_IF(1, manager->IsClientRegistered())
      << "Client already registered - not fetching DMToken";
  if (!manager->IsClientRegistered()) {
    std::string token = TokenServiceFactory::GetForProfile(profile())->
        GetOAuth2LoginRefreshToken();
    if (token.empty()) {
      // No token yet - this class listens for NOTIFICATION_TOKEN_AVAILABLE
      // and will re-attempt registration once the token is available.
      DLOG(WARNING) << "No OAuth Refresh Token - delaying policy download";
      return;
    }
    RegisterCloudPolicyService(token);
  }
  // If client is registered now, prohibit signout.
  ProhibitSignoutIfNeeded();
}

void UserPolicySigninService::RegisterCloudPolicyService(
    const std::string& login_token) {
  DCHECK(!GetManager()->IsClientRegistered());
  DVLOG(1) << "Fetching new DM Token";
  // Do nothing if already starting the registration process.
  if (registration_helper_)
    return;

  // Start the process of registering the CloudPolicyClient. Once it completes,
  // policy fetch will automatically happen.
  registration_helper_.reset(new CloudPolicyClientRegistrationHelper(
      profile()->GetRequestContext(),
      GetManager()->core()->client(),
      ShouldForceLoadPolicy(),
      enterprise_management::DeviceRegisterRequest::BROWSER));
  registration_helper_->StartRegistrationWithLoginToken(
      login_token,
      base::Bind(&UserPolicySigninService::OnRegistrationComplete,
                 base::Unretained(this)));
}

void UserPolicySigninService::OnRegistrationComplete() {
  ProhibitSignoutIfNeeded();
  registration_helper_.reset();
}

void UserPolicySigninService::ProhibitSignoutIfNeeded() {
  if (GetManager()->IsClientRegistered()) {
    DVLOG(1) << "User is registered for policy - prohibiting signout";
    SigninManager* signin_manager =
        SigninManagerFactory::GetForProfile(profile());
    signin_manager->ProhibitSignout(true);
  }
}

}  // namespace policy
