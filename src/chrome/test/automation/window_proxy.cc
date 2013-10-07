// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/automation/window_proxy.h"

#include <algorithm>
#include <vector>

#include "base/logging.h"
#include "chrome/common/automation_constants.h"
#include "chrome/common/automation_messages.h"
#include "chrome/test/automation/automation_proxy.h"
#include "chrome/test/automation/browser_proxy.h"
#include "chrome/test/automation/tab_proxy.h"
#include "ui/gfx/rect.h"
#include "url/gurl.h"

bool WindowProxy::GetViewBounds(int view_id, gfx::Rect* bounds,
                                bool screen_coordinates) {
  if (!is_valid())
    return false;

  if (!bounds) {
    NOTREACHED();
    return false;
  }

  bool result = false;

  if (!sender_->Send(new AutomationMsg_WindowViewBounds(
          handle_, view_id, screen_coordinates, &result, bounds))) {
    return false;
  }

  return result;
}

bool WindowProxy::SetBounds(const gfx::Rect& bounds) {
  if (!is_valid())
    return false;
  bool result = false;
  sender_->Send(new AutomationMsg_SetWindowBounds(handle_, bounds, &result));
  return result;
}
