// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/playback/compositing_display_item.h"

#include "base/strings/stringprintf.h"
#include "base/trace_event/trace_event_argument.h"
#include "cc/proto/display_item.pb.h"
#include "cc/proto/gfx_conversions.h"
#include "cc/proto/skia_conversions.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkData.h"
#include "third_party/skia/include/core/SkFlattenable.h"
#include "third_party/skia/include/core/SkFlattenableSerialization.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "third_party/skia/include/core/SkXfermode.h"
#include "ui/gfx/skia_util.h"

namespace cc {

CompositingDisplayItem::CompositingDisplayItem() {
}

CompositingDisplayItem::~CompositingDisplayItem() {
}

void CompositingDisplayItem::SetNew(uint8_t alpha,
                                    SkXfermode::Mode xfermode,
                                    SkRect* bounds,
                                    skia::RefPtr<SkColorFilter> cf) {
  alpha_ = alpha;
  xfermode_ = xfermode;
  has_bounds_ = !!bounds;
  if (bounds)
    bounds_ = SkRect(*bounds);
  color_filter_ = cf;

  // TODO(pdr): Include color_filter's memory here.
  size_t external_memory_usage = 0;
  DisplayItem::SetNew(true /* suitable_for_gpu_raster */, 1 /* op_count */,
                      external_memory_usage);
}

void CompositingDisplayItem::ToProtobuf(proto::DisplayItem* proto) const {
  proto->set_type(proto::DisplayItem::Type_Compositing);

  proto::CompositingDisplayItem* details = proto->mutable_compositing_item();
  details->set_alpha(static_cast<uint32_t>(alpha_));
  details->set_mode(SkXfermodeModeToProto(xfermode_));
  if (has_bounds_)
    RectFToProto(gfx::SkRectToRectF(bounds_), details->mutable_bounds());

  if (color_filter_) {
    skia::RefPtr<SkData> data =
        skia::AdoptRef(SkValidatingSerializeFlattenable(color_filter_.get()));
    if (data->size() > 0)
      details->set_color_filter(data->data(), data->size());
  }
}

void CompositingDisplayItem::FromProtobuf(const proto::DisplayItem& proto) {
  DCHECK_EQ(proto::DisplayItem::Type_Compositing, proto.type());

  const proto::CompositingDisplayItem& details = proto.compositing_item();
  uint8_t alpha = static_cast<uint8_t>(details.alpha());
  SkXfermode::Mode xfermode = SkXfermodeModeFromProto(details.mode());
  scoped_ptr<SkRect> bounds;
  if (details.has_bounds()) {
    bounds.reset(
        new SkRect(gfx::RectFToSkRect(ProtoToRectF(details.bounds()))));
  }

  skia::RefPtr<SkColorFilter> filter;
  if (details.has_color_filter()) {
    SkFlattenable* flattenable = SkValidatingDeserializeFlattenable(
        details.color_filter().c_str(), details.color_filter().size(),
        SkColorFilter::GetFlattenableType());
    filter = skia::AdoptRef(static_cast<SkColorFilter*>(flattenable));
  }

  SetNew(alpha, xfermode, bounds.get(), std::move(filter));
}

void CompositingDisplayItem::Raster(
    SkCanvas* canvas,
    const gfx::Rect& canvas_target_playback_rect,
    SkPicture::AbortCallback* callback) const {
  SkPaint paint;
  paint.setXfermodeMode(xfermode_);
  paint.setAlpha(alpha_);
  paint.setColorFilter(color_filter_.get());
  canvas->saveLayer(has_bounds_ ? &bounds_ : nullptr, &paint);
}

void CompositingDisplayItem::AsValueInto(
    const gfx::Rect& visual_rect,
    base::trace_event::TracedValue* array) const {
  array->AppendString(base::StringPrintf(
      "CompositingDisplayItem alpha: %d, xfermode: %d, visualRect: [%s]",
      alpha_, xfermode_, visual_rect.ToString().c_str()));
  if (has_bounds_)
    array->AppendString(base::StringPrintf(
        ", bounds: [%f, %f, %f, %f]", static_cast<float>(bounds_.x()),
        static_cast<float>(bounds_.y()), static_cast<float>(bounds_.width()),
        static_cast<float>(bounds_.height())));
}

EndCompositingDisplayItem::EndCompositingDisplayItem() {
  DisplayItem::SetNew(true /* suitable_for_gpu_raster */, 0 /* op_count */,
                      0 /* external_memory_usage */);
}

EndCompositingDisplayItem::~EndCompositingDisplayItem() {
}

void EndCompositingDisplayItem::ToProtobuf(proto::DisplayItem* proto) const {
  proto->set_type(proto::DisplayItem::Type_EndCompositing);
}

void EndCompositingDisplayItem::FromProtobuf(const proto::DisplayItem& proto) {
  DCHECK_EQ(proto::DisplayItem::Type_EndCompositing, proto.type());
}

void EndCompositingDisplayItem::Raster(
    SkCanvas* canvas,
    const gfx::Rect& canvas_target_playback_rect,
    SkPicture::AbortCallback* callback) const {
  canvas->restore();
}

void EndCompositingDisplayItem::AsValueInto(
    const gfx::Rect& visual_rect,
    base::trace_event::TracedValue* array) const {
  array->AppendString(
      base::StringPrintf("EndCompositingDisplayItem visualRect: [%s]",
                         visual_rect.ToString().c_str()));
}

}  // namespace cc
