// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_manifest_constants.h"
#include "chrome/common/extensions/manifest_handlers/kiosk_enabled_info.h"
#include "chrome/common/extensions/manifest_tests/extension_manifest_test.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace extensions {

class ExtensionManifestKioskEnabledTest : public ExtensionManifestTest {
};

TEST_F(ExtensionManifestKioskEnabledTest, InvalidKioskEnabled) {
  LoadAndExpectError("kiosk_enabled_invalid.json",
                     extension_manifest_errors::kInvalidKioskEnabled);
}

TEST_F(ExtensionManifestKioskEnabledTest, KioskEnabledHostedApp) {
  scoped_refptr<Extension> extension(
      LoadAndExpectSuccess("kiosk_enabled_hosted_app.json"));
  EXPECT_FALSE(KioskEnabledInfo::IsKioskEnabled(extension.get()));
}

TEST_F(ExtensionManifestKioskEnabledTest, KioskEnabledPackagedApp) {
  scoped_refptr<Extension> extension(
      LoadAndExpectSuccess("kiosk_enabled_packaged_app.json"));
  EXPECT_FALSE(KioskEnabledInfo::IsKioskEnabled(extension.get()));
}

TEST_F(ExtensionManifestKioskEnabledTest, KioskEnabledExtension) {
  scoped_refptr<Extension> extension(
      LoadAndExpectSuccess("kiosk_enabled_extension.json"));
  EXPECT_FALSE(KioskEnabledInfo::IsKioskEnabled(extension.get()));
}

TEST_F(ExtensionManifestKioskEnabledTest, KioskEnabledPlatformApp) {
  scoped_refptr<Extension> extension(
      LoadAndExpectSuccess("kiosk_enabled_platform_app.json"));
  EXPECT_TRUE(KioskEnabledInfo::IsKioskEnabled(extension.get()));
}

TEST_F(ExtensionManifestKioskEnabledTest, KioskDisabledPlatformApp) {
  scoped_refptr<Extension> extension(
      LoadAndExpectSuccess("kiosk_disabled_platform_app.json"));
  EXPECT_FALSE(KioskEnabledInfo::IsKioskEnabled(extension.get()));
}

TEST_F(ExtensionManifestKioskEnabledTest, KioskDefaultPlatformApp) {
  scoped_refptr<Extension> extension(
      LoadAndExpectSuccess("kiosk_default_platform_app.json"));
  EXPECT_FALSE(KioskEnabledInfo::IsKioskEnabled(extension.get()));
}

}  // namespace extensions
