// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/prefs/pref_value_map.h"
#include "base/values.h"
#include "chrome/browser/search/contextual_search_policy_handler_android.h"
#include "chrome/common/pref_names.h"
#include "components/policy/core/common/policy_map.h"
#include "policy/policy_constants.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace policy {

TEST(ContextualSearchPolicyHandlerAndroidTest, Default) {
  PolicyMap policy;
  PrefValueMap prefs;
  ContextualSearchPolicyHandlerAndroid handler;
  handler.ApplyPolicySettings(policy, &prefs);
  std::string pref_value;
  EXPECT_FALSE(prefs.GetString(prefs::kContextualSearchEnabled, &pref_value));
  EXPECT_EQ("", pref_value);
}

TEST(ContextualSearchPolicyHandlerAndroidTest, Enabled) {
  PolicyMap policy;
  policy.Set(key::kContextualSearchEnabled,
             POLICY_LEVEL_MANDATORY,
             POLICY_SCOPE_USER,
             POLICY_SOURCE_PLATFORM,
             new base::FundamentalValue(true),
             NULL);
  PrefValueMap prefs;
  ContextualSearchPolicyHandlerAndroid handler;
  handler.ApplyPolicySettings(policy, &prefs);

  // Enabling Contextual Search policy should not set the pref.
  std::string pref_value;
  EXPECT_FALSE(prefs.GetString(prefs::kContextualSearchEnabled, &pref_value));
  EXPECT_EQ("", pref_value);
}

TEST(ContextualSearchPolicyHandlerAndroidTest, Disabled) {
  PolicyMap policy;
  policy.Set(key::kContextualSearchEnabled,
             POLICY_LEVEL_MANDATORY,
             POLICY_SCOPE_USER,
             POLICY_SOURCE_PLATFORM,
             new base::FundamentalValue(false),
             NULL);
  PrefValueMap prefs;
  ContextualSearchPolicyHandlerAndroid handler;
  handler.ApplyPolicySettings(policy, &prefs);

  // Disabling Contextual Search should switch the pref to managed.
  std::string pref_value;
  EXPECT_TRUE(prefs.GetString(prefs::kContextualSearchEnabled, &pref_value));
  EXPECT_EQ("false", pref_value);
}

}  // namespace policy
