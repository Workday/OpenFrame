// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/playback/display_list_recording_source.h"

#include <algorithm>

#include "base/numerics/safe_math.h"
#include "cc/base/histograms.h"
#include "cc/base/region.h"
#include "cc/layers/content_layer_client.h"
#include "cc/playback/display_item_list.h"
#include "cc/playback/display_list_raster_source.h"
#include "skia/ext/analysis_canvas.h"

namespace {

#ifdef NDEBUG
const bool kDefaultClearCanvasSetting = false;
#else
const bool kDefaultClearCanvasSetting = true;
#endif

DEFINE_SCOPED_UMA_HISTOGRAM_AREA_TIMER(
    ScopedDisplayListRecordingSourceUpdateTimer,
    "Compositing.%s.DisplayListRecordingSource.UpdateUs",
    "Compositing.%s.DisplayListRecordingSource.UpdateInvalidatedAreaPerMs");

}  // namespace

namespace cc {

DisplayListRecordingSource::DisplayListRecordingSource()
    : slow_down_raster_scale_factor_for_debug_(0),
      generate_discardable_images_metadata_(false),
      requires_clear_(false),
      is_solid_color_(false),
      clear_canvas_with_debug_color_(kDefaultClearCanvasSetting),
      solid_color_(SK_ColorTRANSPARENT),
      background_color_(SK_ColorTRANSPARENT),
      painter_reported_memory_usage_(0) {}

DisplayListRecordingSource::~DisplayListRecordingSource() {
}

void DisplayListRecordingSource::UpdateInvalidationForNewViewport(
    const gfx::Rect& old_recorded_viewport,
    const gfx::Rect& new_recorded_viewport,
    Region* invalidation) {
  // Invalidate newly-exposed and no-longer-exposed areas.
  Region newly_exposed_region(new_recorded_viewport);
  newly_exposed_region.Subtract(old_recorded_viewport);
  invalidation->Union(newly_exposed_region);

  Region no_longer_exposed_region(old_recorded_viewport);
  no_longer_exposed_region.Subtract(new_recorded_viewport);
  invalidation->Union(no_longer_exposed_region);
}

bool DisplayListRecordingSource::UpdateAndExpandInvalidation(
    ContentLayerClient* painter,
    Region* invalidation,
    const gfx::Size& layer_size,
    const gfx::Rect& visible_layer_rect,
    int frame_number,
    RecordingMode recording_mode) {
  ScopedDisplayListRecordingSourceUpdateTimer timer;
  bool updated = false;

  // TODO(chrishtr): delete this conditional once synchronized paint launches.
  if (size_ != layer_size) {
    size_ = layer_size;
    updated = true;
  }

  gfx::Rect new_recorded_viewport = painter->PaintableRegion();
  if (new_recorded_viewport != recorded_viewport_) {
    UpdateInvalidationForNewViewport(recorded_viewport_, new_recorded_viewport,
                                     invalidation);
    recorded_viewport_ = new_recorded_viewport;
    updated = true;
  }

  // Count the area that is being invalidated.
  Region recorded_invalidation(*invalidation);
  recorded_invalidation.Intersect(recorded_viewport_);
  for (Region::Iterator it(recorded_invalidation); it.has_rect(); it.next())
    timer.AddArea(it.rect().size().GetCheckedArea());

  if (!updated && !invalidation->Intersects(recorded_viewport_))
    return false;

  ContentLayerClient::PaintingControlSetting painting_control =
      ContentLayerClient::PAINTING_BEHAVIOR_NORMAL;

  switch (recording_mode) {
    case RECORD_NORMALLY:
      // Already setup for normal recording.
      break;
    case RECORD_WITH_PAINTING_DISABLED:
      painting_control = ContentLayerClient::DISPLAY_LIST_PAINTING_DISABLED;
      break;
    case RECORD_WITH_CACHING_DISABLED:
      painting_control = ContentLayerClient::DISPLAY_LIST_CACHING_DISABLED;
      break;
    case RECORD_WITH_CONSTRUCTION_DISABLED:
      painting_control = ContentLayerClient::DISPLAY_LIST_CONSTRUCTION_DISABLED;
      break;
    case RECORD_WITH_SK_NULL_CANVAS:
    case RECORDING_MODE_COUNT:
      NOTREACHED();
  }

  // TODO(vmpstr): Add a slow_down_recording_scale_factor_for_debug_ to be able
  // to slow down recording.
  display_list_ =
      painter->PaintContentsToDisplayList(recorded_viewport_, painting_control);
  painter_reported_memory_usage_ = painter->GetApproximateUnsharedMemoryUsage();

  DetermineIfSolidColor();
  display_list_->EmitTraceSnapshot();
  if (generate_discardable_images_metadata_)
    display_list_->GenerateDiscardableImagesMetadata();

  return true;
}

gfx::Size DisplayListRecordingSource::GetSize() const {
  return size_;
}

void DisplayListRecordingSource::SetEmptyBounds() {
  size_ = gfx::Size();
  Clear();
}

void DisplayListRecordingSource::SetSlowdownRasterScaleFactor(int factor) {
  slow_down_raster_scale_factor_for_debug_ = factor;
}

void DisplayListRecordingSource::SetGenerateDiscardableImagesMetadata(
    bool generate_metadata) {
  generate_discardable_images_metadata_ = generate_metadata;
}

void DisplayListRecordingSource::SetBackgroundColor(SkColor background_color) {
  background_color_ = background_color;
}

void DisplayListRecordingSource::SetRequiresClear(bool requires_clear) {
  requires_clear_ = requires_clear;
}

bool DisplayListRecordingSource::IsSuitableForGpuRasterization() const {
  // The display list needs to be created (see: UpdateAndExpandInvalidation)
  // before checking for suitability. There are cases where an update will not
  // create a display list (e.g., if the size is empty). We return true in these
  // cases because the gpu suitability bit sticks false.
  return !display_list_ || display_list_->IsSuitableForGpuRasterization();
}

scoped_refptr<DisplayListRasterSource>
DisplayListRecordingSource::CreateRasterSource(bool can_use_lcd_text) const {
  return scoped_refptr<DisplayListRasterSource>(
      DisplayListRasterSource::CreateFromDisplayListRecordingSource(
          this, can_use_lcd_text));
}

void DisplayListRecordingSource::DetermineIfSolidColor() {
  DCHECK(display_list_);
  is_solid_color_ = false;
  solid_color_ = SK_ColorTRANSPARENT;

  if (!display_list_->ShouldBeAnalyzedForSolidColor())
    return;

  gfx::Size layer_size = GetSize();
  skia::AnalysisCanvas canvas(layer_size.width(), layer_size.height());
  display_list_->Raster(&canvas, nullptr, gfx::Rect(), 1.f);
  is_solid_color_ = canvas.GetColorIfSolid(&solid_color_);
}

void DisplayListRecordingSource::Clear() {
  recorded_viewport_ = gfx::Rect();
  display_list_ = nullptr;
  painter_reported_memory_usage_ = 0;
  is_solid_color_ = false;
}

}  // namespace cc
