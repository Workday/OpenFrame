// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/playback/discardable_image_map.h"

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/values.h"
#include "cc/base/region.h"
#include "cc/test/fake_content_layer_client.h"
#include "cc/test/fake_display_list_recording_source.h"
#include "cc/test/skia_common.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkGraphics.h"
#include "third_party/skia/include/core/SkImageGenerator.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/skia_util.h"

namespace cc {
namespace {

struct PositionDrawImage {
  PositionDrawImage(const SkImage* image, const gfx::RectF& image_rect)
      : image(image), image_rect(image_rect) {}
  const SkImage* image;
  gfx::RectF image_rect;
};

}  // namespace

class DiscardableImageMapTest : public testing::Test {
 public:
  std::vector<PositionDrawImage> GetDiscardableImagesInRect(
      const DiscardableImageMap& image_map,
      const gfx::Rect& rect) {
    std::vector<DrawImage> draw_images;
    image_map.GetDiscardableImagesInRect(rect, 1.f, &draw_images);

    std::vector<size_t> indices;
    image_map.images_rtree_.Search(gfx::RectF(rect), &indices);
    std::vector<PositionDrawImage> position_draw_images;
    for (size_t index : indices) {
      position_draw_images.push_back(
          PositionDrawImage(image_map.all_images_[index].first.image(),
                            image_map.all_images_[index].second));
    }

    EXPECT_EQ(draw_images.size(), position_draw_images.size());
    for (size_t i = 0; i < draw_images.size(); ++i)
      EXPECT_TRUE(draw_images[i].image() == position_draw_images[i].image);
    return position_draw_images;
  }
};

TEST_F(DiscardableImageMapTest, GetDiscardableImagesInRectTest) {
  gfx::Rect visible_rect(2048, 2048);
  FakeContentLayerClient content_layer_client;
  content_layer_client.set_bounds(visible_rect.size());

  // Discardable pixel refs are found in the following grids:
  // |---|---|---|---|
  // |   | x |   | x |
  // |---|---|---|---|
  // | x |   | x |   |
  // |---|---|---|---|
  // |   | x |   | x |
  // |---|---|---|---|
  // | x |   | x |   |
  // |---|---|---|---|
  skia::RefPtr<SkImage> discardable_image[4][4];
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      if ((x + y) & 1) {
        discardable_image[y][x] = CreateDiscardableImage(gfx::Size(500, 500));
        SkPaint paint;
        content_layer_client.add_draw_image(
            discardable_image[y][x].get(), gfx::Point(x * 512 + 6, y * 512 + 6),
            paint);
      }
    }
  }

  FakeDisplayListRecordingSource recording_source;
  Region invalidation(visible_rect);
  recording_source.SetGenerateDiscardableImagesMetadata(true);
  recording_source.UpdateAndExpandInvalidation(
      &content_layer_client, &invalidation, visible_rect.size(), visible_rect,
      1, DisplayListRecordingSource::RECORD_NORMALLY);
  DisplayItemList* display_list = recording_source.display_list();

  DiscardableImageMap image_map;
  {
    DiscardableImageMap::ScopedMetadataGenerator generator(&image_map,
                                                           visible_rect.size());
    display_list->Raster(generator.canvas(), nullptr, visible_rect, 1.f);
  }

  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      std::vector<PositionDrawImage> images = GetDiscardableImagesInRect(
          image_map, gfx::Rect(x * 512, y * 512, 500, 500));
      if ((x + y) & 1) {
        EXPECT_EQ(1u, images.size()) << x << " " << y;
        EXPECT_TRUE(images[0].image == discardable_image[y][x].get())
            << x << " " << y;
        EXPECT_EQ(gfx::RectF(x * 512 + 6, y * 512 + 6, 500, 500),
                  images[0].image_rect);
      } else {
        EXPECT_EQ(0u, images.size()) << x << " " << y;
      }
    }
  }

  // Capture 4 pixel refs.
  std::vector<PositionDrawImage> images =
      GetDiscardableImagesInRect(image_map, gfx::Rect(512, 512, 2048, 2048));
  EXPECT_EQ(4u, images.size());
  EXPECT_TRUE(images[0].image == discardable_image[1][2].get());
  EXPECT_EQ(gfx::RectF(2 * 512 + 6, 512 + 6, 500, 500), images[0].image_rect);
  EXPECT_TRUE(images[1].image == discardable_image[2][1].get());
  EXPECT_EQ(gfx::RectF(512 + 6, 2 * 512 + 6, 500, 500), images[1].image_rect);
  EXPECT_TRUE(images[2].image == discardable_image[2][3].get());
  EXPECT_EQ(gfx::RectF(3 * 512 + 6, 2 * 512 + 6, 500, 500),
            images[2].image_rect);
  EXPECT_TRUE(images[3].image == discardable_image[3][2].get());
  EXPECT_EQ(gfx::RectF(2 * 512 + 6, 3 * 512 + 6, 500, 500),
            images[3].image_rect);
}

