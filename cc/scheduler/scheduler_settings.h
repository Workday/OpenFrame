// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_SCHEDULER_SCHEDULER_SETTINGS_H_
#define CC_SCHEDULER_SCHEDULER_SETTINGS_H_

#include "cc/base/cc_export.h"

namespace cc {

class CC_EXPORT SchedulerSettings {
 public:
  SchedulerSettings();
  ~SchedulerSettings();

  bool impl_side_painting;
  bool timeout_and_draw_when_animation_checkerboards;
  bool using_synchronous_renderer_compositor;
  bool throttle_frame_production;
};

}  // namespace cc

#endif  // CC_SCHEDULER_SCHEDULER_SETTINGS_H_
