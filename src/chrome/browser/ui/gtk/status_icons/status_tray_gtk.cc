// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/gtk/status_icons/status_tray_gtk.h"

#include "chrome/browser/ui/gtk/status_icons/status_icon_gtk.h"

StatusTrayGtk::StatusTrayGtk() {
}

StatusTrayGtk::~StatusTrayGtk() {
}

StatusIcon* StatusTrayGtk::CreatePlatformStatusIcon(StatusIconType type,
                                                    const gfx::ImageSkia& image,
                                                    const string16& tool_tip) {
  StatusIcon* icon = new StatusIconGtk();
  icon->SetImage(image);
  icon->SetToolTip(tool_tip);
  return icon;
}

StatusTray* StatusTray::Create() {
  return new StatusTrayGtk();
}