TEST_F(DiscardableImageMapTest, GetDiscardableImagesInRectNonZeroLayer) {
  gfx::Rect visible_rect(1024, 0, 2048, 2048);
  // Make sure visible rect fits into the layer size.
  gfx::Size layer_size(visible_rect.right(), visible_rect.bottom());
  FakeContentLayerClient content_layer_client;
  content_layer_client.set_bounds(layer_size);

  // Discardable pixel refs are found in the following grids:
  // |---|---|---|---|
  // |   | x |   | x |
  // |---|---|---|---|
  // | x |   | x |   |
  // |---|---|---|---|
  // |   | x |   | x |
  // |---|---|---|---|
  // | x |   | x |   |
  // |---|---|---|---|
  skia::RefPtr<SkImage> discardable_image[4][4];
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      if ((x + y) & 1) {
        discardable_image[y][x] = CreateDiscardableImage(gfx::Size(500, 500));
        SkPaint paint;
        content_layer_client.add_draw_image(
            discardable_image[y][x].get(),
            gfx::Point(1024 + x * 512 + 6, y * 512 + 6), paint);
      }
    }
  }

  FakeDisplayListRecordingSource recording_source;
  Region invalidation(visible_rect);
  recording_source.SetGenerateDiscardableImagesMetadata(true);
  recording_source.UpdateAndExpandInvalidation(
      &content_layer_client, &invalidation, layer_size, visible_rect, 1,
      DisplayListRecordingSource::RECORD_NORMALLY);
  DisplayItemList* display_list = recording_source.display_list();

  DiscardableImageMap image_map;
  {
    DiscardableImageMap::ScopedMetadataGenerator generator(&image_map,
                                                           layer_size);
    display_list->Raster(generator.canvas(), nullptr, visible_rect, 1.f);
  }

  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      std::vector<PositionDrawImage> images = GetDiscardableImagesInRect(
          image_map, gfx::Rect(1024 + x * 512, y * 512, 500, 500));
      if ((x + y) & 1) {
        EXPECT_EQ(1u, images.size()) << x << " " << y;
        EXPECT_TRUE(images[0].image == discardable_image[y][x].get())
            << x << " " << y;
        EXPECT_EQ(gfx::RectF(1024 + x * 512 + 6, y * 512 + 6, 500, 500),
                  images[0].image_rect);
      } else {
        EXPECT_EQ(0u, images.size()) << x << " " << y;
      }
    }
  }
  // Capture 4 pixel refs.
  {
    std::vector<PositionDrawImage> images = GetDiscardableImagesInRect(
        image_map, gfx::Rect(1024 + 512, 512, 2048, 2048));
    EXPECT_EQ(4u, images.size());
    EXPECT_TRUE(images[0].image == discardable_image[1][2].get());
    EXPECT_EQ(gfx::RectF(1024 + 2 * 512 + 6, 512 + 6, 500, 500),
              images[0].image_rect);
    EXPECT_TRUE(images[1].image == discardable_image[2][1].get());
    EXPECT_EQ(gfx::RectF(1024 + 512 + 6, 2 * 512 + 6, 500, 500),
              images[1].image_rect);
    EXPECT_TRUE(images[2].image == discardable_image[2][3].get());
    EXPECT_EQ(gfx::RectF(1024 + 3 * 512 + 6, 2 * 512 + 6, 500, 500),
              images[2].image_rect);
    EXPECT_TRUE(images[3].image == discardable_image[3][2].get());
    EXPECT_EQ(gfx::RectF(1024 + 2 * 512 + 6, 3 * 512 + 6, 500, 500),
              images[3].image_rect);
  }

  // Non intersecting rects
  {
    std::vector<PositionDrawImage> images =
        GetDiscardableImagesInRect(image_map, gfx::Rect(0, 0, 1000, 1000));
    EXPECT_EQ(0u, images.size());
  }
  {
    std::vector<PositionDrawImage> images =
        GetDiscardableImagesInRect(image_map, gfx::Rect(3500, 0, 1000, 1000));
    EXPECT_EQ(0u, images.size());
  }
  {
    std::vector<PositionDrawImage> images =
        GetDiscardableImagesInRect(image_map, gfx::Rect(0, 1100, 1000, 1000));
    EXPECT_EQ(0u, images.size());
  }
  {
    std::vector<PositionDrawImage> images = GetDiscardableImagesInRect(
        image_map, gfx::Rect(3500, 1100, 1000, 1000));
    EXPECT_EQ(0u, images.size());
  }
}

