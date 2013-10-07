// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Unit tests for |Misspelling| object.

#include "chrome/browser/spellchecker/misspelling.h"

#include "base/json/json_reader.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

TEST(MisspellingTest, SerializeTest) {
  Misspelling misspelling;
  misspelling.context = ASCIIToUTF16("How doe sit know");
  misspelling.location = 4;
  misspelling.length = 7;
  misspelling.timestamp = base::Time::FromJsTime(42);
  misspelling.hash = 9001;
  misspelling.suggestions.push_back(ASCIIToUTF16("does it"));

  scoped_ptr<base::Value> expected(base::JSONReader::Read(
      "{\"originalText\": \"How doe sit know\","
      "\"misspelledStart\": 4,"
      "\"misspelledLength\": 7,"
      "\"timestamp\": \"42\","
      "\"suggestionId\":\"9001\","
      "\"suggestions\": [\"does it\"],"
      "\"userActions\": [{\"actionType\": \"PENDING\"}]}"));

  scoped_ptr<base::DictionaryValue> serialized(misspelling.Serialize());
  EXPECT_TRUE(serialized->Equals(expected.get()));
}

TEST(MisspellingTest, GetMisspelledStringTest) {
  Misspelling misspelling;
  misspelling.context = ASCIIToUTF16("How doe sit know");
  misspelling.location = 4;
  misspelling.length = 7;
  EXPECT_EQ(ASCIIToUTF16("doe sit"), misspelling.GetMisspelledString());

  misspelling.length = 0;
  EXPECT_EQ(string16(), misspelling.GetMisspelledString());

  misspelling.location = misspelling.context.length();
  misspelling.length = 7;
  EXPECT_EQ(string16(), misspelling.GetMisspelledString());

  misspelling.location = misspelling.context.length() + 1;
  EXPECT_EQ(string16(), misspelling.GetMisspelledString());
}
