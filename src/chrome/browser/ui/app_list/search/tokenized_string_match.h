// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_APP_LIST_SEARCH_TOKENIZED_STRING_MATCH_H_
#define CHROME_BROWSER_UI_APP_LIST_SEARCH_TOKENIZED_STRING_MATCH_H_

#include <vector>

#include "base/basictypes.h"
#include "base/strings/string16.h"
#include "ui/base/range/range.h"

namespace app_list {

class TokenizedString;

// TokenizedStringMatch takes two tokenized strings: one as the text and
// the other one as the query. It matches the query against the text,
// calculates a relevance score between [0, 1] and marks the matched portions
// of text. A relevance of zero means the two are completely different to each
// other. The higher the relevance score, the better the two strings are
// matched. Matched portions of text are stored as index ranges.
class TokenizedStringMatch {
 public:
  typedef std::vector<ui::Range> Hits;

  TokenizedStringMatch();
  ~TokenizedStringMatch();

  // Calculates the relevance and hits. Returns true if the two strings are
  // somewhat matched, i.e. relevance score is not zero.
  bool Calculate(const TokenizedString& query, const TokenizedString& text);

  // Convenience wrapper to calculate match from raw string input.
  bool Calculate(const string16& query, const string16& text);

  double relevance() const { return relevance_; }
  const Hits& hits() const { return hits_; }

 private:
  double relevance_;
  Hits hits_;

  DISALLOW_COPY_AND_ASSIGN(TokenizedStringMatch);
};

}  // namespace app_list

#endif  // CHROME_BROWSER_UI_APP_LIST_SEARCH_TOKENIZED_STRING_MATCH_H_
