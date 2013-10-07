// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_POLICY_USER_CLOUD_POLICY_MANAGER_CHROMEOS_H_
#define CHROME_BROWSER_CHROMEOS_POLICY_USER_CLOUD_POLICY_MANAGER_CHROMEOS_H_

#include <string>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/policy/cloud/cloud_policy_client.h"
#include "chrome/browser/policy/cloud/cloud_policy_constants.h"
#include "chrome/browser/policy/cloud/cloud_policy_manager.h"
#include "chrome/browser/policy/cloud/cloud_policy_service.h"
#include "chrome/browser/policy/cloud/component_cloud_policy_service.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service.h"

class GoogleServiceAuthError;
class PrefService;

namespace net {
class URLRequestContextGetter;
}

namespace policy {

class DeviceManagementService;
class PolicyOAuth2TokenFetcher;
class ResourceCache;

// UserCloudPolicyManagerChromeOS implements logic for initializing user policy
// on Chrome OS.
class UserCloudPolicyManagerChromeOS
    : public CloudPolicyManager,
      public CloudPolicyClient::Observer,
      public CloudPolicyService::Observer,
      public ComponentCloudPolicyService::Delegate,
      public BrowserContextKeyedService {
 public:
  // If |wait_for_policy_fetch| is true, IsInitializationComplete() will return
  // false as long as there hasn't been a successful policy fetch.
  UserCloudPolicyManagerChromeOS(
      scoped_ptr<CloudPolicyStore> store,
      scoped_ptr<ResourceCache> resource_cache,
      bool wait_for_policy_fetch);
  virtual ~UserCloudPolicyManagerChromeOS();

  // Initializes the cloud connection. |local_state| and
  // |device_management_service| must stay valid until this object is deleted.
  void Connect(PrefService* local_state,
               DeviceManagementService* device_management_service,
               scoped_refptr<net::URLRequestContextGetter> request_context,
               UserAffiliation user_affiliation);

  // This class is one of the policy providers, and must be ready for the
  // creation of the Profile's PrefService; all the other
  // BrowserContextKeyedServices depend on the PrefService, so this class can't
  // depend on other BCKS to avoid a circular dependency. So instead of using
  // the ProfileOAuth2TokenService directly to get the access token, a 3rd
  // service (UserCloudPolicyTokenForwarder) will fetch it later and pass it
  // to this method once available.
  // The |access_token| can then be used to authenticate the registration
  // request to the DMServer.
  void OnAccessTokenAvailable(const std::string& access_token);

  // Returns true if the underlying CloudPolicyClient is already registered.
  bool IsClientRegistered() const;

  // ConfigurationPolicyProvider:
  virtual void Shutdown() OVERRIDE;
  virtual bool IsInitializationComplete(PolicyDomain domain) const OVERRIDE;
  virtual void RegisterPolicyDomain(
      scoped_refptr<const PolicyDomainDescriptor> descriptor) OVERRIDE;

  // CloudPolicyManager:
  virtual scoped_ptr<PolicyBundle> CreatePolicyBundle() OVERRIDE;

  // CloudPolicyService::Observer:
  virtual void OnInitializationCompleted(CloudPolicyService* service) OVERRIDE;

  // CloudPolicyClient::Observer:
  virtual void OnPolicyFetched(CloudPolicyClient* client) OVERRIDE;
  virtual void OnRegistrationStateChanged(CloudPolicyClient* client) OVERRIDE;
  virtual void OnClientError(CloudPolicyClient* client) OVERRIDE;

  // ComponentCloudPolicyService::Delegate:
  virtual void OnComponentCloudPolicyRefreshNeeded() OVERRIDE;
  virtual void OnComponentCloudPolicyUpdated() OVERRIDE;

 private:
  // Fetches a policy token using the authentication context of the signin
  // Profile, and calls back to OnOAuth2PolicyTokenFetched when done.
  void FetchPolicyOAuthTokenUsingSigninProfile();

  // Called once the policy access token is available, and starts the
  // registration with the policy server if the token was successfully fetched.
  void OnOAuth2PolicyTokenFetched(const std::string& policy_token,
                                  const GoogleServiceAuthError& error);

  // Completion handler for the explicit policy fetch triggered on startup in
  // case |wait_for_policy_fetch_| is true. |success| is true if the fetch was
  // successful.
  void OnInitialPolicyFetchComplete(bool success);

  // Cancels waiting for the policy fetch and flags the
  // ConfigurationPolicyProvider ready (assuming all other initialization tasks
  // have completed).
  void CancelWaitForPolicyFetch();

  void StartRefreshSchedulerIfReady();

  // Owns the store, note that CloudPolicyManager just keeps a plain pointer.
  scoped_ptr<CloudPolicyStore> store_;

  // Handles fetching and storing cloud policy for components. It uses the
  // |store_|, so destroy it first.
  scoped_ptr<ComponentCloudPolicyService> component_policy_service_;

  // Whether to wait for a policy fetch to complete before reporting
  // IsInitializationComplete().
  bool wait_for_policy_fetch_;

  // The pref service to pass to the refresh scheduler on initialization.
  PrefService* local_state_;

  // Used to fetch the policy OAuth token, when necessary. This object holds
  // a callback with an unretained reference to the manager, when it exists.
  scoped_ptr<PolicyOAuth2TokenFetcher> token_fetcher_;

  // The access token passed to OnAccessTokenAvailable. It is stored here so
  // that it can be used if OnInitializationCompleted is called later.
  std::string access_token_;

  DISALLOW_COPY_AND_ASSIGN(UserCloudPolicyManagerChromeOS);
};

}  // namespace policy

#endif  // CHROME_BROWSER_CHROMEOS_POLICY_USER_CLOUD_POLICY_MANAGER_CHROMEOS_H_
