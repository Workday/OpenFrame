// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/mock_configuration_policy_provider.h"

#include <string>

#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "chrome/browser/policy/policy_bundle.h"

namespace policy {

MockConfigurationPolicyProvider::MockConfigurationPolicyProvider() {}

MockConfigurationPolicyProvider::~MockConfigurationPolicyProvider() {}

void MockConfigurationPolicyProvider::UpdateChromePolicy(
    const PolicyMap& policy) {
  scoped_ptr<PolicyBundle> bundle(new PolicyBundle());
  bundle->Get(PolicyNamespace(POLICY_DOMAIN_CHROME, std::string()))
      .CopyFrom(policy);
  UpdatePolicy(bundle.Pass());
  if (base::MessageLoop::current())
    base::RunLoop().RunUntilIdle();
}

MockConfigurationPolicyObserver::MockConfigurationPolicyObserver() {}

MockConfigurationPolicyObserver::~MockConfigurationPolicyObserver() {}

}  // namespace policy
