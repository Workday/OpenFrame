// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_SIGNEDIN_DEVICES_SIGNEDIN_DEVICES_API_H__
#define CHROME_BROWSER_EXTENSIONS_API_SIGNEDIN_DEVICES_SIGNEDIN_DEVICES_API_H__

#include <string>
#include <vector>

#include "base/memory/scoped_vector.h"

namespace browser_sync {
class DeviceInfo;
}  // namespace browser_sync

namespace extensions {
class ExtensionPrefs;
}  // namespace extensions

class Profile;
class ProfileSyncService;

namespace extensions {

// Gets the list of signed in devices. The returned scoped vector will be
// filled with the list of devices associated with the account signed into this
// |profile|. This function needs the |extension_id| because the
// public device ids are set per extension.
ScopedVector<browser_sync::DeviceInfo> GetAllSignedInDevices(
    const std::string& extension_id,
    Profile* profile);

ScopedVector<browser_sync::DeviceInfo> GetAllSignedInDevices(
    const std::string& extension_id,
    ProfileSyncService* pss,
    ExtensionPrefs* extension_prefs);

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_SIGNEDIN_DEVICES_SIGNEDIN_DEVICES_API_H__
