// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/linked_ptr.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_constants.h"
#include "chrome/common/extensions/extension_icon_set.h"
#include "chrome/common/extensions/manifest_handlers/icons_handler.h"
#include "chrome/common/extensions/manifest_tests/extension_manifest_test.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace extensions {

class IconsManifestTest : public ExtensionManifestTest {
};

TEST_F(IconsManifestTest, NormalizeIconPaths) {
  scoped_refptr<extensions::Extension> extension(
      LoadAndExpectSuccess("normalize_icon_paths.json"));
  const ExtensionIconSet& icons = IconsInfo::GetIcons(extension.get());

  EXPECT_EQ("16.png", icons.Get(extension_misc::EXTENSION_ICON_BITTY,
                                ExtensionIconSet::MATCH_EXACTLY));
  EXPECT_EQ("48.png", icons.Get(extension_misc::EXTENSION_ICON_MEDIUM,
                                ExtensionIconSet::MATCH_EXACTLY));
}

TEST_F(IconsManifestTest, InvalidIconSizes) {
  scoped_refptr<extensions::Extension> extension(
      LoadAndExpectSuccess("init_ignored_icon_size.json"));
  EXPECT_EQ("",
            IconsInfo::GetIcons(extension.get())
                .Get(300, ExtensionIconSet::MATCH_EXACTLY));
}

TEST_F(IconsManifestTest, ValidIconSizes) {
  scoped_refptr<extensions::Extension> extension(
      LoadAndExpectSuccess("init_valid_icon_size.json"));
  const ExtensionIconSet& icons = IconsInfo::GetIcons(extension.get());

  EXPECT_EQ("16.png", icons.Get(extension_misc::EXTENSION_ICON_BITTY,
                                ExtensionIconSet::MATCH_EXACTLY));
  EXPECT_EQ("24.png", icons.Get(extension_misc::EXTENSION_ICON_SMALLISH,
                                ExtensionIconSet::MATCH_EXACTLY));
  EXPECT_EQ("32.png", icons.Get(extension_misc::EXTENSION_ICON_SMALL,
                                ExtensionIconSet::MATCH_EXACTLY));
  EXPECT_EQ("48.png", icons.Get(extension_misc::EXTENSION_ICON_MEDIUM,
                                ExtensionIconSet::MATCH_EXACTLY));
  EXPECT_EQ("128.png", icons.Get(extension_misc::EXTENSION_ICON_LARGE,
                                 ExtensionIconSet::MATCH_EXACTLY));
  EXPECT_EQ("256.png", icons.Get(extension_misc::EXTENSION_ICON_EXTRA_LARGE,
                                 ExtensionIconSet::MATCH_EXACTLY));
  EXPECT_EQ("512.png", icons.Get(extension_misc::EXTENSION_ICON_GIGANTOR,
                                 ExtensionIconSet::MATCH_EXACTLY));
}

}  // namespace extensions
