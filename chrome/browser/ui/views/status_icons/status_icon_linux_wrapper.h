// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_STATUS_ICONS_STATUS_ICON_LINUX_WRAPPER_H_
#define CHROME_BROWSER_UI_VIEWS_STATUS_ICONS_STATUS_ICON_LINUX_WRAPPER_H_

#include "base/memory/scoped_ptr.h"
#include "chrome/browser/status_icons/desktop_notification_balloon.h"
#include "chrome/browser/status_icons/status_icon.h"
#include "ui/linux_ui/linux_ui.h"

// Wrapper class for StatusIconLinux that implements the standard StatusIcon
// interface. Also handles callbacks from StatusIconLinux.
class StatusIconLinuxWrapper : public StatusIcon,
                               public StatusIconLinux::Delegate {
 public:
  virtual ~StatusIconLinuxWrapper();

  // StatusIcon overrides:
  virtual void SetImage(const gfx::ImageSkia& image) OVERRIDE;
  virtual void SetPressedImage(const gfx::ImageSkia& image) OVERRIDE;
  virtual void SetToolTip(const string16& tool_tip) OVERRIDE;
  virtual void DisplayBalloon(const gfx::ImageSkia& icon,
                              const string16& title,
                              const string16& contents) OVERRIDE;

  // StatusIconLinux::Delegate overrides:
  virtual void OnClick() OVERRIDE;
  virtual bool HasClickAction() OVERRIDE;

  static StatusIconLinuxWrapper* CreateWrappedStatusIcon(
      const gfx::ImageSkia& image,
      const string16& tool_tip);

 protected:
  // StatusIcon overrides:
  // Invoked after a call to SetContextMenu() to let the platform-specific
  // subclass update the native context menu based on the new model. If NULL is
  // passed, subclass should destroy the native context menu.
  virtual void UpdatePlatformContextMenu(ui::MenuModel* model) OVERRIDE;

 private:
  // A status icon wrapper should only be created by calling
  // CreateWrappedStatusIcon().
  explicit StatusIconLinuxWrapper(StatusIconLinux* status_icon);

  // Notification balloon.
  DesktopNotificationBalloon notification_;
  scoped_ptr<StatusIconLinux> status_icon_;

  DISALLOW_COPY_AND_ASSIGN(StatusIconLinuxWrapper);
};

#endif  // CHROME_BROWSER_UI_VIEWS_STATUS_ICONS_STATUS_ICON_LINUX_WRAPPER_H_
