// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/support/web_gesture_curve_mock.h"

#include "third_party/WebKit/public/platform/WebFloatSize.h"
#include "third_party/WebKit/public/platform/WebGestureCurveTarget.h"
#include "webkit/support/weburl_loader_mock_factory.h"

WebGestureCurveMock::WebGestureCurveMock(const WebKit::WebFloatPoint& velocity,
    const WebKit::WebSize& cumulative_scroll)
    : velocity_(velocity),
      cumulative_scroll_(cumulative_scroll) {
}

WebGestureCurveMock::~WebGestureCurveMock() {
}

bool WebGestureCurveMock::apply(double time,
                                WebKit::WebGestureCurveTarget* target) {
  WebKit::WebSize displacement(velocity_.x * time, velocity_.y * time);
  WebKit::WebFloatSize increment(displacement.width - cumulative_scroll_.width,
      displacement.height - cumulative_scroll_.height);
  cumulative_scroll_ = displacement;
  target->notifyCurrentFlingVelocity(WebKit::WebFloatSize(velocity_.x,
                                                          velocity_.y));
  // scrollBy() could delete this curve if the animation is over, so don't
  // touch any member variables after making that call.
  target->scrollBy(increment);
  return true;
}
