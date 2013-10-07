// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/disk_cache/flash/flash_cache_test_base.h"
#include "net/disk_cache/flash/format.h"
#include "net/disk_cache/flash/log_store.h"
#include "net/disk_cache/flash/segment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace disk_cache {

TEST_F(FlashCacheTest, LogStoreCreateEntry) {
  LogStore log_store(path_, kStorageSize);
  EXPECT_TRUE(log_store.Init());

  const int32 kSize = 100;
  const std::string buf(kSize, 0);

  int32 id;
  EXPECT_TRUE(log_store.CreateEntry(kSize, &id));
  EXPECT_TRUE(log_store.WriteData(buf.data(), kSize/2));
  EXPECT_TRUE(log_store.WriteData(buf.data(), kSize/2));
  log_store.CloseEntry(id);

  EXPECT_TRUE(log_store.Close());
}

// Also tests reading from current segment.
TEST_F(FlashCacheTest, LogStoreOpenEntry) {
  LogStore log_store(path_, kStorageSize);
  EXPECT_TRUE(log_store.Init());

  const int32 kSize = 100;
  const std::vector<char> expected(kSize, 'b');

  int32 id;
  EXPECT_TRUE(log_store.CreateEntry(kSize, &id));
  EXPECT_TRUE(log_store.WriteData(&expected[0], kSize));
  log_store.CloseEntry(id);

  EXPECT_TRUE(log_store.OpenEntry(id));
  std::vector<char> actual(kSize, 0);
  EXPECT_TRUE(log_store.ReadData(id, &actual[0], kSize, 0));
  log_store.CloseEntry(id);

  EXPECT_EQ(expected, actual);
  EXPECT_TRUE(log_store.Close());
}

// Also tests that writing advances segments.
TEST_F(FlashCacheTest, LogStoreReadFromClosedSegment) {
  LogStore log_store(path_, kStorageSize);
  EXPECT_TRUE(log_store.Init());

  const int32 kSize = disk_cache::kFlashSegmentFreeSpace;
  const std::vector<char> expected(kSize, 'a');

  // First two entries go to segment 0.
  int32 id1;
  EXPECT_EQ(0, log_store.write_index_);
  EXPECT_TRUE(log_store.CreateEntry(kSize/2, &id1));
  EXPECT_TRUE(log_store.WriteData(&expected[0], kSize/2));
  log_store.CloseEntry(id1);

  int32 id2;
  EXPECT_EQ(0, log_store.write_index_);
  EXPECT_TRUE(log_store.CreateEntry(kSize/2, &id2));
  EXPECT_TRUE(log_store.WriteData(&expected[0], kSize/2));
  log_store.CloseEntry(id2);

  // This entry goes to segment 1.
  int32 id3;
  EXPECT_TRUE(log_store.CreateEntry(kSize, &id3));
  EXPECT_EQ(1, log_store.write_index_);
  EXPECT_TRUE(log_store.WriteData(&expected[0], kSize));
  log_store.CloseEntry(id3);

  // We read from segment 0.
  EXPECT_TRUE(log_store.OpenEntry(id1));
  std::vector<char> actual(kSize, 0);
  EXPECT_TRUE(log_store.ReadData(id1, &actual[0], kSize, id1));
  log_store.CloseEntry(id1);

  EXPECT_EQ(expected, actual);
  EXPECT_TRUE(log_store.Close());
}

TEST_F(FlashCacheTest, LogStoreReadFromCurrentAfterClose) {
  LogStore log_store(path_, kStorageSize);
  EXPECT_TRUE(log_store.Init());

  const int32 kSize = disk_cache::kFlashSegmentFreeSpace;
  const std::vector<char> expected(kSize, 'a');

  int32 id1;
  EXPECT_EQ(0, log_store.write_index_);
  EXPECT_TRUE(log_store.CreateEntry(kSize/2, &id1));
  EXPECT_TRUE(log_store.WriteData(&expected[0], kSize/2));
  log_store.CloseEntry(id1);

  // Create a reference to above entry.
  EXPECT_TRUE(log_store.OpenEntry(id1));

  // This entry fills the first segment.
  int32 id2;
  EXPECT_EQ(0, log_store.write_index_);
  EXPECT_TRUE(log_store.CreateEntry(kSize/2, &id2));
  EXPECT_TRUE(log_store.WriteData(&expected[0], kSize/2));
  log_store.CloseEntry(id2);

  // Creating this entry forces closing of the first segment.
  int32 id3;
  EXPECT_TRUE(log_store.CreateEntry(kSize, &id3));
  EXPECT_EQ(1, log_store.write_index_);
  EXPECT_TRUE(log_store.WriteData(&expected[0], kSize));
  log_store.CloseEntry(id3);

  // Now attempt to read from the closed segment.
  std::vector<char> actual(kSize, 0);
  EXPECT_TRUE(log_store.ReadData(id1, &actual[0], kSize, id1));
  log_store.CloseEntry(id1);

  EXPECT_EQ(expected, actual);
  EXPECT_TRUE(log_store.Close());
}

// TODO(agayev): Add a test that confirms that in-use segment is not selected as
// the next write segment.

}  // namespace disk_cache
