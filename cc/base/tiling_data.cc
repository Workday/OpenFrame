// Copyright 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/base/tiling_data.h"

#include <algorithm>

#include "ui/gfx/rect.h"
#include "ui/gfx/vector2d.h"

namespace cc {

static int ComputeNumTiles(int max_texture_size,
                           int total_size,
                           int border_texels) {
  if (max_texture_size - 2 * border_texels <= 0)
    return total_size > 0 && max_texture_size >= total_size ? 1 : 0;

  int num_tiles = std::max(1,
                           1 + (total_size - 1 - 2 * border_texels) /
                           (max_texture_size - 2 * border_texels));
  return total_size > 0 ? num_tiles : 0;
}

TilingData::TilingData()
    : border_texels_(0) {
  RecomputeNumTiles();
}

TilingData::TilingData(
    gfx::Size max_texture_size,
    gfx::Size total_size,
    bool has_border_texels)
    : max_texture_size_(max_texture_size),
      total_size_(total_size),
      border_texels_(has_border_texels ? 1 : 0) {
  RecomputeNumTiles();
}

TilingData::TilingData(
    gfx::Size max_texture_size,
    gfx::Size total_size,
    int border_texels)
    : max_texture_size_(max_texture_size),
      total_size_(total_size),
      border_texels_(border_texels) {
  RecomputeNumTiles();
}

void TilingData::SetTotalSize(gfx::Size total_size) {
  total_size_ = total_size;
  RecomputeNumTiles();
}

void TilingData::SetMaxTextureSize(gfx::Size max_texture_size) {
  max_texture_size_ = max_texture_size;
  RecomputeNumTiles();
}

void TilingData::SetHasBorderTexels(bool has_border_texels) {
  border_texels_ = has_border_texels ? 1 : 0;
  RecomputeNumTiles();
}

void TilingData::SetBorderTexels(int border_texels) {
  border_texels_ = border_texels;
  RecomputeNumTiles();
}

int TilingData::TileXIndexFromSrcCoord(int src_position) const {
  if (num_tiles_x_ <= 1)
    return 0;

  DCHECK_GT(max_texture_size_.width() - 2 * border_texels_, 0);
  int x = (src_position - border_texels_) /
      (max_texture_size_.width() - 2 * border_texels_);
  return std::min(std::max(x, 0), num_tiles_x_ - 1);
}

int TilingData::TileYIndexFromSrcCoord(int src_position) const {
  if (num_tiles_y_ <= 1)
    return 0;

  DCHECK_GT(max_texture_size_.height() - 2 * border_texels_, 0);
  int y = (src_position - border_texels_) /
      (max_texture_size_.height() - 2 * border_texels_);
  return std::min(std::max(y, 0), num_tiles_y_ - 1);
}

int TilingData::FirstBorderTileXIndexFromSrcCoord(int src_position) const {
  if (num_tiles_x_ <= 1)
    return 0;

  DCHECK_GT(max_texture_size_.width() - 2 * border_texels_, 0);
  int inner_tile_size = max_texture_size_.width() - 2 * border_texels_;
  int x = (src_position - 2 * border_texels_) / inner_tile_size;
  return std::min(std::max(x, 0), num_tiles_x_ - 1);
}

int TilingData::FirstBorderTileYIndexFromSrcCoord(int src_position) const {
  if (num_tiles_y_ <= 1)
    return 0;

  DCHECK_GT(max_texture_size_.height() - 2 * border_texels_, 0);
  int inner_tile_size = max_texture_size_.height() - 2 * border_texels_;
  int y = (src_position - 2 * border_texels_) / inner_tile_size;
  return std::min(std::max(y, 0), num_tiles_y_ - 1);
}

int TilingData::LastBorderTileXIndexFromSrcCoord(int src_position) const {
  if (num_tiles_x_ <= 1)
    return 0;

  DCHECK_GT(max_texture_size_.width() - 2 * border_texels_, 0);
  int inner_tile_size = max_texture_size_.width() - 2 * border_texels_;
  int x = src_position / inner_tile_size;
  return std::min(std::max(x, 0), num_tiles_x_ - 1);
}

int TilingData::LastBorderTileYIndexFromSrcCoord(int src_position) const {
  if (num_tiles_y_ <= 1)
    return 0;

  DCHECK_GT(max_texture_size_.height() - 2 * border_texels_, 0);
  int inner_tile_size = max_texture_size_.height() - 2 * border_texels_;
  int y = src_position / inner_tile_size;
  return std::min(std::max(y, 0), num_tiles_y_ - 1);
}

gfx::Rect TilingData::TileBounds(int i, int j) const {
  AssertTile(i, j);
  int max_texture_size_x = max_texture_size_.width() - 2 * border_texels_;
  int max_texture_size_y = max_texture_size_.height() - 2 * border_texels_;
  int total_size_x = total_size_.width();
  int total_size_y = total_size_.height();

  int lo_x = max_texture_size_x * i;
  if (i != 0)
    lo_x += border_texels_;

  int lo_y = max_texture_size_y * j;
  if (j != 0)
    lo_y += border_texels_;

  int hi_x = max_texture_size_x * (i + 1) + border_texels_;
  if (i + 1 == num_tiles_x_)
    hi_x += border_texels_;

  int hi_y = max_texture_size_y * (j + 1) + border_texels_;
  if (j + 1 == num_tiles_y_)
    hi_y += border_texels_;

  hi_x = std::min(hi_x, total_size_x);
  hi_y = std::min(hi_y, total_size_y);

  int x = lo_x;
  int y = lo_y;
  int width = hi_x - lo_x;
  int height = hi_y - lo_y;
  DCHECK_GE(x, 0);
  DCHECK_GE(y, 0);
  DCHECK_GE(width, 0);
  DCHECK_GE(height, 0);
  DCHECK_LE(x, total_size_.width());
  DCHECK_LE(y, total_size_.height());
  return gfx::Rect(x, y, width, height);
}

gfx::Rect TilingData::TileBoundsWithBorder(int i, int j) const {
  AssertTile(i, j);
  int max_texture_size_x = max_texture_size_.width() - 2 * border_texels_;
  int max_texture_size_y = max_texture_size_.height() - 2 * border_texels_;
  int total_size_x = total_size_.width();
  int total_size_y = total_size_.height();

  int lo_x = max_texture_size_x * i;
  int lo_y = max_texture_size_y * j;

  int hi_x = lo_x + max_texture_size_x + 2 * border_texels_;
  int hi_y = lo_y + max_texture_size_y + 2 * border_texels_;

  hi_x = std::min(hi_x, total_size_x);
  hi_y = std::min(hi_y, total_size_y);

  int x = lo_x;
  int y = lo_y;
  int width = hi_x - lo_x;
  int height = hi_y - lo_y;
  DCHECK_GE(x, 0);
  DCHECK_GE(y, 0);
  DCHECK_GE(width, 0);
  DCHECK_GE(height, 0);
  DCHECK_LE(x, total_size_.width());
  DCHECK_LE(y, total_size_.height());
  return gfx::Rect(x, y, width, height);
}

int TilingData::TilePositionX(int x_index) const {
  DCHECK_GE(x_index, 0);
  DCHECK_LT(x_index, num_tiles_x_);

  int pos = (max_texture_size_.width() - 2 * border_texels_) * x_index;
  if (x_index != 0)
    pos += border_texels_;

  return pos;
}

int TilingData::TilePositionY(int y_index) const {
  DCHECK_GE(y_index, 0);
  DCHECK_LT(y_index, num_tiles_y_);

  int pos = (max_texture_size_.height() - 2 * border_texels_) * y_index;
  if (y_index != 0)
    pos += border_texels_;

  return pos;
}

int TilingData::TileSizeX(int x_index) const {
  DCHECK_GE(x_index, 0);
  DCHECK_LT(x_index, num_tiles_x_);

  if (!x_index && num_tiles_x_ == 1)
    return total_size_.width();
  if (!x_index && num_tiles_x_ > 1)
    return max_texture_size_.width() - border_texels_;
  if (x_index < num_tiles_x_ - 1)
    return max_texture_size_.width() - 2 * border_texels_;
  if (x_index == num_tiles_x_ - 1)
    return total_size_.width() - TilePositionX(x_index);

  NOTREACHED();
  return 0;
}

int TilingData::TileSizeY(int y_index) const {
  DCHECK_GE(y_index, 0);
  DCHECK_LT(y_index, num_tiles_y_);

  if (!y_index && num_tiles_y_ == 1)
    return total_size_.height();
  if (!y_index && num_tiles_y_ > 1)
    return max_texture_size_.height() - border_texels_;
  if (y_index < num_tiles_y_ - 1)
    return max_texture_size_.height() - 2 * border_texels_;
  if (y_index == num_tiles_y_ - 1)
    return total_size_.height() - TilePositionY(y_index);

  NOTREACHED();
  return 0;
}

gfx::Vector2d TilingData::TextureOffset(int x_index, int y_index) const {
  int left = (!x_index || num_tiles_x_ == 1) ? 0 : border_texels_;
  int top = (!y_index || num_tiles_y_ == 1) ? 0 : border_texels_;

  return gfx::Vector2d(left, top);
}

void TilingData::RecomputeNumTiles() {
  num_tiles_x_ = ComputeNumTiles(
      max_texture_size_.width(), total_size_.width(), border_texels_);
  num_tiles_y_ = ComputeNumTiles(
      max_texture_size_.height(), total_size_.height(), border_texels_);
}

TilingData::BaseIterator::BaseIterator(const TilingData* tiling_data)
    : tiling_data_(tiling_data),
      index_x_(-1),
      index_y_(-1) {
}

TilingData::Iterator::Iterator(const TilingData* tiling_data, gfx::Rect rect)
    : BaseIterator(tiling_data),
      left_(-1),
      right_(-1),
      bottom_(-1) {
  if (tiling_data_->num_tiles_x() <= 0 || tiling_data_->num_tiles_y() <= 0) {
    done();
    return;
  }

  rect.Intersect(gfx::Rect(tiling_data_->total_size()));
  index_x_ = tiling_data_->FirstBorderTileXIndexFromSrcCoord(rect.x());
  index_y_ = tiling_data_->FirstBorderTileYIndexFromSrcCoord(rect.y());
  left_ = index_x_;
  right_ = tiling_data_->LastBorderTileXIndexFromSrcCoord(rect.right() - 1);
  bottom_ = tiling_data_->LastBorderTileYIndexFromSrcCoord(rect.bottom() - 1);

  // Index functions always return valid indices, so explicitly check
  // for non-intersecting rects.
  gfx::Rect new_rect = tiling_data_->TileBoundsWithBorder(index_x_, index_y_);
  if (!new_rect.Intersects(rect))
    done();
}

TilingData::Iterator& TilingData::Iterator::operator++() {
  if (!*this)
    return *this;

  index_x_++;
  if (index_x_ > right_) {
    index_x_ = left_;
    index_y_++;
    if (index_y_ > bottom_)
      done();
  }

  return *this;
}

TilingData::DifferenceIterator::DifferenceIterator(
    const TilingData* tiling_data,
    gfx::Rect consider,
    gfx::Rect ignore)
    : BaseIterator(tiling_data),
      consider_left_(-1),
      consider_top_(-1),
      consider_right_(-1),
      consider_bottom_(-1),
      ignore_left_(-1),
      ignore_top_(-1),
      ignore_right_(-1),
      ignore_bottom_(-1) {
  if (tiling_data_->num_tiles_x() <= 0 || tiling_data_->num_tiles_y() <= 0) {
    done();
    return;
  }

  gfx::Rect bounds(tiling_data_->total_size());
  consider.Intersect(bounds);
  ignore.Intersect(bounds);
  if (consider.IsEmpty()) {
    done();
    return;
  }

  consider_left_ =
      tiling_data_->FirstBorderTileXIndexFromSrcCoord(consider.x());
  consider_top_ =
      tiling_data_->FirstBorderTileYIndexFromSrcCoord(consider.y());
  consider_right_ =
      tiling_data_->LastBorderTileXIndexFromSrcCoord(consider.right() - 1);
  consider_bottom_ =
      tiling_data_->LastBorderTileYIndexFromSrcCoord(consider.bottom() - 1);

  if (!ignore.IsEmpty()) {
    ignore_left_ =
        tiling_data_->FirstBorderTileXIndexFromSrcCoord(ignore.x());
    ignore_top_ =
        tiling_data_->FirstBorderTileYIndexFromSrcCoord(ignore.y());
    ignore_right_ =
        tiling_data_->LastBorderTileXIndexFromSrcCoord(ignore.right() - 1);
    ignore_bottom_ =
        tiling_data_->LastBorderTileYIndexFromSrcCoord(ignore.bottom() - 1);

    // Clamp ignore indices to consider indices.
    ignore_left_ = std::max(ignore_left_, consider_left_);
    ignore_top_ = std::max(ignore_top_, consider_top_);
    ignore_right_ = std::min(ignore_right_, consider_right_);
    ignore_bottom_ = std::min(ignore_bottom_, consider_bottom_);
  }

  if (ignore_left_ == consider_left_ && ignore_right_ == consider_right_ &&
      ignore_top_ == consider_top_ && ignore_bottom_ == consider_bottom_) {
    done();
    return;
  }

  index_x_ = consider_left_;
  index_y_ = consider_top_;

  if (in_ignore_rect())
    ++(*this);
}

TilingData::DifferenceIterator& TilingData::DifferenceIterator::operator++() {
  if (!*this)
    return *this;

  index_x_++;
  if (in_ignore_rect())
    index_x_ = ignore_right_ + 1;

  if (index_x_ > consider_right_) {
    index_x_ = consider_left_;
    index_y_++;

    if (in_ignore_rect()) {
      index_x_ = ignore_right_ + 1;
      // If the ignore rect spans the whole consider rect horizontally, then
      // ignore_right + 1 will be out of bounds.
      if (in_ignore_rect() || index_x_ > consider_right_) {
        index_y_ = ignore_bottom_ + 1;
        index_x_ = consider_left_;
      }
    }

    if (index_y_ > consider_bottom_)
      done();
  }

  return *this;
}

}  // namespace cc
