// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_POLICY_MOCK_POLICY_SERVICE_H_
#define CHROME_BROWSER_POLICY_MOCK_POLICY_SERVICE_H_

#include "chrome/browser/policy/policy_domain_descriptor.h"
#include "chrome/browser/policy/policy_service.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace policy {

class MockPolicyServiceObserver : public PolicyService::Observer {
 public:
  MockPolicyServiceObserver();
  virtual ~MockPolicyServiceObserver();

  MOCK_METHOD3(OnPolicyUpdated, void(const PolicyNamespace&,
                                     const PolicyMap& previous,
                                     const PolicyMap& current));
  MOCK_METHOD1(OnPolicyServiceInitialized, void(PolicyDomain));
};

class MockPolicyService : public PolicyService {
 public:
  MockPolicyService();
  virtual ~MockPolicyService();

  MOCK_METHOD2(AddObserver, void(PolicyDomain, Observer*));
  MOCK_METHOD2(RemoveObserver, void(PolicyDomain, Observer*));

  MOCK_METHOD1(RegisterPolicyDomain,
               void(scoped_refptr<const PolicyDomainDescriptor>));

  MOCK_CONST_METHOD1(GetPolicies, const PolicyMap&(const PolicyNamespace&));
  MOCK_CONST_METHOD1(GetPolicyDomainDescriptor,
                     scoped_refptr<const PolicyDomainDescriptor>(PolicyDomain));
  MOCK_CONST_METHOD1(IsInitializationComplete, bool(PolicyDomain domain));
  MOCK_METHOD1(RefreshPolicies, void(const base::Closure&));
};

}  // namespace policy

#endif  // CHROME_BROWSER_POLICY_MOCK_POLICY_SERVICE_H_
