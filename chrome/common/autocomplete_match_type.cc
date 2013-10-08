// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/autocomplete_match_type.h"

#include "base/basictypes.h"

// static
std::string AutocompleteMatchType::ToString(AutocompleteMatchType::Type type) {
  const char* strings[] = {
    "url-what-you-typed",
    "history-url",
    "history-title",
    "history-body",
    "history-keyword",
    "navsuggest",
    "search-what-you-typed",
    "search-history",
    "search-suggest",
    "search-other-engine",
    "extension-app",
    "contact",
    "bookmark-title",
  };
  COMPILE_ASSERT(arraysize(strings) == AutocompleteMatchType::NUM_TYPES,
                 strings_array_must_match_type_enum);
  return strings[type];
}
