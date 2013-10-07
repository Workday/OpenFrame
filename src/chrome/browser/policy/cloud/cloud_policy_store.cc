// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/cloud/cloud_policy_store.h"

#include "base/hash.h"
#include "base/logging.h"
#include "chrome/browser/policy/cloud/cloud_external_data_manager.h"

namespace policy {

CloudPolicyStore::Observer::~Observer() {}

CloudPolicyStore::CloudPolicyStore()
    : status_(STATUS_OK),
      validation_status_(CloudPolicyValidatorBase::VALIDATION_OK),
      invalidation_version_(0),
      is_initialized_(false),
      policy_changed_(false),
      hash_value_(0) {}

CloudPolicyStore::~CloudPolicyStore() {}

void CloudPolicyStore::Store(
    const enterprise_management::PolicyFetchResponse& policy,
    int64 invalidation_version) {
  invalidation_version_ = invalidation_version;
  Store(policy);
}

void CloudPolicyStore::AddObserver(CloudPolicyStore::Observer* observer) {
  observers_.AddObserver(observer);
}

void CloudPolicyStore::RemoveObserver(CloudPolicyStore::Observer* observer) {
  observers_.RemoveObserver(observer);
}

void CloudPolicyStore::NotifyStoreLoaded() {
  // Determine if the policy changed by comparing the new policy's hash value
  // to the previous.
  uint32 new_hash_value = 0;
  if (policy_ && policy_->has_policy_value())
    new_hash_value = base::Hash(policy_->policy_value());
  policy_changed_ = new_hash_value != hash_value_;
  hash_value_ = new_hash_value;

  is_initialized_ = true;
  // The |external_data_manager_| must be notified first so that when other
  // observers are informed about the changed policies and try to fetch external
  // data referenced by these, the |external_data_manager_| has the required
  // metadata already.
  if (external_data_manager_)
    external_data_manager_->OnPolicyStoreLoaded();
  FOR_EACH_OBSERVER(Observer, observers_, OnStoreLoaded(this));
}

void CloudPolicyStore::NotifyStoreError() {
  is_initialized_ = true;
  FOR_EACH_OBSERVER(Observer, observers_, OnStoreError(this));
}

void CloudPolicyStore::SetExternalDataManager(
    base::WeakPtr<CloudExternalDataManager> external_data_manager) {
  DCHECK(!external_data_manager_);
  external_data_manager_ = external_data_manager;
  if (is_initialized_)
    external_data_manager_->OnPolicyStoreLoaded();
}

}  // namespace policy
