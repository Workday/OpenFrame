// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_POLICY_STUB_ENTERPRISE_INSTALL_ATTRIBUTES_H_
#define CHROME_BROWSER_CHROMEOS_POLICY_STUB_ENTERPRISE_INSTALL_ATTRIBUTES_H_

#include <string>

#include "base/basictypes.h"
#include "chrome/browser/chromeos/policy/enterprise_install_attributes.h"
#include "chrome/browser/policy/cloud/cloud_policy_constants.h"

namespace policy {

// This class allows tests to override specific values for easier testing.
// To make IsEnterpriseDevice() return true, set a non-empty registration
// user.
class StubEnterpriseInstallAttributes : public EnterpriseInstallAttributes {
 public:
  StubEnterpriseInstallAttributes();

  void SetDomain(const std::string& domain);
  void SetRegistrationUser(const std::string& user);
  void SetDeviceId(const std::string& id);
  void SetMode(DeviceMode mode);

 private:
  DISALLOW_COPY_AND_ASSIGN(StubEnterpriseInstallAttributes);
};

}  // namespace policy

#endif  // CHROME_BROWSER_CHROMEOS_POLICY_STUB_ENTERPRISE_INSTALL_ATTRIBUTES_H_
