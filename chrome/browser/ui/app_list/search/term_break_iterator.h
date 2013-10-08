// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_APP_LIST_SEARCH_TERM_BREAK_ITERATOR_H_
#define CHROME_BROWSER_UI_APP_LIST_SEARCH_TERM_BREAK_ITERATOR_H_

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "base/strings/string16.h"

namespace base {
namespace i18n {
class UTF16CharIterator;
}
}

namespace app_list {

// TermBreakIterator breaks terms out of a word. Terms are broken on
// camel case boundaries and alpha/number boundaries. Numbers are defined
// as [0-9\.,]+.
//  e.g.
//   CamelCase -> Camel, Case
//   Python2.7 -> Python, 2.7
class TermBreakIterator {
 public:
  // Note that |word| must out live this iterator.
  explicit TermBreakIterator(const string16& word);
  ~TermBreakIterator();

  // Advance to the next term. Returns false if at the end of the word.
  bool Advance();

  // Returns the current term, which is the substr of |word_| in range
  // [prev_, pos_).
  const string16 GetCurrentTerm() const;

  size_t prev() const { return prev_; }
  size_t pos() const { return pos_; }

  static const size_t npos = -1;

 private:
  enum State {
    STATE_START,   // Initial state
    STATE_NUMBER,  // Current char is a number [0-9\.,].
    STATE_UPPER,   // Current char is upper case.
    STATE_LOWER,   // Current char is lower case.
    STATE_CHAR,    // Current char has no case, e.g. a cjk char.
    STATE_LAST,
  };

  // Returns new state for given |ch|.
  State GetNewState(char16 ch);

  const string16& word_;
  size_t prev_;
  size_t pos_;

  scoped_ptr<base::i18n::UTF16CharIterator> iter_;
  State state_;

  DISALLOW_COPY_AND_ASSIGN(TermBreakIterator);
};

}  // namespace app_list

#endif  // CHROME_BROWSER_UI_APP_LIST_SEARCH_TERM_BREAK_ITERATOR_H_
