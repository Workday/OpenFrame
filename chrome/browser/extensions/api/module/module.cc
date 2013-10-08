// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/module/module.h"

#include <string>

#include "base/values.h"
#include "chrome/browser/extensions/extension_prefs.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/profiles/profile.h"

namespace extensions {

namespace extension {

namespace {

// A preference for storing the extension's update URL data. If not empty, the
// the ExtensionUpdater will append a ap= parameter to the URL when checking if
// a new version of the extension is available.
const char kUpdateURLData[] = "update_url_data";

}  // namespace

std::string GetUpdateURLData(const ExtensionPrefs* prefs,
                             const std::string& extension_id) {
  std::string data;
  prefs->ReadPrefAsString(extension_id, kUpdateURLData, &data);
  return data;
}

}  // namespace extension

bool ExtensionSetUpdateUrlDataFunction::RunImpl() {
  std::string data;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(0, &data));

  ExtensionPrefs::Get(profile())->UpdateExtensionPref(
      extension_id(),
      extension::kUpdateURLData,
      base::Value::CreateStringValue(data));
  return true;
}

bool ExtensionIsAllowedIncognitoAccessFunction::RunImpl() {
  ExtensionService* ext_service =
      ExtensionSystem::Get(profile())->extension_service();
  const Extension* extension = GetExtension();

  SetResult(Value::CreateBooleanValue(
      ext_service->IsIncognitoEnabled(extension->id())));
  return true;
}

bool ExtensionIsAllowedFileSchemeAccessFunction::RunImpl() {
  ExtensionService* ext_service =
      ExtensionSystem::Get(profile())->extension_service();
  const Extension* extension = GetExtension();

  SetResult(Value::CreateBooleanValue(
      ext_service->AllowFileAccess(extension)));
  return true;
}

}  // namespace extensions
