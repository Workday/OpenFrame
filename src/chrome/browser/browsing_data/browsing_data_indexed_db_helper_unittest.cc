// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "testing/gtest/include/gtest/gtest.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/browsing_data/browsing_data_indexed_db_helper.h"
#include "chrome/test/base/testing_profile.h"

namespace {

typedef testing::Test CannedBrowsingDataIndexedDBHelperTest;

TEST_F(CannedBrowsingDataIndexedDBHelperTest, Empty) {
  const GURL origin("http://host1:1/");
  const string16 description(ASCIIToUTF16("description"));

  scoped_refptr<CannedBrowsingDataIndexedDBHelper> helper(
      new CannedBrowsingDataIndexedDBHelper());

  ASSERT_TRUE(helper->empty());
  helper->AddIndexedDB(origin, description);
  ASSERT_FALSE(helper->empty());
  helper->Reset();
  ASSERT_TRUE(helper->empty());
}

TEST_F(CannedBrowsingDataIndexedDBHelperTest, IgnoreExtensionsAndDevTools) {
  const GURL origin1("chrome-extension://abcdefghijklmnopqrstuvwxyz/");
  const GURL origin2("chrome-devtools://abcdefghijklmnopqrstuvwxyz/");
  const string16 description(ASCIIToUTF16("description"));

  scoped_refptr<CannedBrowsingDataIndexedDBHelper> helper(
      new CannedBrowsingDataIndexedDBHelper());

  ASSERT_TRUE(helper->empty());
  helper->AddIndexedDB(origin1, description);
  ASSERT_TRUE(helper->empty());
  helper->AddIndexedDB(origin2, description);
  ASSERT_TRUE(helper->empty());
}

} // namespace
