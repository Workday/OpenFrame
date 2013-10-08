// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SPELLCHECKER_WORD_TRIMMER_H_
#define CHROME_BROWSER_SPELLCHECKER_WORD_TRIMMER_H_

#include "base/strings/string16.h"

// Trims |text| to contain only the range from |start| to |end| and |keep| words
// on either side of the range. The |start| and |end| parameters are character
// indexes into |text|. The |keep| parameter is the number of words to keep on
// either side of the |start|-|end| range. The function updates |start| in
// accordance with the trimming.
//
// Example:
//
//  size_t start = 14;
//  size_t end = 23;
//  string16 text = ASCIIToUTF16("one two three four five six seven eight");
//  int keep = 2;
//  string16 trimmed = TrimWords(&start, end, text, keep);
//  DCHECK(trimmed == ASCIIToUTF16("two three four five six seven"));
//  DCHECK(start == 10);
//
string16 TrimWords(
    size_t* start,
    size_t end,
    const string16& text,
    size_t keep);

#endif  // CHROME_BROWSER_SPELLCHECKER_WORD_TRIMMER_H_
