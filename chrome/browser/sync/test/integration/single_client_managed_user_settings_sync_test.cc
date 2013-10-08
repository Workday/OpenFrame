// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/prefs/pref_service.h"
#include "base/values.h"
#include "chrome/browser/managed_mode/managed_user_service.h"
#include "chrome/browser/managed_mode/managed_user_service_factory.h"
#include "chrome/browser/policy/managed_mode_policy_provider.h"
#include "chrome/browser/policy/profile_policy_connector.h"
#include "chrome/browser/policy/profile_policy_connector_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sync/profile_sync_service_harness.h"
#include "chrome/browser/sync/test/integration/sync_test.h"
#include "policy/policy_constants.h"

class SingleClientManagedUserSettingsSyncTest : public SyncTest {
 public:
  SingleClientManagedUserSettingsSyncTest() : SyncTest(SINGLE_CLIENT) {}

  virtual ~SingleClientManagedUserSettingsSyncTest() {}
};

// TODO(pavely): Fix this test.
IN_PROC_BROWSER_TEST_F(SingleClientManagedUserSettingsSyncTest,
                       DISABLED_Sanity) {
  ASSERT_TRUE(SetupClients());
  for (int i = 0; i < num_clients(); ++i) {
    Profile* profile = GetProfile(i);
    ManagedUserServiceFactory::GetForProfile(profile)->InitForTesting();
    // Managed users are prohibited from signing into the browser. Currently
    // that means they're also unable to sync anything, so override that for
    // this test.
    // TODO(pamg): Remove this override (and the several #includes it requires)
    // once sync and signin are properly separated for managed users. See
    // http://crbug.com/239785.
    policy::ProfilePolicyConnector* connector =
        policy::ProfilePolicyConnectorFactory::GetForProfile(profile);
    policy::ManagedModePolicyProvider* policy_provider =
        connector->managed_mode_policy_provider();
    scoped_ptr<base::Value> allow_signin(new base::FundamentalValue(true));
    policy_provider->SetLocalPolicyForTesting(policy::key::kSigninAllowed,
                                              allow_signin.Pass());

    // The user should not be signed in.
    std::string username;
    // ProfileSyncServiceHarness sets the password, which can't be empty.
    std::string password = "password";
    GetClient(i)->SetCredentials(username, password);
  }
  ASSERT_TRUE(SetupSync());
}
