// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Helper functions used for URLMatcher and Declarative APIs.

#ifndef EXTENSIONS_COMMON_MATCHER_URL_MATCHER_HELPERS_H_
#define EXTENSIONS_COMMON_MATCHER_URL_MATCHER_HELPERS_H_

#include <string>
#include <vector>

namespace base {
class Value;
}

namespace extensions {
namespace url_matcher_helpers {

// Converts a ValueList |value| of strings into a vector. Returns true if
// successful.
bool GetAsStringVector(const base::Value* value, std::vector<std::string>* out);

}  // namespace declarative_helpers
}  // namespace extensions

#endif  // EXTENSIONS_COMMON_MATCHER_URL_MATCHER_HELPERS_H_
