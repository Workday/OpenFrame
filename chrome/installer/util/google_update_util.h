// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_INSTALLER_UTIL_GOOGLE_UPDATE_UTIL_H_
#define CHROME_INSTALLER_UTIL_GOOGLE_UPDATE_UTIL_H_

#include <string>

namespace google_update {

// If user-level Google Update is absent, calls the system-level
// GoogleUpdateSetup.exe to install it, and waits until it finishes.
// Returns true if already installed, installed successfully, or
// if Google Update is not present at system-level.
// Returns false if the installation fails.
bool EnsureUserLevelGoogleUpdatePresent();

// Tell Google Update that an uninstall has taken place.  This gives it a chance
// to uninstall itself straight away if no more products are installed on the
// system rather than waiting for the next time the scheduled task runs.
// Returns false if Google Update could not be executed, or times out.
bool UninstallGoogleUpdate(bool system_install);

// Returns the value corresponding to |key| in untrusted data passed from
// Google Update.  Returns an empty string if |key| is absent or if its value
// contains non-printable characters.
std::string GetUntrustedDataValue(const std::string& key);

}  // namespace google_update

#endif  // CHROME_INSTALLER_UTIL_GOOGLE_UPDATE_UTIL_H_
