// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/translate/translate_prefs.h"

#include <algorithm>
#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

class TranslatePrefsTest : public testing::Test {
 public:
  TranslatePrefsTest() {}
  virtual ~TranslatePrefsTest() {}
};

TEST_F(TranslatePrefsTest, CreateBlockedLanguages) {
  std::vector<std::string> blacklisted_languages;
  blacklisted_languages.push_back("en");
  blacklisted_languages.push_back("fr");
  // Hebrew: synonym to 'he'
  blacklisted_languages.push_back("iw");
  // Haitian is not used as Accept-Language
  blacklisted_languages.push_back("ht");

  std::vector<std::string> accept_languages;
  accept_languages.push_back("en");
  accept_languages.push_back("ja");
  // Filippino: synonym to 'tl'
  accept_languages.push_back("fil");
  // General Chinese is not used as Translate language, but not filtered
  // when merging.
  accept_languages.push_back("zh");

  std::vector<std::string> blocked_languages;

  TranslatePrefs::CreateBlockedLanguages(&blocked_languages,
                                         blacklisted_languages,
                                         accept_languages);

  // The order of the elements cannot be determined.
  std::vector<std::string> expected;
  expected.push_back("en");
  expected.push_back("fr");
  expected.push_back("iw");
  expected.push_back("ht");
  expected.push_back("ja");
  expected.push_back("tl");
  expected.push_back("zh");

  EXPECT_EQ(expected.size(), blocked_languages.size());
  for (std::vector<std::string>::const_iterator it = expected.begin();
       it != expected.end(); ++it) {
    EXPECT_NE(blocked_languages.end(),
              std::find(blocked_languages.begin(),
                        blocked_languages.end(),
                        *it));
  }
}
