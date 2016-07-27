// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blimp/net/input_message_generator.h"

#include "base/logging.h"
#include "blimp/common/proto/blimp_message.pb.h"
#include "blimp/common/proto/input.pb.h"
#include "blimp/net/blimp_message_processor.h"
#include "third_party/WebKit/public/platform/WebGestureDevice.h"
#include "third_party/WebKit/public/web/WebInputEvent.h"

namespace blimp {
namespace {

void CommonWebGestureToProto(const blink::WebGestureEvent& event,
                             InputMessage::Type type,
                             InputMessage* proto) {
  proto->set_type(type);
  proto->set_timestamp_seconds(event.timeStampSeconds);

  GestureCommon* common = proto->mutable_gesture_common();
  common->set_x(event.x);
  common->set_y(event.y);
  common->set_global_x(event.globalX);
  common->set_global_y(event.globalY);
}

void GestureScrollBeginToProto(const blink::WebGestureEvent& event,
                               InputMessage* proto) {
  CommonWebGestureToProto(event, InputMessage::Type_GestureScrollBegin, proto);

  GestureScrollBegin* details = proto->mutable_gesture_scroll_begin();
  details->set_delta_x_hint(event.data.scrollBegin.deltaXHint);
  details->set_delta_y_hint(event.data.scrollBegin.deltaYHint);
  details->set_target_viewport(event.data.scrollBegin.targetViewport);
}

void GestureScrollEndToProto(const blink::WebGestureEvent& event,
                             InputMessage* proto) {
  CommonWebGestureToProto(event, InputMessage::Type_GestureScrollEnd, proto);
}

void GestureScrollUpdateToProto(const blink::WebGestureEvent& event,
                                InputMessage* proto) {
  CommonWebGestureToProto(event, InputMessage::Type_GestureScrollUpdate, proto);

  GestureScrollUpdate* details = proto->mutable_gesture_scroll_update();
  details->set_delta_x(event.data.scrollUpdate.deltaX);
  details->set_delta_y(event.data.scrollUpdate.deltaY);
  details->set_velocity_x(event.data.scrollUpdate.velocityX);
  details->set_velocity_y(event.data.scrollUpdate.velocityY);
  details->set_previous_update_in_sequence_prevented(
      event.data.scrollUpdate.previousUpdateInSequencePrevented);
  details->set_prevent_propagation(
      event.data.scrollUpdate.preventPropagation);
  details->set_inertial(event.data.scrollUpdate.inertial);
}

void GestureFlingStartToProto(const blink::WebGestureEvent& event,
                              InputMessage* proto) {
  CommonWebGestureToProto(event, InputMessage::Type_GestureFlingStart, proto);

  GestureFlingStart* details = proto->mutable_gesture_fling_start();
  details->set_velocity_x(event.data.flingStart.velocityX);
  details->set_velocity_y(event.data.flingStart.velocityY);
  details->set_target_viewport(event.data.flingStart.targetViewport);
}

void GestureFlingCancelToProto(const blink::WebGestureEvent& event,
                               InputMessage* proto) {
  CommonWebGestureToProto(event, InputMessage::Type_GestureFlingCancel, proto);

  GestureFlingCancel* details = proto->mutable_gesture_fling_cancel();
  details->set_prevent_boosting(event.data.flingCancel.preventBoosting);
}

void GestureTapToProto(const blink::WebGestureEvent& event,
                       InputMessage* proto) {
  CommonWebGestureToProto(event, InputMessage::Type_GestureTap, proto);

  GestureTap* details = proto->mutable_gesture_tap();
  details->set_tap_count(event.data.tap.tapCount);
  details->set_width(event.data.tap.width);
  details->set_height(event.data.tap.height);
}

void GesturePinchBeginToProto(const blink::WebGestureEvent& event,
                              InputMessage* proto) {
  CommonWebGestureToProto(event, InputMessage::Type_GesturePinchBegin, proto);
}

void GesturePinchEndToProto(const blink::WebGestureEvent& event,
                            InputMessage* proto) {
  CommonWebGestureToProto(event, InputMessage::Type_GesturePinchEnd, proto);
}

void GesturePinchUpdateToProto(const blink::WebGestureEvent& event,
                               InputMessage* proto) {
  CommonWebGestureToProto(event, InputMessage::Type_GesturePinchUpdate, proto);

  GesturePinchUpdate* details = proto->mutable_gesture_pinch_update();
  details->set_zoom_disabled(event.data.pinchUpdate.zoomDisabled);
  details->set_scale(event.data.pinchUpdate.scale);
}

}  // namespace

InputMessageGenerator::InputMessageGenerator() {}

InputMessageGenerator::~InputMessageGenerator() {}

scoped_ptr<BlimpMessage> InputMessageGenerator::GenerateMessage(
    const blink::WebInputEvent& event) {
  scoped_ptr<BlimpMessage> message(new BlimpMessage);
  message->set_type(BlimpMessage::INPUT);
  InputMessage* details = message->mutable_input();

  switch (event.type) {
    case blink::WebInputEvent::Type::GestureScrollBegin:
      GestureScrollBeginToProto(
          static_cast<const blink::WebGestureEvent&>(event),
          details);
      break;
    case blink::WebInputEvent::Type::GestureScrollEnd:
      GestureScrollEndToProto(static_cast<const blink::WebGestureEvent&>(event),
                              details);
      break;
    case blink::WebInputEvent::Type::GestureScrollUpdate:
      GestureScrollUpdateToProto(
          static_cast<const blink::WebGestureEvent&>(event),
          details);
      break;
    case blink::WebInputEvent::Type::GestureFlingStart:
      GestureFlingStartToProto(
          static_cast<const blink::WebGestureEvent&>(event),
          details);
      break;
    case blink::WebInputEvent::Type::GestureFlingCancel:
      GestureFlingCancelToProto(
          static_cast<const blink::WebGestureEvent&>(event),
          details);
      break;
    case blink::WebInputEvent::Type::GestureTap:
      GestureTapToProto(static_cast<const blink::WebGestureEvent&>(event),
                        details);
      break;
    case blink::WebInputEvent::Type::GesturePinchBegin:
      GesturePinchBeginToProto(
          static_cast<const blink::WebGestureEvent&>(event),
          details);
      break;
    case blink::WebInputEvent::Type::GesturePinchEnd:
      GesturePinchEndToProto(static_cast<const blink::WebGestureEvent&>(event),
                             details);
      break;
    case blink::WebInputEvent::Type::GesturePinchUpdate:
      GesturePinchUpdateToProto(
          static_cast<const blink::WebGestureEvent&>(event),
          details);
      break;
    // Unsupported types:
    case blink::WebInputEvent::Type::Undefined:
    case blink::WebInputEvent::Type::MouseDown:
    case blink::WebInputEvent::Type::MouseUp:
    case blink::WebInputEvent::Type::MouseMove:
    case blink::WebInputEvent::Type::MouseEnter:
    case blink::WebInputEvent::Type::MouseLeave:
    case blink::WebInputEvent::Type::ContextMenu:
    case blink::WebInputEvent::Type::MouseWheel:
    case blink::WebInputEvent::Type::RawKeyDown:
    case blink::WebInputEvent::Type::KeyDown:
    case blink::WebInputEvent::Type::KeyUp:
    case blink::WebInputEvent::Type::Char:
    case blink::WebInputEvent::Type::GestureShowPress:
    case blink::WebInputEvent::Type::GestureTapUnconfirmed:
    case blink::WebInputEvent::Type::GestureTapDown:
    case blink::WebInputEvent::Type::GestureTapCancel:
    case blink::WebInputEvent::Type::GestureDoubleTap:
    case blink::WebInputEvent::Type::GestureTwoFingerTap:
    case blink::WebInputEvent::Type::GestureLongPress:
    case blink::WebInputEvent::Type::GestureLongTap:
    case blink::WebInputEvent::Type::TouchStart:
    case blink::WebInputEvent::Type::TouchMove:
    case blink::WebInputEvent::Type::TouchEnd:
    case blink::WebInputEvent::Type::TouchCancel:
      NOTIMPLEMENTED();
      return nullptr;
  }

  return message;
}

}  // namespace blimp
