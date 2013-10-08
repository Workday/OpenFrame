// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/basictypes.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/app_list/search/tokenized_string_match.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace app_list {
namespace test {

// Returns a string of |text| marked the hits in |match| using block bracket.
// e.g. text= "Text", hits = [{0,1}], returns "[T]ext".
std::string MatchHit(const string16& text,
                     const TokenizedStringMatch& match) {
  string16 marked = text;

  const TokenizedStringMatch::Hits& hits = match.hits();
  for (TokenizedStringMatch::Hits::const_reverse_iterator it = hits.rbegin();
       it != hits.rend(); ++it) {
    const ui::Range& hit = *it;
    marked.insert(hit.end(), 1, ']');
    marked.insert(hit.start(), 1, '[');
  }

  return UTF16ToUTF8(marked);
}

TEST(TokenizedStringMatchTest, NotMatch) {
  struct {
    const char* text;
    const char* query;
  } kTestCases[] = {
    { "", "" },
    { "", "query" },
    { "text", "" },
    { "!", "!@#$%^&*()<<<**>>>" },
    { "abd", "abcd"},
    { "cd", "abcd"},
  };

  TokenizedStringMatch match;
  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(kTestCases); ++i) {
    const string16 text(UTF8ToUTF16(kTestCases[i].text));
    EXPECT_FALSE(match.Calculate(UTF8ToUTF16(kTestCases[i].query), text))
        << "Test case " << i
        << " : text=" << kTestCases[i].text
        << ", query=" << kTestCases[i].query;
  }
}

TEST(TokenizedStringMatchTest, Match) {
  struct {
    const char* text;
    const char* query;
    const char* expect;
  } kTestCases[] = {
    { "ScratchPad", "pad", "Scratch[Pad]" },
    { "ScratchPad", "sp", "[S]cratch[P]ad" },
    { "Chess2", "che", "[Che]ss2" },
    { "Chess2", "c2", "[C]hess[2]" },
    { "Cut the rope", "cut ro", "[Cut] the [ro]pe" },
    { "Cut the rope", "cr", "[C]ut the [r]ope" },
    { "John Doe", "jdoe", "[J]ohn [Doe]" },
    { "John Doe", "johnd", "[John D]oe" },
  };

  TokenizedStringMatch match;
  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(kTestCases); ++i) {
    const string16 text(UTF8ToUTF16(kTestCases[i].text));
    EXPECT_TRUE(match.Calculate(UTF8ToUTF16(kTestCases[i].query), text));
    EXPECT_EQ(kTestCases[i].expect, MatchHit(text, match));
  }
}

TEST(TokenizedStringMatchTest, Relevance) {
  struct {
    const char* text;
    const char* query_low;
    const char* query_high;
  } kTestCases[] = {
    // More matched chars are better.
    { "Google Chrome", "g", "go" },
    { "Google Chrome", "go", "goo" },
    { "Google Chrome", "goo", "goog" },
    { "Google Chrome", "c", "ch" },
    { "Google Chrome", "ch", "chr" },
    // Acronym match is better than something in the middle.
    { "Google Chrome", "ch", "gc" },
    // Prefix match is better than middle match and acronym match.
    { "Google Chrome", "ch", "go" },
    { "Google Chrome", "gc", "go" },
  };

  TokenizedStringMatch match_low;
  TokenizedStringMatch match_high;
  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(kTestCases); ++i) {
    const string16 text(UTF8ToUTF16(kTestCases[i].text));
    EXPECT_TRUE(
        match_low.Calculate(UTF8ToUTF16(kTestCases[i].query_low), text));
    EXPECT_TRUE(
        match_high.Calculate(UTF8ToUTF16(kTestCases[i].query_high), text));
    EXPECT_LT(match_low.relevance(), match_high.relevance())
        << "Test case " << i
        << " : text=" << kTestCases[i].text
        << ", query_low=" << kTestCases[i].query_low
        << ", query_high=" << kTestCases[i].query_high;
  }
}

}  // namespace test
}  // namespace app_list
