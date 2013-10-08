// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_POLICY_PROFILE_POLICY_CONNECTOR_FACTORY_H_
#define CHROME_BROWSER_POLICY_PROFILE_POLICY_CONNECTOR_FACTORY_H_

#include <map>

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/singleton.h"
#include "components/browser_context_keyed_service/browser_context_keyed_base_factory.h"

class Profile;

namespace base {
class SequencedTaskRunner;
}

namespace content {
class BrowserContext;
}

namespace policy {

class ProfilePolicyConnector;

// Creates ProfilePolicyConnectors for Profiles, which manage the common
// policy providers and other policy components.
// TODO(joaodasilva): convert this class to a proper PKS once the PrefService,
// which depends on this class, becomes a PKS too.
class ProfilePolicyConnectorFactory : public BrowserContextKeyedBaseFactory {
 public:
  // Returns the ProfilePolicyConnectorFactory singleton.
  static ProfilePolicyConnectorFactory* GetInstance();

  // Returns the ProfilePolicyConnector associated with |profile|. This is only
  // valid before |profile| is shut down.
  static ProfilePolicyConnector* GetForProfile(Profile* profile);

  // Creates a new ProfilePolicyConnector for |profile|, which must be managed
  // by the caller. Subsequent calls to GetForProfile() will return the instance
  // created, as long as it lives.
  // If |force_immediate_load| is true then policy is loaded synchronously on
  // startup.
  static scoped_ptr<ProfilePolicyConnector> CreateForProfile(
      Profile* profile,
      bool force_immediate_load,
      base::SequencedTaskRunner* sequenced_task_runner);

  // Overrides the |connector| for the given |profile|; use only in tests.
  // Once this class becomes a proper PKS then it can reuse the testing
  // methods of BrowserContextKeyedServiceFactory.
  void SetServiceForTesting(Profile* profile,
                            ProfilePolicyConnector* connector);

 private:
  friend struct DefaultSingletonTraits<ProfilePolicyConnectorFactory>;

  ProfilePolicyConnectorFactory();
  virtual ~ProfilePolicyConnectorFactory();

  ProfilePolicyConnector* GetForProfileInternal(Profile* profile);

  scoped_ptr<ProfilePolicyConnector> CreateForProfileInternal(
      Profile* profile,
      bool force_immediate_load,
      base::SequencedTaskRunner* sequenced_task_runner);

  // BrowserContextKeyedBaseFactory:
  virtual void BrowserContextShutdown(
      content::BrowserContext* context) OVERRIDE;
  virtual void BrowserContextDestroyed(
      content::BrowserContext* context) OVERRIDE;
  virtual void RegisterProfilePrefs(
      user_prefs::PrefRegistrySyncable* registry) OVERRIDE;
  virtual void SetEmptyTestingFactory(
      content::BrowserContext* context) OVERRIDE;
  virtual void CreateServiceNow(content::BrowserContext* context) OVERRIDE;

  typedef std::map<Profile*, ProfilePolicyConnector*> ConnectorMap;
  ConnectorMap connectors_;

  DISALLOW_COPY_AND_ASSIGN(ProfilePolicyConnectorFactory);
};

}  // namespace policy

#endif  // CHROME_BROWSER_POLICY_PROFILE_POLICY_CONNECTOR_FACTORY_H_
