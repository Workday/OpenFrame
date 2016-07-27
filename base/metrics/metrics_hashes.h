// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_METRICS_METRICS_HASHES_H_
#define BASE_METRICS_METRICS_HASHES_H_

#include <stdint.h>
#include <string>

#include "base/base_export.h"

namespace base {

// Computes a uint64 hash of a given string based on its MD5 hash. Suitable for
// metric names.
BASE_EXPORT uint64_t HashMetricName(const std::string& name);

}  // namespace metrics

#endif  // BASE_METRICS_METRICS_HASHES_H_
