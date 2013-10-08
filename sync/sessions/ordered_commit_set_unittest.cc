// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sync/sessions/ordered_commit_set.h"
#include "sync/test/engine/test_id_factory.h"
#include "testing/gtest/include/gtest/gtest.h"

using std::vector;

namespace syncer {
namespace sessions {
namespace {

class OrderedCommitSetTest : public testing::Test {
 public:
  OrderedCommitSetTest() {
    routes_[BOOKMARKS] = GROUP_UI;
    routes_[PREFERENCES] = GROUP_UI;
    routes_[AUTOFILL] = GROUP_DB;
    routes_[SESSIONS] = GROUP_PASSIVE;
  }
 protected:
  TestIdFactory ids_;
  ModelSafeRoutingInfo routes_;
};

TEST_F(OrderedCommitSetTest, Projections) {
  vector<syncable::Id> expected;
  for (int i = 0; i < 8; i++)
    expected.push_back(ids_.NewLocalId());

  OrderedCommitSet commit_set1(routes_), commit_set2(routes_);
  commit_set1.AddCommitItem(0, expected[0], BOOKMARKS);
  commit_set1.AddCommitItem(1, expected[1], BOOKMARKS);
  commit_set1.AddCommitItem(2, expected[2], PREFERENCES);
  // Duplicates should be dropped.
  commit_set1.AddCommitItem(2, expected[2], PREFERENCES);
  commit_set1.AddCommitItem(3, expected[3], SESSIONS);
  commit_set1.AddCommitItem(4, expected[4], SESSIONS);
  commit_set2.AddCommitItem(7, expected[7], AUTOFILL);
  commit_set2.AddCommitItem(6, expected[6], AUTOFILL);
  commit_set2.AddCommitItem(5, expected[5], AUTOFILL);
  // Add something in set1 to set2, which should get dropped by AppendReverse.
  commit_set2.AddCommitItem(0, expected[0], BOOKMARKS);
  commit_set1.AppendReverse(commit_set2);

  // First, we should verify the projections are correct. Second, we want to
  // do the same verification after truncating by 1. Next, try truncating
  // the set to a size of 4, so that the DB projection is wiped out and
  // PASSIVE has one element removed.  Finally, truncate to 1 so only UI is
  // remaining.
  int j = 0;
  do {
    SCOPED_TRACE(::testing::Message("Iteration j = ") << j);
    vector<syncable::Id> all_ids = commit_set1.GetAllCommitIds();
    EXPECT_EQ(expected.size(), all_ids.size());
    for (size_t i = 0; i < expected.size(); i++) {
      SCOPED_TRACE(::testing::Message("CommitSet mismatch at iteration i = ")
                   << i);
      EXPECT_TRUE(expected[i] == all_ids[i]);
      EXPECT_TRUE(expected[i] == commit_set1.GetCommitIdAt(i));
    }

    OrderedCommitSet::Projection p1, p2, p3;
    p1 = commit_set1.GetCommitIdProjection(GROUP_UI);
    p2 = commit_set1.GetCommitIdProjection(GROUP_PASSIVE);
    p3 = commit_set1.GetCommitIdProjection(GROUP_DB);
    EXPECT_TRUE(p1.size() + p2.size() + p3.size() == expected.size()) << "Sum"
        << "of sizes of projections should equal full expected size!";

    for (size_t i = 0; i < p1.size(); i++) {
      SCOPED_TRACE(::testing::Message("UI projection mismatch at i = ") << i);
      EXPECT_TRUE(expected[p1[i]] == commit_set1.GetCommitIdAt(p1[i]))
          << "expected[p1[i]] = " << expected[p1[i]]
          << ", commit_set1[p1[i]] = " << commit_set1.GetCommitIdAt(p1[i]);
    }
    for (size_t i = 0; i < p2.size(); i++) {
      SCOPED_TRACE(::testing::Message("PASSIVE projection mismatch at i = ")
                   << i);
      EXPECT_TRUE(expected[p2[i]] == commit_set1.GetCommitIdAt(p2[i]))
          << "expected[p2[i]] = " << expected[p2[i]]
          << ", commit_set1[p2[i]] = " << commit_set1.GetCommitIdAt(p2[i]);
    }
    for (size_t i = 0; i < p3.size(); i++) {
      SCOPED_TRACE(::testing::Message("DB projection mismatch at i = ") << i);
      EXPECT_TRUE(expected[p3[i]] == commit_set1.GetCommitIdAt(p3[i]))
          << "expected[p3[i]] = " << expected[p3[i]]
          << ", commit_set1[p3[i]] = " << commit_set1.GetCommitIdAt(p3[i]);
    }

    int cut_to_size = 7 - 3 * j++;
    if (cut_to_size < 0)
      break;

    expected.resize(cut_to_size);
    commit_set1.Truncate(cut_to_size);
  } while (true);
}

TEST_F(OrderedCommitSetTest, HasBookmarkCommitId) {
  OrderedCommitSet commit_set(routes_);

  commit_set.AddCommitItem(0, ids_.NewLocalId(), AUTOFILL);
  commit_set.AddCommitItem(1, ids_.NewLocalId(), SESSIONS);
  EXPECT_FALSE(commit_set.HasBookmarkCommitId());

  commit_set.AddCommitItem(2, ids_.NewLocalId(), PREFERENCES);
  commit_set.AddCommitItem(3, ids_.NewLocalId(), PREFERENCES);
  EXPECT_FALSE(commit_set.HasBookmarkCommitId());

  commit_set.AddCommitItem(4, ids_.NewLocalId(), BOOKMARKS);
  EXPECT_TRUE(commit_set.HasBookmarkCommitId());

  commit_set.Truncate(4);
  EXPECT_FALSE(commit_set.HasBookmarkCommitId());
}

TEST_F(OrderedCommitSetTest, AddAndRemoveEntries) {
  OrderedCommitSet commit_set(routes_);

  ASSERT_TRUE(commit_set.Empty());

  commit_set.AddCommitItem(0, ids_.NewLocalId(), AUTOFILL);
  ASSERT_EQ(static_cast<size_t>(1), commit_set.Size());

  commit_set.Clear();
  ASSERT_TRUE(commit_set.Empty());
}

}  // namespace
}  // namespace sessions
}  // namespace syncer
