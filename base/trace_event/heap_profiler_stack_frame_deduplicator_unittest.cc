// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/ref_counted.h"
#include "base/trace_event/heap_profiler_allocation_context.h"
#include "base/trace_event/heap_profiler_stack_frame_deduplicator.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace trace_event {

// Define all strings once, because the deduplicator requires pointer equality,
// and string interning is unreliable.
const char kBrowserMain[] = "BrowserMain";
const char kRendererMain[] = "RendererMain";
const char kCreateWidget[] = "CreateWidget";
const char kInitialize[] = "Initialize";
const char kMalloc[] = "malloc";

class StackFrameDeduplicatorTest : public testing::Test {};

TEST_F(StackFrameDeduplicatorTest, SingleBacktrace) {
  Backtrace bt = {
      {kBrowserMain, kCreateWidget, kMalloc, 0, 0, 0, 0, 0, 0, 0, 0, 0}};

  // The call tree should look like this (index in brackets).
  //
  // BrowserMain [0]
  //   CreateWidget [1]
  //     malloc [2]

  scoped_refptr<StackFrameDeduplicator> dedup = new StackFrameDeduplicator;
  ASSERT_EQ(2, dedup->Insert(bt));

  auto iter = dedup->begin();
  ASSERT_EQ(kBrowserMain, (iter + 0)->frame);
  ASSERT_EQ(-1, (iter + 0)->parent_frame_index);

  ASSERT_EQ(kCreateWidget, (iter + 1)->frame);
  ASSERT_EQ(0, (iter + 1)->parent_frame_index);

  ASSERT_EQ(kMalloc, (iter + 2)->frame);
  ASSERT_EQ(1, (iter + 2)->parent_frame_index);

  ASSERT_EQ(iter + 3, dedup->end());
}

// Test that there can be different call trees (there can be multiple bottom
// frames). Also verify that frames with the same name but a different caller
// are represented as distinct nodes.
TEST_F(StackFrameDeduplicatorTest, MultipleRoots) {
  Backtrace bt0 = {{kBrowserMain, kCreateWidget, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
  Backtrace bt1 = {{kRendererMain, kCreateWidget, 0, 0, 0, 0, 0, 0, 0, 0, 0}};

  // The call tree should look like this (index in brackets).
  //
  // BrowserMain [0]
  //   CreateWidget [1]
  // RendererMain [2]
  //   CreateWidget [3]
  //
  // Note that there will be two instances of CreateWidget,
  // with different parents.

  scoped_refptr<StackFrameDeduplicator> dedup = new StackFrameDeduplicator;
  ASSERT_EQ(1, dedup->Insert(bt0));
  ASSERT_EQ(3, dedup->Insert(bt1));

  auto iter = dedup->begin();
  ASSERT_EQ(kBrowserMain, (iter + 0)->frame);
  ASSERT_EQ(-1, (iter + 0)->parent_frame_index);

  ASSERT_EQ(kCreateWidget, (iter + 1)->frame);
  ASSERT_EQ(0, (iter + 1)->parent_frame_index);

  ASSERT_EQ(kRendererMain, (iter + 2)->frame);
  ASSERT_EQ(-1, (iter + 2)->parent_frame_index);

  ASSERT_EQ(kCreateWidget, (iter + 3)->frame);
  ASSERT_EQ(2, (iter + 3)->parent_frame_index);

  ASSERT_EQ(iter + 4, dedup->end());
}

TEST_F(StackFrameDeduplicatorTest, Deduplication) {
  Backtrace bt0 = {{kBrowserMain, kCreateWidget, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
  Backtrace bt1 = {{kBrowserMain, kInitialize, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};

  // The call tree should look like this (index in brackets).
  //
  // BrowserMain [0]
  //   CreateWidget [1]
  //   Initialize [2]
  //
  // Note that BrowserMain will be re-used.

  scoped_refptr<StackFrameDeduplicator> dedup = new StackFrameDeduplicator;
  ASSERT_EQ(1, dedup->Insert(bt0));
  ASSERT_EQ(2, dedup->Insert(bt1));

  auto iter = dedup->begin();
  ASSERT_EQ(kBrowserMain, (iter + 0)->frame);
  ASSERT_EQ(-1, (iter + 0)->parent_frame_index);

  ASSERT_EQ(kCreateWidget, (iter + 1)->frame);
  ASSERT_EQ(0, (iter + 1)->parent_frame_index);

  ASSERT_EQ(kInitialize, (iter + 2)->frame);
  ASSERT_EQ(0, (iter + 2)->parent_frame_index);

  ASSERT_EQ(iter + 3, dedup->end());

  // Inserting the same backtrace again should return the index of the existing
  // node.
  ASSERT_EQ(1, dedup->Insert(bt0));
  ASSERT_EQ(2, dedup->Insert(bt1));
  ASSERT_EQ(dedup->begin() + 3, dedup->end());
}

}  // namespace trace_event
}  // namespace base
