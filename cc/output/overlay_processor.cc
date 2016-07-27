// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/output/overlay_processor.h"

#include "cc/output/output_surface.h"
#include "cc/output/overlay_strategy_single_on_top.h"
#include "cc/output/overlay_strategy_underlay.h"
#include "cc/quads/draw_quad.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/transform.h"

namespace cc {

OverlayProcessor::OverlayProcessor(OutputSurface* surface) : surface_(surface) {
}

void OverlayProcessor::Initialize() {
  DCHECK(surface_);
  OverlayCandidateValidator* validator =
      surface_->GetOverlayCandidateValidator();
  if (validator)
    validator->GetStrategies(&strategies_);
}

OverlayProcessor::~OverlayProcessor() {}

gfx::Rect OverlayProcessor::GetAndResetOverlayDamage() {
  gfx::Rect result = overlay_damage_rect_;
  overlay_damage_rect_ = gfx::Rect();
  return result;
}

bool OverlayProcessor::ProcessForCALayers(
    ResourceProvider* resource_provider,
    RenderPassList* render_passes,
    OverlayCandidateList* overlay_candidates,
    CALayerOverlayList* ca_layer_overlays,
    gfx::Rect* damage_rect) {
  RenderPass* root_render_pass = render_passes->back().get();

  OverlayCandidateValidator* overlay_validator =
      surface_->GetOverlayCandidateValidator();
  if (!overlay_validator || !overlay_validator->AllowCALayerOverlays())
    return false;

  if (!ProcessForCALayerOverlays(
          resource_provider, gfx::RectF(root_render_pass->output_rect),
          root_render_pass->quad_list, ca_layer_overlays))
    return false;

  // CALayer overlays are all-or-nothing. If all quads were replaced with
  // layers then clear the list and remove the backbuffer from the overcandidate
  // list.
  overlay_candidates->clear();
  render_passes->back()->quad_list.clear();
  overlay_damage_rect_ = root_render_pass->output_rect;
  *damage_rect = gfx::Rect();
  return true;
}

void OverlayProcessor::ProcessForOverlays(ResourceProvider* resource_provider,
                                          RenderPassList* render_passes,
                                          OverlayCandidateList* candidates,
                                          CALayerOverlayList* ca_layer_overlays,
                                          gfx::Rect* damage_rect) {
  // First attempt to process for CALayers.
  if (ProcessForCALayers(resource_provider, render_passes, candidates,
                         ca_layer_overlays, damage_rect)) {
    return;
  }

  // Only if that fails, attempt hardware overlay strategies.
  for (const auto& strategy : strategies_) {
    if (!strategy->Attempt(resource_provider, render_passes, candidates))
      continue;

    // Subtract on-top overlays from the damage rect, unless the overlays use
    // the backbuffer as their content (in which case, add their combined rect
    // back to the damage at the end).
    gfx::Rect output_surface_overlay_damage_rect;
    for (const OverlayCandidate& overlay : *candidates) {
      if (overlay.plane_z_order > 0) {
        const gfx::Rect overlay_display_rect =
            ToEnclosedRect(overlay.display_rect);
        overlay_damage_rect_.Union(overlay_display_rect);
        damage_rect->Subtract(overlay_display_rect);
        if (overlay.use_output_surface_for_resource)
          output_surface_overlay_damage_rect.Union(overlay_display_rect);
      }
    }
    damage_rect->Union(output_surface_overlay_damage_rect);
    return;
  }
}

}  // namespace cc
