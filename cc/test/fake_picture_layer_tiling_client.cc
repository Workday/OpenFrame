// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/test/fake_picture_layer_tiling_client.h"

#include <limits>

#include "cc/test/fake_tile_manager.h"

namespace cc {

class FakeInfinitePicturePileImpl : public PicturePileImpl {
 public:
  FakeInfinitePicturePileImpl() {
    gfx::Size size(std::numeric_limits<int>::max(),
                   std::numeric_limits<int>::max());
    Resize(size);
    recorded_region_ = Region(gfx::Rect(size));
  }

 protected:
  virtual ~FakeInfinitePicturePileImpl() {}
};

FakePictureLayerTilingClient::FakePictureLayerTilingClient()
    : tile_manager_(new FakeTileManager(&tile_manager_client_)),
      pile_(new FakeInfinitePicturePileImpl()),
      twin_tiling_(NULL),
      allow_create_tile_(true) {}

FakePictureLayerTilingClient::~FakePictureLayerTilingClient() {
}

scoped_refptr<Tile> FakePictureLayerTilingClient::CreateTile(
    PictureLayerTiling*,
    gfx::Rect rect) {
  if (!allow_create_tile_)
    return NULL;
  return new Tile(tile_manager_.get(),
                  pile_.get(),
                  tile_size_,
                  rect,
                  gfx::Rect(),
                  1,
                  0,
                  0,
                  true);
}

void FakePictureLayerTilingClient::SetTileSize(gfx::Size tile_size) {
  tile_size_ = tile_size;
}

gfx::Size FakePictureLayerTilingClient::CalculateTileSize(
    gfx::Size /* content_bounds */) const {
  return tile_size_;
}

const Region* FakePictureLayerTilingClient::GetInvalidation() {
  return &invalidation_;
}

const PictureLayerTiling* FakePictureLayerTilingClient::GetTwinTiling(
      const PictureLayerTiling* tiling) {
  return twin_tiling_;
}

}  // namespace cc
