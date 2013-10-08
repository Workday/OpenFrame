// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/declarative_webrequest/request_stage.h"

#include "base/basictypes.h"

namespace extensions {

const unsigned int kActiveStages = ON_BEFORE_REQUEST |
                                   ON_BEFORE_SEND_HEADERS |
                                   ON_HEADERS_RECEIVED |
                                   ON_AUTH_REQUIRED;

// HighestBit<n> computes the highest bit of |n| in compile time, provided that
// |n| is a positive compile-time constant.
template <long unsigned int n>
struct HighestBit {
  COMPILE_ASSERT(n > 0, argument_is_not_a_positive_compile_time_constant);
  enum { VALUE = HighestBit<(n >> 1)>::VALUE << 1 };
};
template <>
struct HighestBit<1> {
  enum { VALUE = 1 };
};

const unsigned int kLastActiveStage = HighestBit<kActiveStages>::VALUE;

}  // namespace extensions
