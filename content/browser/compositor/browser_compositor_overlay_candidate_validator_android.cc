// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/compositor/browser_compositor_overlay_candidate_validator_android.h"

#include "cc/output/overlay_processor.h"
#include "cc/output/overlay_strategy_underlay.h"
#include "ui/gfx/geometry/rect_conversions.h"

namespace content {

BrowserCompositorOverlayCandidateValidatorAndroid::
    BrowserCompositorOverlayCandidateValidatorAndroid() {}

BrowserCompositorOverlayCandidateValidatorAndroid::
    ~BrowserCompositorOverlayCandidateValidatorAndroid() {}

void BrowserCompositorOverlayCandidateValidatorAndroid::GetStrategies(
    cc::OverlayProcessor::StrategyList* strategies) {
  strategies->push_back(make_scoped_ptr(new cc::OverlayStrategyUnderlay(this)));
}

void BrowserCompositorOverlayCandidateValidatorAndroid::CheckOverlaySupport(
    cc::OverlayCandidateList* candidates) {
  // There should only be at most a single overlay candidate: the video quad.
  // There's no check that the presented candidate is really a video frame for
  // a fullscreen video. Instead it's assumed that if a quad is marked as
  // overlayable, it's a fullscreen video quad.
  DCHECK_LE(candidates->size(), 1u);

  if (!candidates->empty()) {
    cc::OverlayCandidate& candidate = candidates->front();
    candidate.display_rect =
        gfx::RectF(gfx::ToEnclosingRect(candidate.display_rect));
    candidate.overlay_handled = true;
    candidate.plane_z_order = -1;
  }
}

bool BrowserCompositorOverlayCandidateValidatorAndroid::AllowCALayerOverlays() {
  return false;
}

// Overlays will still be allowed when software mirroring is enabled, even
// though they won't appear in the mirror.
void BrowserCompositorOverlayCandidateValidatorAndroid::SetSoftwareMirrorMode(
    bool enabled) {}

}  // namespace content
