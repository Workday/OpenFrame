// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/extensions/manifest_handlers/minimum_chrome_version_checker.h"

#include "base/strings/utf_string_conversions.h"
#include "base/version.h"
#include "chrome/common/chrome_version_info.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_manifest_constants.h"
#include "extensions/common/error_utils.h"
#include "grit/chromium_strings.h"
#include "ui/base/l10n/l10n_util.h"

namespace keys = extension_manifest_keys;
namespace errors = extension_manifest_errors;

namespace extensions {

MinimumChromeVersionChecker::MinimumChromeVersionChecker() {
}

MinimumChromeVersionChecker::~MinimumChromeVersionChecker() {
}

bool MinimumChromeVersionChecker::Parse(Extension* extension, string16* error) {
  std::string minimum_version_string;
  if (!extension->manifest()->GetString(keys::kMinimumChromeVersion,
                                        &minimum_version_string)) {
    *error = ASCIIToUTF16(errors::kInvalidMinimumChromeVersion);
    return false;
  }

  Version minimum_version(minimum_version_string);
  if (!minimum_version.IsValid()) {
    *error = ASCIIToUTF16(errors::kInvalidMinimumChromeVersion);
    return false;
  }

  chrome::VersionInfo current_version_info;
  Version current_version(current_version_info.Version());
  if (!current_version.IsValid()) {
    NOTREACHED();
    return false;
  }

  if (current_version.CompareTo(minimum_version) < 0) {
    *error = ErrorUtils::FormatErrorMessageUTF16(
        errors::kChromeVersionTooLow,
        l10n_util::GetStringUTF8(IDS_PRODUCT_NAME),
        minimum_version_string);
    return false;
  }
  return true;
}

const std::vector<std::string> MinimumChromeVersionChecker::Keys() const {
  return SingleKey(keys::kMinimumChromeVersion);
}

}  // namespace extensions
