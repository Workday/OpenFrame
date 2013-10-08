// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/extensions/api/i18n/default_locale_handler.h"
#include "chrome/common/extensions/extension_manifest_constants.h"
#include "chrome/common/extensions/manifest_tests/extension_manifest_test.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace extensions {

class DefaultLocaleManifestTest : public ExtensionManifestTest {
};

TEST_F(DefaultLocaleManifestTest, DefaultLocale) {
  LoadAndExpectError("default_locale_invalid.json",
                     extension_manifest_errors::kInvalidDefaultLocale);

  scoped_refptr<Extension> extension(
      LoadAndExpectSuccess("default_locale_valid.json"));
  EXPECT_EQ("de-AT", LocaleInfo::GetDefaultLocale(extension.get()));
}

}  // namespace extensions