TEST_F(DiscardableImageMapTest, GetDiscardableImagesInRectOnePixelQuery) {
  gfx::Rect visible_rect(2048, 2048);
  FakeContentLayerClient content_layer_client;
  content_layer_client.set_bounds(visible_rect.size());

  // Discardable pixel refs are found in the following grids:
  // |---|---|---|---|
  // |   | x |   | x |
  // |---|---|---|---|
  // | x |   | x |   |
  // |---|---|---|---|
  // |   | x |   | x |
  // |---|---|---|---|
  // | x |   | x |   |
  // |---|---|---|---|
  skia::RefPtr<SkImage> discardable_image[4][4];
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      if ((x + y) & 1) {
        discardable_image[y][x] = CreateDiscardableImage(gfx::Size(500, 500));
        SkPaint paint;
        content_layer_client.add_draw_image(
            discardable_image[y][x].get(), gfx::Point(x * 512 + 6, y * 512 + 6),
            paint);
      }
    }
  }

  FakeDisplayListRecordingSource recording_source;
  Region invalidation(visible_rect);
  recording_source.SetGenerateDiscardableImagesMetadata(true);
  recording_source.UpdateAndExpandInvalidation(
      &content_layer_client, &invalidation, visible_rect.size(), visible_rect,
      1, DisplayListRecordingSource::RECORD_NORMALLY);
  DisplayItemList* display_list = recording_source.display_list();

  DiscardableImageMap image_map;
  {
    DiscardableImageMap::ScopedMetadataGenerator generator(&image_map,
                                                           visible_rect.size());
    display_list->Raster(generator.canvas(), nullptr, visible_rect, 1.f);
  }

  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      std::vector<PositionDrawImage> images = GetDiscardableImagesInRect(
          image_map, gfx::Rect(x * 512 + 256, y * 512 + 256, 1, 1));
      if ((x + y) & 1) {
        EXPECT_EQ(1u, images.size()) << x << " " << y;
        EXPECT_TRUE(images[0].image == discardable_image[y][x].get())
            << x << " " << y;
        EXPECT_EQ(gfx::RectF(x * 512 + 6, y * 512 + 6, 500, 500),
                  images[0].image_rect);
      } else {
        EXPECT_EQ(0u, images.size()) << x << " " << y;
      }
    }
  }
}

TEST_F(DiscardableImageMapTest, GetDiscardableImagesInRectMassiveImage) {
  gfx::Rect visible_rect(2048, 2048);
  FakeContentLayerClient content_layer_client;
  content_layer_client.set_bounds(visible_rect.size());

  skia::RefPtr<SkImage> discardable_image;
  discardable_image = CreateDiscardableImage(gfx::Size(1 << 25, 1 << 25));
  SkPaint paint;
  content_layer_client.add_draw_image(discardable_image.get(), gfx::Point(0, 0),
                                      paint);

  FakeDisplayListRecordingSource recording_source;
  Region invalidation(visible_rect);
  recording_source.SetGenerateDiscardableImagesMetadata(true);
  recording_source.UpdateAndExpandInvalidation(
      &content_layer_client, &invalidation, visible_rect.size(), visible_rect,
      1, DisplayListRecordingSource::RECORD_NORMALLY);
  DisplayItemList* display_list = recording_source.display_list();

  DiscardableImageMap image_map;
  {
    DiscardableImageMap::ScopedMetadataGenerator generator(&image_map,
                                                           visible_rect.size());
    display_list->Raster(generator.canvas(), nullptr, visible_rect, 1.f);
  }
  std::vector<PositionDrawImage> images =
      GetDiscardableImagesInRect(image_map, gfx::Rect(0, 0, 1, 1));
  EXPECT_EQ(1u, images.size());
  EXPECT_TRUE(images[0].image == discardable_image.get());
  EXPECT_EQ(gfx::RectF(0, 0, 1 << 25, 1 << 25), images[0].image_rect);
}

}  // namespace cc
