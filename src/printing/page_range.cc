// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "printing/page_range.h"

#include <set>
#include <vector>

namespace printing {

/* static */
std::vector<int> PageRange::GetPages(const PageRanges& ranges) {
  std::set<int> pages;
  for (unsigned i = 0; i < ranges.size(); ++i) {
    const PageRange& range = ranges[i];
    // Ranges are inclusive.
    for (int i = range.from; i <= range.to; ++i) {
      pages.insert(i);
    }
  }
  return std::vector<int>(pages.begin(), pages.end());
}

}  // namespace printing
