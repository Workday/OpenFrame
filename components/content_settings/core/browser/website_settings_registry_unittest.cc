// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/logging.h"
#include "base/prefs/pref_registry.h"
#include "base/values.h"
#include "components/content_settings/core/browser/website_settings_info.h"
#include "components/content_settings/core/browser/website_settings_registry.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace content_settings {

class WebsiteSettingsRegistryTest : public testing::Test {
 protected:
  WebsiteSettingsRegistry* registry() { return &registry_; }

 private:
  WebsiteSettingsRegistry registry_;
};

TEST_F(WebsiteSettingsRegistryTest, Get) {
  // CONTENT_SETTINGS_TYPE_APP_BANNER should be registered.
  const WebsiteSettingsInfo* info =
      registry()->Get(CONTENT_SETTINGS_TYPE_APP_BANNER);
  ASSERT_TRUE(info);
  EXPECT_EQ(CONTENT_SETTINGS_TYPE_APP_BANNER, info->type());
  EXPECT_EQ("app-banner", info->name());
}

TEST_F(WebsiteSettingsRegistryTest, GetByName) {
  // Random string shouldn't be registered.
  EXPECT_FALSE(registry()->GetByName("abc"));

  // "app-banner" should be registered.
  const WebsiteSettingsInfo* info = registry()->GetByName("app-banner");
  ASSERT_TRUE(info);
  EXPECT_EQ(CONTENT_SETTINGS_TYPE_APP_BANNER, info->type());
  EXPECT_EQ("app-banner", info->name());
  EXPECT_EQ(registry()->Get(CONTENT_SETTINGS_TYPE_APP_BANNER), info);

  // Register a new setting.
  registry()->Register(static_cast<ContentSettingsType>(10), "test", nullptr,
                       WebsiteSettingsInfo::UNSYNCABLE,
                       WebsiteSettingsInfo::LOSSY,
                       WebsiteSettingsInfo::TOP_LEVEL_DOMAIN_ONLY_SCOPE);
  info = registry()->GetByName("test");
  ASSERT_TRUE(info);
  EXPECT_EQ(10, info->type());
  EXPECT_EQ("test", info->name());
  EXPECT_EQ(registry()->Get(static_cast<ContentSettingsType>(10)), info);
}

TEST_F(WebsiteSettingsRegistryTest, Properties) {
  // "app-banner" should be registered.
  const WebsiteSettingsInfo* info =
      registry()->Get(CONTENT_SETTINGS_TYPE_APP_BANNER);
  ASSERT_TRUE(info);
  EXPECT_EQ("profile.content_settings.exceptions.app_banner",
            info->pref_name());
  EXPECT_EQ("profile.default_content_setting_values.app_banner",
            info->default_value_pref_name());
  ASSERT_FALSE(info->initial_default_value());
  EXPECT_EQ(PrefRegistry::LOSSY_PREF, info->GetPrefRegistrationFlags());

  // Register a new setting.
  registry()->Register(static_cast<ContentSettingsType>(10), "test",
                       make_scoped_ptr(new base::FundamentalValue(999)),
                       WebsiteSettingsInfo::SYNCABLE,
                       WebsiteSettingsInfo::LOSSY,
                       WebsiteSettingsInfo::TOP_LEVEL_DOMAIN_ONLY_SCOPE);
  info = registry()->Get(static_cast<ContentSettingsType>(10));
  ASSERT_TRUE(info);
  EXPECT_EQ("profile.content_settings.exceptions.test", info->pref_name());
  EXPECT_EQ("profile.default_content_setting_values.test",
            info->default_value_pref_name());
  int setting;
  ASSERT_TRUE(info->initial_default_value()->GetAsInteger(&setting));
  EXPECT_EQ(999, setting);
  EXPECT_EQ(PrefRegistry::LOSSY_PREF |
                user_prefs::PrefRegistrySyncable::SYNCABLE_PREF,
            info->GetPrefRegistrationFlags());
  EXPECT_EQ(WebsiteSettingsInfo::TOP_LEVEL_DOMAIN_ONLY_SCOPE,
            info->scoping_type());
}

TEST_F(WebsiteSettingsRegistryTest, Iteration) {
  registry()->Register(static_cast<ContentSettingsType>(10), "test",
                       make_scoped_ptr(new base::FundamentalValue(999)),
                       WebsiteSettingsInfo::SYNCABLE,
                       WebsiteSettingsInfo::LOSSY,
                       WebsiteSettingsInfo::TOP_LEVEL_DOMAIN_ONLY_SCOPE);

  bool found = false;
  for (const WebsiteSettingsInfo* info : *registry()) {
    EXPECT_EQ(registry()->Get(info->type()), info);
    if (info->type() == 10) {
      EXPECT_FALSE(found);
      found = true;
    }
  }

  EXPECT_TRUE(found);
}

}  // namespace content_settings
