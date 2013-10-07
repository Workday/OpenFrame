// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_manifest_constants.h"
#include "chrome/common/extensions/manifest_handlers/offline_enabled_info.h"
#include "chrome/common/extensions/manifest_tests/extension_manifest_test.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace errors = extension_manifest_errors;

namespace extensions {

class ExtensionManifestOfflineEnabledTest : public ExtensionManifestTest {
};

TEST_F(ExtensionManifestOfflineEnabledTest, OfflineEnabled) {
  LoadAndExpectError("offline_enabled_invalid.json",
                     errors::kInvalidOfflineEnabled);
  scoped_refptr<Extension> extension_0(
      LoadAndExpectSuccess("offline_enabled_extension.json"));
  EXPECT_TRUE(OfflineEnabledInfo::IsOfflineEnabled(extension_0.get()));
  scoped_refptr<Extension> extension_1(
      LoadAndExpectSuccess("offline_enabled_packaged_app.json"));
  EXPECT_TRUE(OfflineEnabledInfo::IsOfflineEnabled(extension_1.get()));
  scoped_refptr<Extension> extension_2(
      LoadAndExpectSuccess("offline_disabled_packaged_app.json"));
  EXPECT_FALSE(OfflineEnabledInfo::IsOfflineEnabled(extension_2.get()));
  scoped_refptr<Extension> extension_3(
      LoadAndExpectSuccess("offline_default_packaged_app.json"));
  EXPECT_FALSE(OfflineEnabledInfo::IsOfflineEnabled(extension_3.get()));
  scoped_refptr<Extension> extension_4(
      LoadAndExpectSuccess("offline_enabled_hosted_app.json"));
  EXPECT_TRUE(OfflineEnabledInfo::IsOfflineEnabled(extension_4.get()));
  scoped_refptr<Extension> extension_5(
      LoadAndExpectSuccess("offline_default_platform_app.json"));
  EXPECT_TRUE(OfflineEnabledInfo::IsOfflineEnabled(extension_5.get()));
}

}  // namespace extensions
