// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/declarative/initializing_rules_registry.h"

#include <algorithm>

#include "base/message_loop/message_loop.h"
#include "chrome/browser/extensions/api/declarative/test_rules_registry.h"
#include "content/public/test/test_browser_thread.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {
const char kExtensionId[] = "foobar";
const char kRuleId[] = "foo";
}  // namespace

namespace extensions {

TEST(InitializingRulesRegistryTest, FillOptionalIdentifiers) {
  base::MessageLoopForUI message_loop;
  content::TestBrowserThread thread(content::BrowserThread::UI, &message_loop);

  std::string error;
  scoped_refptr<RulesRegistry> registry = new InitializingRulesRegistry(
      new TestRulesRegistry(content::BrowserThread::UI, "" /*event_name*/));
  InitializingRulesRegistry* init_registry =
      static_cast<InitializingRulesRegistry*>(registry.get());

  // Add rules and check that their identifiers are filled and unique.

  std::vector<linked_ptr<RulesRegistry::Rule> > add_rules;
  add_rules.push_back(make_linked_ptr(new RulesRegistry::Rule));
  add_rules.push_back(make_linked_ptr(new RulesRegistry::Rule));
  error = registry->AddRules(kExtensionId, add_rules);
  EXPECT_TRUE(error.empty());

  std::vector<linked_ptr<RulesRegistry::Rule> > get_rules;
  error = registry->GetAllRules(kExtensionId, &get_rules);
  EXPECT_TRUE(error.empty());

  ASSERT_EQ(2u, get_rules.size());

  ASSERT_TRUE(get_rules[0]->id.get());
  EXPECT_NE("", *get_rules[0]->id);

  ASSERT_TRUE(get_rules[1]->id.get());
  EXPECT_NE("", *get_rules[1]->id);

  EXPECT_NE(*get_rules[0]->id, *get_rules[1]->id);

  EXPECT_EQ(1u /*extensions*/ + 2u /*rules*/,
            init_registry->GetNumberOfUsedRuleIdentifiersForTesting());

  // Check that we cannot add a new rule with the same ID.

  std::vector<linked_ptr<RulesRegistry::Rule> > add_rules_2;
  add_rules_2.push_back(make_linked_ptr(new RulesRegistry::Rule));
  add_rules_2[0]->id.reset(new std::string(*get_rules[0]->id));
  error = registry->AddRules(kExtensionId, add_rules_2);
  EXPECT_FALSE(error.empty());

  std::vector<linked_ptr<RulesRegistry::Rule> > get_rules_2;
  error = registry->GetAllRules(kExtensionId, &get_rules_2);
  EXPECT_TRUE(error.empty());
  ASSERT_EQ(2u, get_rules_2.size());
  EXPECT_EQ(1u /*extensions*/ + 2u /*rules*/,
            init_registry->GetNumberOfUsedRuleIdentifiersForTesting());

  // Check that we can register the old rule IDs once they were unregistered.

  std::vector<std::string> remove_rules_3;
  remove_rules_3.push_back(*get_rules[0]->id);
  error = registry->RemoveRules(kExtensionId, remove_rules_3);
  EXPECT_TRUE(error.empty());

  EXPECT_EQ(1u /*extensions*/ + 1u /*rules*/,
            init_registry->GetNumberOfUsedRuleIdentifiersForTesting());

  std::vector<linked_ptr<RulesRegistry::Rule> > get_rules_3a;
  error = registry->GetAllRules(kExtensionId, &get_rules_3a);
  EXPECT_TRUE(error.empty());
  ASSERT_EQ(1u, get_rules_3a.size());

  std::vector<linked_ptr<RulesRegistry::Rule> > add_rules_3;
  add_rules_3.push_back(make_linked_ptr(new RulesRegistry::Rule));
  add_rules_3[0]->id.reset(new std::string(*get_rules[0]->id));
  error = registry->AddRules(kExtensionId, add_rules_3);
  EXPECT_TRUE(error.empty());
  EXPECT_EQ(1u /*extensions*/ + 2u /*rules*/,
            init_registry->GetNumberOfUsedRuleIdentifiersForTesting());

  std::vector<linked_ptr<RulesRegistry::Rule> > get_rules_3b;
  error = registry->GetAllRules(kExtensionId, &get_rules_3b);
  EXPECT_TRUE(error.empty());
  ASSERT_EQ(2u, get_rules_3b.size());

  // Check that we can register a rule with an ID that is not modified.

  error = registry->RemoveAllRules(kExtensionId);
  EXPECT_TRUE(error.empty());
  EXPECT_EQ(0u /*extensions*/ + 0u /*rules*/,
            init_registry->GetNumberOfUsedRuleIdentifiersForTesting());

  std::vector<linked_ptr<RulesRegistry::Rule> > get_rules_4a;
  error = registry->GetAllRules(kExtensionId, &get_rules_4a);
  EXPECT_TRUE(error.empty());
  ASSERT_TRUE(get_rules_4a.empty());

  std::vector<linked_ptr<RulesRegistry::Rule> > add_rules_4;
  add_rules_4.push_back(make_linked_ptr(new RulesRegistry::Rule));
  add_rules_4[0]->id.reset(new std::string(kRuleId));
  error = registry->AddRules(kExtensionId, add_rules_4);
  EXPECT_TRUE(error.empty());

  EXPECT_EQ(1u /*extensions*/ + 1u /*rules*/,
            init_registry->GetNumberOfUsedRuleIdentifiersForTesting());

  std::vector<linked_ptr<RulesRegistry::Rule> > get_rules_4b;
  error = registry->GetAllRules(kExtensionId, &get_rules_4b);
  EXPECT_TRUE(error.empty());

  ASSERT_EQ(1u, get_rules_4b.size());

  ASSERT_TRUE(get_rules_4b[0]->id.get());
  EXPECT_EQ(kRuleId, *get_rules_4b[0]->id);

  registry->OnExtensionUnloaded(kExtensionId);
  EXPECT_EQ(0u /*extensions*/ + 0u /*rules*/,
            init_registry->GetNumberOfUsedRuleIdentifiersForTesting());

  // Make sure that deletion traits of registry are executed.
  registry = NULL;
  message_loop.RunUntilIdle();
}

TEST(InitializingRulesRegistryTest, FillOptionalPriority) {
  base::MessageLoopForUI message_loop;
  content::TestBrowserThread thread(content::BrowserThread::UI, &message_loop);

  std::string error;
  scoped_refptr<RulesRegistry> registry = new InitializingRulesRegistry(
      new TestRulesRegistry(content::BrowserThread::UI, "" /*event_name*/));

  // Add rules and check that their priorities are filled if they are empty.

  std::vector<linked_ptr<RulesRegistry::Rule> > add_rules;
  add_rules.push_back(make_linked_ptr(new RulesRegistry::Rule));
  add_rules[0]->priority.reset(new int(2));
  add_rules.push_back(make_linked_ptr(new RulesRegistry::Rule));
  error = registry->AddRules(kExtensionId, add_rules);
  EXPECT_TRUE(error.empty());

  std::vector<linked_ptr<RulesRegistry::Rule> > get_rules;
  error = registry->GetAllRules(kExtensionId, &get_rules);
  EXPECT_TRUE(error.empty());

  ASSERT_EQ(2u, get_rules.size());

  ASSERT_TRUE(get_rules[0]->priority.get());
  ASSERT_TRUE(get_rules[1]->priority.get());

  // Verify the precondition so that the following EXPECT_EQ statements work.
  EXPECT_GT(InitializingRulesRegistry::DEFAULT_PRIORITY, 2);
  EXPECT_EQ(2, std::min(*get_rules[0]->priority, *get_rules[1]->priority));
  EXPECT_EQ(InitializingRulesRegistry::DEFAULT_PRIORITY,
            std::max(*get_rules[0]->priority, *get_rules[1]->priority));

  // Make sure that deletion traits of registry are executed.
  registry = NULL;
  message_loop.RunUntilIdle();
}

}  // namespace extensions
