// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/base/ui_base_switches_util.h"

#include "base/command_line.h"
#include "ui/base/ui_base_switches.h"

namespace switches {

bool IsLinkDisambiguationPopupEnabled() {
#if defined(OS_ANDROID)
  return true;
#else
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableLinkDisambiguationPopup)) {
    return true;
  }
  return false;
#endif
}

bool IsTouchDragDropEnabled() {
#if defined(OS_CHROMEOS)
  return !base::CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kDisableTouchDragDrop);
#else
  return base::CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kEnableTouchDragDrop);
#endif
}

bool IsTouchFeedbackEnabled() {
  static bool touch_feedback_enabled =
      !base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kDisableTouchFeedback);
  return touch_feedback_enabled;
}

const char kChromeFrame_util[] = "chrome-frame";

bool IsChromeFrame() {
	return base::CommandLine::ForCurrentProcess()->HasSwitch(kChromeFrame_util);
}

}  // namespace switches
