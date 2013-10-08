// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_GTK_STATUS_ICONS_STATUS_TRAY_GTK_H_
#define CHROME_BROWSER_UI_GTK_STATUS_ICONS_STATUS_TRAY_GTK_H_

#include "base/compiler_specific.h"
#include "chrome/browser/status_icons/status_tray.h"

class StatusTrayGtk : public StatusTray {
 public:
  StatusTrayGtk();
  virtual ~StatusTrayGtk();

 protected:
  // Overriden from StatusTray:
  virtual StatusIcon* CreatePlatformStatusIcon(
      StatusIconType type,
      const gfx::ImageSkia& image,
      const string16& tool_tip) OVERRIDE;

 private:
  DISALLOW_COPY_AND_ASSIGN(StatusTrayGtk);
};

#endif  // CHROME_BROWSER_UI_GTK_STATUS_ICONS_STATUS_TRAY_GTK_H_
