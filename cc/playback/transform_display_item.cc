// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/playback/transform_display_item.h"

#include "base/strings/stringprintf.h"
#include "base/trace_event/trace_event_argument.h"
#include "cc/proto/display_item.pb.h"
#include "cc/proto/gfx_conversions.h"
#include "third_party/skia/include/core/SkCanvas.h"

namespace cc {

TransformDisplayItem::TransformDisplayItem()
    : transform_(gfx::Transform::kSkipInitialization) {
}

TransformDisplayItem::~TransformDisplayItem() {
}

void TransformDisplayItem::SetNew(const gfx::Transform& transform) {
  transform_ = transform;

  DisplayItem::SetNew(true /* suitable_for_gpu_raster */, 1 /* op_count */,
                      0 /* external_memory_usage */);
}

void TransformDisplayItem::ToProtobuf(proto::DisplayItem* proto) const {
  proto->set_type(proto::DisplayItem::Type_Transform);

  proto::TransformDisplayItem* details = proto->mutable_transform_item();
  TransformToProto(transform_, details->mutable_transform());
}

void TransformDisplayItem::FromProtobuf(const proto::DisplayItem& proto) {
  DCHECK_EQ(proto::DisplayItem::Type_Transform, proto.type());

  const proto::TransformDisplayItem& details = proto.transform_item();
  gfx::Transform transform = ProtoToTransform(details.transform());

  SetNew(transform);
}

void TransformDisplayItem::Raster(SkCanvas* canvas,
                                  const gfx::Rect& canvas_target_playback_rect,
                                  SkPicture::AbortCallback* callback) const {
  canvas->save();
  if (!transform_.IsIdentity())
    canvas->concat(transform_.matrix());
}

void TransformDisplayItem::AsValueInto(
    const gfx::Rect& visual_rect,
    base::trace_event::TracedValue* array) const {
  array->AppendString(base::StringPrintf(
      "TransformDisplayItem transform: [%s] visualRect: [%s]",
      transform_.ToString().c_str(), visual_rect.ToString().c_str()));
}

EndTransformDisplayItem::EndTransformDisplayItem() {
  DisplayItem::SetNew(true /* suitable_for_gpu_raster */, 0 /* op_count */,
                      0 /* external_memory_usage */);
}

EndTransformDisplayItem::~EndTransformDisplayItem() {
}

void EndTransformDisplayItem::ToProtobuf(proto::DisplayItem* proto) const {
  proto->set_type(proto::DisplayItem::Type_EndTransform);
}

void EndTransformDisplayItem::FromProtobuf(const proto::DisplayItem& proto) {
  DCHECK_EQ(proto::DisplayItem::Type_EndTransform, proto.type());
}

void EndTransformDisplayItem::Raster(
    SkCanvas* canvas,
    const gfx::Rect& canvas_target_playback_rect,
    SkPicture::AbortCallback* callback) const {
  canvas->restore();
}

void EndTransformDisplayItem::AsValueInto(
    const gfx::Rect& visual_rect,
    base::trace_event::TracedValue* array) const {
  array->AppendString(
      base::StringPrintf("EndTransformDisplayItem visualRect: [%s]",
                         visual_rect.ToString().c_str()));
}

}  // namespace cc
