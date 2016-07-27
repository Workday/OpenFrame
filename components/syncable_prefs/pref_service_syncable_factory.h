// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SYNCABLE_PREFS_PREF_SERVICE_SYNCABLE_FACTORY_H_
#define COMPONENTS_SYNCABLE_PREFS_PREF_SERVICE_SYNCABLE_FACTORY_H_

#include "base/prefs/pref_service_factory.h"

namespace base {
class CommandLine;
}

namespace policy {
class BrowserPolicyConnector;
class PolicyService;
}

namespace user_prefs {
class PrefRegistrySyncable;
}

namespace syncable_prefs {

class PrefModelAssociatorClient;
class PrefServiceSyncable;

// A PrefServiceFactory that also knows how to build a
// PrefServiceSyncable, and may know about Chrome concepts such as
// PolicyService.
class PrefServiceSyncableFactory : public base::PrefServiceFactory {
 public:
  PrefServiceSyncableFactory();
  ~PrefServiceSyncableFactory() override;

#if defined(ENABLE_CONFIGURATION_POLICY)
  // Set up policy pref stores using the given policy service and connector.
  void SetManagedPolicies(policy::PolicyService* service,
                          policy::BrowserPolicyConnector* connector);
  void SetRecommendedPolicies(policy::PolicyService* service,
                              policy::BrowserPolicyConnector* connector);
#endif

  void SetPrefModelAssociatorClient(
      PrefModelAssociatorClient* pref_model_associator_client);

  scoped_ptr<PrefServiceSyncable> CreateSyncable(
      user_prefs::PrefRegistrySyncable* registry);

 private:
  PrefModelAssociatorClient* pref_model_associator_client_;

  DISALLOW_COPY_AND_ASSIGN(PrefServiceSyncableFactory);
};

}  // namespace syncable_prefs

#endif  // COMPONENTS_SYNCABLE_PREFS_PREF_SERVICE_SYNCABLE_FACTORY_H_
