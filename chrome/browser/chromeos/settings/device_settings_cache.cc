// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/settings/device_settings_cache.h"

#include <string>

#include "base/base64.h"
#include "base/bind.h"
#include "base/prefs/pref_registry_simple.h"
#include "base/prefs/pref_service.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/browser/policy/proto/cloud/device_management_backend.pb.h"
#include "chrome/common/pref_names.h"

namespace em = enterprise_management;

namespace chromeos {

namespace device_settings_cache {

void RegisterPrefs(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(prefs::kDeviceSettingsCache, "invalid");
}

bool Store(const em::PolicyData& policy, PrefService* local_state) {
  if (local_state) {
    std::string policy_string = policy.SerializeAsString();
    std::string encoded;
    if (!base::Base64Encode(policy_string, &encoded)) {
      LOG(ERROR) << "Can't encode policy in base64.";
      return false;
    }
    local_state->SetString(prefs::kDeviceSettingsCache, encoded);
    return true;
  }
  return false;
}

bool Retrieve(em::PolicyData *policy, PrefService* local_state) {
  if (local_state) {
    std::string encoded =
        local_state->GetString(prefs::kDeviceSettingsCache);
    std::string policy_string;
    if (!base::Base64Decode(encoded, &policy_string)) {
      // This is normal and happens on first boot.
      VLOG(1) << "Can't decode policy from base64.";
      return false;
    }
    return policy->ParseFromString(policy_string);
  }
  return false;
}

}  // namespace device_settings_cache

}  // namespace chromeos
