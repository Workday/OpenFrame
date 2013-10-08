// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_POLICY_USER_CLOUD_POLICY_TOKEN_FORWARDER_FACTORY_H_
#define CHROME_BROWSER_CHROMEOS_POLICY_USER_CLOUD_POLICY_TOKEN_FORWARDER_FACTORY_H_

#include "base/basictypes.h"
#include "base/memory/singleton.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service_factory.h"

namespace content {
class BrowserContext;
}

namespace policy {

// Creates instances of UserCloudPolicyTokenForwarder for Profiles that may need
// to fetch the policy token.
class UserCloudPolicyTokenForwarderFactory
    : public BrowserContextKeyedServiceFactory {
 public:
  // Returns an instance of the UserCloudPolicyTokenForwarderFactory singleton.
  static UserCloudPolicyTokenForwarderFactory* GetInstance();

 private:
  friend struct DefaultSingletonTraits<UserCloudPolicyTokenForwarderFactory>;

  UserCloudPolicyTokenForwarderFactory();
  virtual ~UserCloudPolicyTokenForwarderFactory();

  // BrowserContextKeyedServiceFactory:
  virtual BrowserContextKeyedService* BuildServiceInstanceFor(
      content::BrowserContext* context) const OVERRIDE;
  virtual bool ServiceIsCreatedWithBrowserContext() const OVERRIDE;
  virtual bool ServiceIsNULLWhileTesting() const OVERRIDE;

  DISALLOW_COPY_AND_ASSIGN(UserCloudPolicyTokenForwarderFactory);
};

}  // namespace policy

#endif  // CHROME_BROWSER_CHROMEOS_POLICY_USER_CLOUD_POLICY_TOKEN_FORWARDER_FACTORY_H_
