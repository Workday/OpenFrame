// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_STANDARD_MANAGEMENT_POLICY_PROVIDER_H_
#define CHROME_BROWSER_EXTENSIONS_STANDARD_MANAGEMENT_POLICY_PROVIDER_H_

#include "chrome/browser/extensions/management_policy.h"

namespace extensions {

class Blacklist;
class Extension;
class ExtensionPrefs;

// The standard management policy provider, which takes into account the
// extension black/whitelists and admin black/whitelists.
class StandardManagementPolicyProvider : public ManagementPolicy::Provider {
 public:
  // |prefs| must outlive this.
  explicit StandardManagementPolicyProvider(ExtensionPrefs* prefs);

  virtual ~StandardManagementPolicyProvider();

  // ManagementPolicy::Provider implementation.
  virtual std::string GetDebugPolicyProviderName() const OVERRIDE;
  virtual bool UserMayLoad(const Extension* extension,
                           string16* error) const OVERRIDE;
  virtual bool UserMayModifySettings(const Extension* extension,
                                     string16* error) const OVERRIDE;
  virtual bool MustRemainEnabled(const Extension* extension,
                                 string16* error) const OVERRIDE;

 private:
  ExtensionPrefs* const prefs_;
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_STANDARD_MANAGEMENT_POLICY_PROVIDER_H_
