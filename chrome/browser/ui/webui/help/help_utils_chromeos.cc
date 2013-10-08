// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/help/help_utils_chromeos.h"

#include <algorithm>

#include "base/logging.h"
#include "base/values.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/browser/chromeos/settings/cros_settings_names.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "third_party/cros_system_api/dbus/service_constants.h"
#include "ui/base/l10n/l10n_util.h"

namespace {

const bool kDefaultUpdateOverCellularAllowed = false;

}  // namespace

namespace help_utils_chromeos {

bool IsUpdateOverCellularAllowed() {
  chromeos::CrosSettings* settings = chromeos::CrosSettings::Get();
  if (!settings)
    return kDefaultUpdateOverCellularAllowed;

  const base::Value* raw_types_value =
      settings->GetPref(chromeos::kAllowedConnectionTypesForUpdate);
  if (!raw_types_value)
    return kDefaultUpdateOverCellularAllowed;
  const base::ListValue* types_value;
  CHECK(raw_types_value->GetAsList(&types_value));
  for (size_t i = 0; i < types_value->GetSize(); ++i) {
    int connection_type;
    if (!types_value->GetInteger(i, &connection_type)) {
      LOG(WARNING) << "Can't parse connection type #" << i;
      continue;
    }
    if (connection_type == 4)
      return true;
  }
  return kDefaultUpdateOverCellularAllowed;
}

string16 GetConnectionTypeAsUTF16(const std::string& type) {
  if (type == flimflam::kTypeEthernet)
    return l10n_util::GetStringUTF16(IDS_NETWORK_TYPE_ETHERNET);
  if (type == flimflam::kTypeWifi)
    return l10n_util::GetStringUTF16(IDS_NETWORK_TYPE_WIFI);
  if (type == flimflam::kTypeWimax)
    return l10n_util::GetStringUTF16(IDS_NETWORK_TYPE_WIMAX);
  if (type == flimflam::kTypeBluetooth)
    return l10n_util::GetStringUTF16(IDS_NETWORK_TYPE_BLUETOOTH);
  if (type == flimflam::kTypeCellular)
    return l10n_util::GetStringUTF16(IDS_NETWORK_TYPE_CELLULAR);
  if (type == flimflam::kTypeVPN)
    return l10n_util::GetStringUTF16(IDS_NETWORK_TYPE_VPN);
  NOTREACHED();
  return string16();
}

}  // namespace help_utils_chromeos
