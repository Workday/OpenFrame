// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/syncable_prefs/pref_service_syncable_factory.h"

#include "base/prefs/default_pref_store.h"
#include "base/prefs/pref_notifier_impl.h"
#include "base/prefs/pref_value_store.h"
#include "base/trace_event/trace_event.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/syncable_prefs/pref_service_syncable.h"

#if defined(ENABLE_CONFIGURATION_POLICY)
#include "components/policy/core/browser/browser_policy_connector.h"
#include "components/policy/core/browser/configuration_policy_pref_store.h"
#include "components/policy/core/common/policy_service.h"
#include "components/policy/core/common/policy_types.h"
#endif

namespace syncable_prefs {

PrefServiceSyncableFactory::PrefServiceSyncableFactory() {
}

PrefServiceSyncableFactory::~PrefServiceSyncableFactory() {
}

#if defined(ENABLE_CONFIGURATION_POLICY)
void PrefServiceSyncableFactory::SetManagedPolicies(
    policy::PolicyService* service,
    policy::BrowserPolicyConnector* connector) {
  set_managed_prefs(new policy::ConfigurationPolicyPrefStore(
      service, connector->GetHandlerList(), policy::POLICY_LEVEL_MANDATORY));
}

void PrefServiceSyncableFactory::SetRecommendedPolicies(
    policy::PolicyService* service,
    policy::BrowserPolicyConnector* connector) {
  set_recommended_prefs(new policy::ConfigurationPolicyPrefStore(
      service, connector->GetHandlerList(), policy::POLICY_LEVEL_RECOMMENDED));
}
#endif

void PrefServiceSyncableFactory::SetPrefModelAssociatorClient(
    PrefModelAssociatorClient* pref_model_associator_client) {
  pref_model_associator_client_ = pref_model_associator_client;
}

scoped_ptr<PrefServiceSyncable> PrefServiceSyncableFactory::CreateSyncable(
    user_prefs::PrefRegistrySyncable* pref_registry) {
  TRACE_EVENT0("browser", "PrefServiceSyncableFactory::CreateSyncable");
  PrefNotifierImpl* pref_notifier = new PrefNotifierImpl();
  scoped_ptr<PrefServiceSyncable> pref_service(
      new PrefServiceSyncable(
          pref_notifier,
          new PrefValueStore(managed_prefs_.get(),
                             supervised_user_prefs_.get(),
                             extension_prefs_.get(),
                             command_line_prefs_.get(),
                             user_prefs_.get(),
                             recommended_prefs_.get(),
                             pref_registry->defaults().get(),
                             pref_notifier),
          user_prefs_.get(),
          pref_registry,
          pref_model_associator_client_,
          read_error_callback_,
          async_));
  return pref_service.Pass();
}

}  // namespace syncable_prefs
