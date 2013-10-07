// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/cloud/user_cloud_policy_manager.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "chrome/browser/policy/cloud/cloud_policy_constants.h"
#include "chrome/browser/policy/cloud/cloud_policy_service.h"
#include "chrome/browser/policy/cloud/user_cloud_policy_manager_factory.h"
#include "chrome/browser/policy/cloud/user_cloud_policy_store.h"
#include "chrome/browser/policy/policy_types.h"
#include "chrome/common/pref_names.h"

namespace em = enterprise_management;

namespace policy {

UserCloudPolicyManager::UserCloudPolicyManager(
    Profile* profile,
    scoped_ptr<UserCloudPolicyStore> store)
    : CloudPolicyManager(
          PolicyNamespaceKey(GetChromeUserPolicyType(), std::string()),
          store.get()),
      profile_(profile),
      store_(store.Pass()) {
  UserCloudPolicyManagerFactory::GetInstance()->Register(profile_, this);
}

UserCloudPolicyManager::~UserCloudPolicyManager() {
  UserCloudPolicyManagerFactory::GetInstance()->Unregister(profile_, this);
}

void UserCloudPolicyManager::Connect(
    PrefService* local_state, scoped_ptr<CloudPolicyClient> client) {
  core()->Connect(client.Pass());
  StartRefreshScheduler();
  core()->TrackRefreshDelayPref(local_state, prefs::kUserPolicyRefreshRate);
}

// static
scoped_ptr<CloudPolicyClient>
UserCloudPolicyManager::CreateCloudPolicyClient(
    DeviceManagementService* device_management_service) {
  return make_scoped_ptr(
      new CloudPolicyClient(std::string(), std::string(),
                            USER_AFFILIATION_NONE,
                            NULL, device_management_service)).Pass();
}

void UserCloudPolicyManager::DisconnectAndRemovePolicy() {
  core()->Disconnect();
  store_->Clear();
}

bool UserCloudPolicyManager::IsClientRegistered() const {
  return client() && client()->is_registered();
}

}  // namespace policy
