// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/page_load_metrics/browser/page_load_metrics_util.h"

#include <algorithm>

#include "components/page_load_metrics/common/page_load_timing.h"

namespace page_load_metrics {

base::TimeDelta GetFirstContentfulPaint(const PageLoadTiming& timing) {
  if (timing.first_text_paint.is_zero())
    return timing.first_image_paint;
  if (timing.first_image_paint.is_zero())
    return timing.first_text_paint;
  return std::min(timing.first_text_paint, timing.first_image_paint);
}

}  // namespace page_load_metrics

