// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/signedin_devices/id_mapping_helper.h"

#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"

#include "chrome/browser/sync/glue/device_info.h"

using base::DictionaryValue;
using base::Value;
using browser_sync::DeviceInfo;

namespace extensions {

std::string GetPublicIdFromGUID(
    const base::DictionaryValue& id_mapping,
    const std::string& guid) {
  for (DictionaryValue::Iterator it(id_mapping);
       !it.IsAtEnd();
       it.Advance()) {
    const Value& value = it.value();
    std::string guid_in_value;
    if (!value.GetAsString(&guid_in_value)) {
      LOG(ERROR) << "Badly formatted dictionary";
      continue;
    }
    if (guid_in_value == guid) {
      return it.key();
    }
  }

  return std::string();
}

std::string GetGUIDFromPublicId(
    const base::DictionaryValue& id_mapping,
    const std::string& id) {
  std::string guid;
  id_mapping.GetString(id, &guid);
  return guid;
}

// Finds out a random unused id. First finds a random id.
// If the id is in use, increments the id until it finds an unused id.
std::string GetRandomId(
  const DictionaryValue& mapping,
  int device_count) {
  // Set the max value for rand to be twice the device count.
  int max = device_count * 2;
  int rand_value = base::RandInt(0, max);
  std::string string_value;
  const Value *out_value;

  do {
    string_value = base::IntToString(rand_value);
    rand_value++;
  } while (mapping.Get(string_value, &out_value));

  return string_value;
}

void CreateMappingForUnmappedDevices(
    std::vector<browser_sync::DeviceInfo*>* device_info,
    base::DictionaryValue* value) {
  for (unsigned int i = 0; i < device_info->size(); ++i) {
    DeviceInfo* device = (*device_info)[i];
    std::string local_id = GetPublicIdFromGUID(*value,
                                               device->guid());

    // If the device does not have a local id, set one.
    if (local_id.empty()) {
      local_id = GetRandomId(*value, device_info->size());
      value->SetString(local_id, device->guid());
    }
    device->set_public_id(local_id);
  }
}

}  // namespace  extensions
