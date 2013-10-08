// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_GTK_NOTIFICATIONS_BALLOON_VIEW_HOST_GTK_H_
#define CHROME_BROWSER_UI_GTK_NOTIFICATIONS_BALLOON_VIEW_HOST_GTK_H_

#include "chrome/browser/notifications/balloon_host.h"
#include "ui/gfx/native_widget_types.h"

// BalloonViewHost class is a delegate to the renderer host for the HTML
// notification.  When initialized it creates a new RenderViewHost and loads
// the contents of the toast into it.  It also handles links within the toast,
// loading them into a new tab.
class BalloonViewHost : public BalloonHost {
 public:
  explicit BalloonViewHost(Balloon* balloon);

  virtual ~BalloonViewHost();

  // Changes the size of the balloon.
  void UpdateActualSize(const gfx::Size& new_size);

  // Accessors.
  gfx::NativeView native_view() const;

 private:
  DISALLOW_COPY_AND_ASSIGN(BalloonViewHost);
};

#endif  // CHROME_BROWSER_UI_GTK_NOTIFICATIONS_BALLOON_VIEW_HOST_GTK_H_
