// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_OUTPUT_MANAGED_MEMORY_POLICY_H_
#define CC_OUTPUT_MANAGED_MEMORY_POLICY_H_

#include "base/basictypes.h"
#include "cc/base/cc_export.h"
#include "cc/resources/tile_priority.h"

namespace cc {

struct CC_EXPORT ManagedMemoryPolicy {
  enum PriorityCutoff {
    CUTOFF_ALLOW_NOTHING,
    CUTOFF_ALLOW_REQUIRED_ONLY,
    CUTOFF_ALLOW_NICE_TO_HAVE,
    CUTOFF_ALLOW_EVERYTHING,
  };
  static const size_t kDefaultNumResourcesLimit;

  explicit ManagedMemoryPolicy(size_t bytes_limit_when_visible);
  ManagedMemoryPolicy(size_t bytes_limit_when_visible,
                      PriorityCutoff priority_cutoff_when_visible,
                      size_t bytes_limit_when_not_visible,
                      PriorityCutoff priority_cutoff_when_not_visible,
                      size_t num_resources_limit);
  bool operator==(const ManagedMemoryPolicy&) const;
  bool operator!=(const ManagedMemoryPolicy&) const;

  size_t bytes_limit_when_visible;
  PriorityCutoff priority_cutoff_when_visible;
  size_t bytes_limit_when_not_visible;
  PriorityCutoff priority_cutoff_when_not_visible;
  size_t num_resources_limit;

  static int PriorityCutoffToValue(PriorityCutoff priority_cutoff);
  static TileMemoryLimitPolicy PriorityCutoffToTileMemoryLimitPolicy(
      PriorityCutoff priority_cutoff);
};

}  // namespace cc

#endif  // CC_OUTPUT_MANAGED_MEMORY_POLICY_H_
