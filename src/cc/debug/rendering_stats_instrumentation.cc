// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/debug/rendering_stats_instrumentation.h"

namespace cc {

// static
scoped_ptr<RenderingStatsInstrumentation>
    RenderingStatsInstrumentation::Create() {
  return make_scoped_ptr(new RenderingStatsInstrumentation());
}

RenderingStatsInstrumentation::RenderingStatsInstrumentation()
    : record_rendering_stats_(false) {
}

RenderingStatsInstrumentation::~RenderingStatsInstrumentation() {}

RenderingStats RenderingStatsInstrumentation::GetRenderingStats() {
  base::AutoLock scoped_lock(lock_);
  return rendering_stats_;
}

base::TimeTicks RenderingStatsInstrumentation::StartRecording() const {
  if (record_rendering_stats_)
    return base::TimeTicks::HighResNow();
  return base::TimeTicks();
}

base::TimeDelta RenderingStatsInstrumentation::EndRecording(
    base::TimeTicks start_time) const {
  if (!start_time.is_null())
    return base::TimeTicks::HighResNow() - start_time;
  return base::TimeDelta();
}

void RenderingStatsInstrumentation::IncrementAnimationFrameCount() {
  if (!record_rendering_stats_)
    return;

  base::AutoLock scoped_lock(lock_);
  rendering_stats_.animation_frame_count++;
}

void RenderingStatsInstrumentation::SetScreenFrameCount(int64 count) {
  if (!record_rendering_stats_)
    return;

  base::AutoLock scoped_lock(lock_);
  rendering_stats_.screen_frame_count = count;
}

void RenderingStatsInstrumentation::SetDroppedFrameCount(int64 count) {
  if (!record_rendering_stats_)
    return;

  base::AutoLock scoped_lock(lock_);
  rendering_stats_.dropped_frame_count = count;
}

void RenderingStatsInstrumentation::AddCommit(base::TimeDelta duration) {
  if (!record_rendering_stats_)
    return;

  base::AutoLock scoped_lock(lock_);
  rendering_stats_.total_commit_time += duration;
  rendering_stats_.total_commit_count++;
}

void RenderingStatsInstrumentation::AddPaint(base::TimeDelta duration,
                                             int64 pixels) {
  if (!record_rendering_stats_)
    return;

  base::AutoLock scoped_lock(lock_);
  rendering_stats_.total_paint_time += duration;
  rendering_stats_.total_pixels_painted += pixels;
}

void RenderingStatsInstrumentation::AddRecord(base::TimeDelta duration,
                                              int64 pixels) {
  if (!record_rendering_stats_)
    return;

  base::AutoLock scoped_lock(lock_);
  rendering_stats_.total_record_time += duration;
  rendering_stats_.total_pixels_recorded += pixels;
}

void RenderingStatsInstrumentation::AddRaster(base::TimeDelta total_duration,
                                              base::TimeDelta best_duration,
                                              int64 pixels,
                                              bool is_in_pending_tree_now_bin) {
  if (!record_rendering_stats_)
    return;

  base::AutoLock scoped_lock(lock_);
  rendering_stats_.total_rasterize_time += total_duration;
  rendering_stats_.best_rasterize_time += best_duration;
  rendering_stats_.total_pixels_rasterized += pixels;

  if (is_in_pending_tree_now_bin) {
    rendering_stats_.total_rasterize_time_for_now_bins_on_pending_tree +=
        total_duration;
  }
}

void RenderingStatsInstrumentation::IncrementImplThreadScrolls() {
  if (!record_rendering_stats_)
    return;

  base::AutoLock scoped_lock(lock_);
  rendering_stats_.num_impl_thread_scrolls++;
}

void RenderingStatsInstrumentation::IncrementMainThreadScrolls() {
  if (!record_rendering_stats_)
    return;

  base::AutoLock scoped_lock(lock_);
  rendering_stats_.num_main_thread_scrolls++;
}

void RenderingStatsInstrumentation::AddLayersDrawn(int64 amount) {
  if (!record_rendering_stats_)
    return;

  base::AutoLock scoped_lock(lock_);
  rendering_stats_.num_layers_drawn += amount;
}

void RenderingStatsInstrumentation::AddMissingTiles(int64 amount) {
  if (!record_rendering_stats_)
    return;

  base::AutoLock scoped_lock(lock_);
  rendering_stats_.num_missing_tiles += amount;
}

void RenderingStatsInstrumentation::AddDeferredImageDecode(
    base::TimeDelta duration) {
  if (!record_rendering_stats_)
    return;

  base::AutoLock scoped_lock(lock_);
  rendering_stats_.total_deferred_image_decode_time += duration;
  rendering_stats_.total_deferred_image_decode_count++;
}

void RenderingStatsInstrumentation::AddImageGathering(
    base::TimeDelta duration) {
  if (!record_rendering_stats_)
    return;

  base::AutoLock scoped_lock(lock_);
  rendering_stats_.total_image_gathering_time += duration;
  rendering_stats_.total_image_gathering_count++;
}

void RenderingStatsInstrumentation::IncrementDeferredImageCacheHitCount() {
  if (!record_rendering_stats_)
    return;

  base::AutoLock scoped_lock(lock_);
  rendering_stats_.total_deferred_image_cache_hit_count++;
}

void RenderingStatsInstrumentation::AddAnalysisResult(
    base::TimeDelta duration,
    bool is_solid_color) {
  if (!record_rendering_stats_)
    return;

  base::AutoLock scoped_lock(lock_);
  rendering_stats_.total_tiles_analyzed++;
  rendering_stats_.total_tile_analysis_time += duration;
  if (is_solid_color)
    rendering_stats_.solid_color_tiles_analyzed++;
}

}  // namespace cc
