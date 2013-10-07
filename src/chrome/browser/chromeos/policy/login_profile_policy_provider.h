// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_POLICY_LOGIN_PROFILE_POLICY_PROVIDER_H_
#define CHROME_BROWSER_CHROMEOS_POLICY_LOGIN_PROFILE_POLICY_PROVIDER_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/policy/configuration_policy_provider.h"
#include "chrome/browser/policy/policy_service.h"

namespace policy {

// Policy provider for the login profile. Since the login profile is not
// associated with any user, it does not receive regular user policy. However,
// several device policies that control features on the login screen surface as
// user policies in the login profile.
class LoginProfilePolicyProvider : public ConfigurationPolicyProvider,
                                   public PolicyService::Observer {
 public:
  explicit LoginProfilePolicyProvider(PolicyService* device_policy_service);
  virtual ~LoginProfilePolicyProvider();

  // ConfigurationPolicyProvider:
  virtual void Init() OVERRIDE;
  virtual void Shutdown() OVERRIDE;
  virtual void RefreshPolicies() OVERRIDE;

  // PolicyService::Observer:
  virtual void OnPolicyUpdated(const PolicyNamespace& ns,
                               const PolicyMap& previous,
                               const PolicyMap& current) OVERRIDE;
  virtual void OnPolicyServiceInitialized(PolicyDomain domain) OVERRIDE;

  void OnDevicePolicyRefreshDone();

 private:
  void UpdateFromDevicePolicy();

  PolicyService* device_policy_service_;  // Not owned.

  bool waiting_for_device_policy_refresh_;

  base::WeakPtrFactory<LoginProfilePolicyProvider> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(LoginProfilePolicyProvider);
};

}  // namespace policy

#endif  // CHROME_BROWSER_CHROMEOS_POLICY_LOGIN_PROFILE_POLICY_PROVIDER_H_
