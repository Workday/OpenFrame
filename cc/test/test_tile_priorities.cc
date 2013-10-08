// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/test/test_tile_priorities.h"

namespace cc {

TilePriorityForSoonBin::TilePriorityForSoonBin()
    : TilePriority(HIGH_RESOLUTION, 0.5, 300.0) {}

TilePriorityForEventualBin::TilePriorityForEventualBin()
    : TilePriority(NON_IDEAL_RESOLUTION, 1.0, 315.0) {}

TilePriorityForNowBin::TilePriorityForNowBin()
    : TilePriority(HIGH_RESOLUTION, 0, 0) {}

TilePriorityRequiredForActivation::TilePriorityRequiredForActivation()
    : TilePriority(HIGH_RESOLUTION, 0, 0) {
  required_for_activation = true;
}

}  // namespace cc
