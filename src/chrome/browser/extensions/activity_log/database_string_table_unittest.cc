// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/files/scoped_temp_dir.h"
#include "chrome/browser/extensions/activity_log/database_string_table.h"
#include "sql/connection.h"
#include "sql/statement.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace extensions {

class DatabaseStringTableTest : public testing::Test {
 protected:
  virtual void SetUp() OVERRIDE {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base::FilePath db_file = temp_dir_.path().AppendASCII("StringTable.db");

    ASSERT_TRUE(db_.Open(db_file));
  }

  virtual void TearDown() OVERRIDE {
    db_.Close();
  }

  base::ScopedTempDir temp_dir_;
  sql::Connection db_;
};

// Check that initializing the database works.
TEST_F(DatabaseStringTableTest, Init) {
  DatabaseStringTable table("test");
  table.Initialize(&db_);
  ASSERT_TRUE(db_.DoesTableExist("test"));
  ASSERT_TRUE(db_.DoesIndexExist("test_index"));
}

// Insert a new mapping into the table, then verify the table contents.
TEST_F(DatabaseStringTableTest, Insert) {
  DatabaseStringTable table("test");
  table.Initialize(&db_);
  int64 id;
  ASSERT_TRUE(table.StringToInt(&db_, "abc", &id));

  sql::Statement query(
      db_.GetUniqueStatement("SELECT id FROM test WHERE value = 'abc'"));
  ASSERT_TRUE(query.Step());
  int64 raw_id = query.ColumnInt64(0);
  ASSERT_EQ(id, raw_id);
}

// Check that different strings are mapped to different values, and the same
// string is mapped to the same value repeatably.
TEST_F(DatabaseStringTableTest, InsertMultiple) {
  DatabaseStringTable table("test");
  table.Initialize(&db_);

  int64 id1;
  int64 id2;
  ASSERT_TRUE(table.StringToInt(&db_, "string1", &id1));
  ASSERT_TRUE(table.StringToInt(&db_, "string2", &id2));
  ASSERT_NE(id1, id2);

  int64 id1a;
  ASSERT_TRUE(table.StringToInt(&db_, "string1", &id1a));
  ASSERT_EQ(id1, id1a);
}

// Check that values can be read back from the database even after the
// in-memory cache is cleared.
TEST_F(DatabaseStringTableTest, CacheCleared) {
  DatabaseStringTable table("test");
  table.Initialize(&db_);

  int64 id1;
  ASSERT_TRUE(table.StringToInt(&db_, "string1", &id1));

  table.ClearCache();

  int64 id2;
  ASSERT_TRUE(table.StringToInt(&db_, "string1", &id2));
  ASSERT_EQ(id1, id2);
}

// Check that direct database modifications are picked up after the cache is
// cleared.
TEST_F(DatabaseStringTableTest, DatabaseModified) {
  DatabaseStringTable table("test");
  table.Initialize(&db_);

  int64 id1;
  ASSERT_TRUE(table.StringToInt(&db_, "modified", &id1));

  ASSERT_TRUE(
      db_.Execute("UPDATE test SET id = id + 1 WHERE value = 'modified'"));

  int64 id2;
  ASSERT_TRUE(table.StringToInt(&db_, "modified", &id2));
  ASSERT_EQ(id1, id2);

  table.ClearCache();

  int64 id3;
  ASSERT_TRUE(table.StringToInt(&db_, "modified", &id3));
  ASSERT_EQ(id1 + 1, id3);
}

// Check that looking up an unknown id returns an error.
TEST_F(DatabaseStringTableTest, BadLookup) {
  DatabaseStringTable table("test");
  table.Initialize(&db_);
  std::string value;
  ASSERT_FALSE(table.IntToString(&db_, 1, &value));
}

// Check looking up an inserted value, both cached and not cached.
TEST_F(DatabaseStringTableTest, Lookup) {
  DatabaseStringTable table("test");
  table.Initialize(&db_);
  int64 id;
  ASSERT_TRUE(table.StringToInt(&db_, "abc", &id));

  std::string value;
  ASSERT_TRUE(table.IntToString(&db_, id, &value));
  ASSERT_EQ("abc", value);

  table.ClearCache();
  value = "";
  ASSERT_TRUE(table.IntToString(&db_, id, &value));
  ASSERT_EQ("abc", value);
}

}  // namespace extensions
