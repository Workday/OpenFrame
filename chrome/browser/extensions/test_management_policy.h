// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_TEST_MANAGEMENT_POLICY_H_
#define CHROME_BROWSER_EXTENSIONS_TEST_MANAGEMENT_POLICY_H_

#include <string>

#include "base/strings/string16.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/management_policy.h"

namespace extensions {
// This class provides a simple way to create providers with specific
// restrictions and a known error message, for use in testing.
class TestManagementPolicyProvider : public ManagementPolicy::Provider {
 public:
  enum AllowedActionFlag {
    ALLOW_ALL = 0,
    PROHIBIT_LOAD = 1 << 0,
    PROHIBIT_MODIFY_STATUS = 1 << 1,
    MUST_REMAIN_ENABLED = 1 << 2
  };

  static std::string expected_error() {
    return "Action prohibited by test provider.";
  }

  TestManagementPolicyProvider();
  explicit TestManagementPolicyProvider(int prohibited_actions);

  void SetProhibitedActions(int prohibited_actions);

  virtual std::string GetDebugPolicyProviderName() const OVERRIDE;

  virtual bool UserMayLoad(const Extension* extension,
                           string16* error) const OVERRIDE;

  virtual bool UserMayModifySettings(const Extension* extension,
                                     string16* error) const OVERRIDE;

  virtual bool MustRemainEnabled(const Extension* extension,
                                 string16* error) const OVERRIDE;

 private:
  bool may_load_;
  bool may_modify_status_;
  bool must_remain_enabled_;

  string16 error_message_;
};
}  // namespace
#endif  // CHROME_BROWSER_EXTENSIONS_TEST_MANAGEMENT_POLICY_H_
