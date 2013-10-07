// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_STATUS_ICONS_STATUS_ICON_WIN_H_
#define CHROME_BROWSER_UI_VIEWS_STATUS_ICONS_STATUS_ICON_WIN_H_

#include <windows.h>
#include <shellapi.h>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "base/win/scoped_gdi_object.h"
#include "chrome/browser/status_icons/status_icon.h"

namespace gfx {
class Point;
}

namespace views {
class MenuRunner;
}

class StatusIconWin : public StatusIcon {
 public:
  // Constructor which provides this icon's unique ID and messaging window.
  StatusIconWin(UINT id, HWND window, UINT message);
  virtual ~StatusIconWin();

  // Handles a click event from the user - if |left_button_click| is true and
  // there is a registered observer, passes the click event to the observer,
  // otherwise displays the context menu if there is one.
  void HandleClickEvent(const gfx::Point& cursor_pos, bool left_button_click);

  // Handles a click on the balloon from the user.
  void HandleBalloonClickEvent();

  // Re-creates the status tray icon now after the taskbar has been created.
  void ResetIcon();

  UINT icon_id() const { return icon_id_; }
  UINT message_id() const { return message_id_; }

  // Overridden from StatusIcon:
  virtual void SetImage(const gfx::ImageSkia& image) OVERRIDE;
  virtual void SetPressedImage(const gfx::ImageSkia& image) OVERRIDE;
  virtual void SetToolTip(const string16& tool_tip) OVERRIDE;
  virtual void DisplayBalloon(const gfx::ImageSkia& icon,
                              const string16& title,
                              const string16& contents) OVERRIDE;

 protected:
  // Overridden from StatusIcon:
  virtual void UpdatePlatformContextMenu(ui::MenuModel* menu) OVERRIDE;

 private:
  void InitIconData(NOTIFYICONDATA* icon_data);

  // The unique ID corresponding to this icon.
  UINT icon_id_;

  // Window used for processing messages from this icon.
  HWND window_;

  // The message identifier used for status icon messages.
  UINT message_id_;

  // The currently-displayed icon for the window.
  base::win::ScopedHICON icon_;

  // The currently-displayed icon for the notification balloon.
  base::win::ScopedHICON balloon_icon_;

  // Not owned.
  ui::MenuModel* menu_model_;

  // Context menu associated with this icon (if any).
  scoped_ptr<views::MenuRunner> menu_runner_;

  DISALLOW_COPY_AND_ASSIGN(StatusIconWin);
};

// Implements status notifications using Windows 8 metro style notifications.
class StatusIconMetro : public StatusIcon {
 public:
  // Constructor which provides this icon's unique ID and messaging window.
  explicit StatusIconMetro(UINT id);
  virtual ~StatusIconMetro();

  // Overridden from StatusIcon:
  virtual void SetImage(const gfx::ImageSkia& image) OVERRIDE;
  virtual void SetPressedImage(const gfx::ImageSkia& image) OVERRIDE;
  virtual void SetToolTip(const string16& tool_tip) OVERRIDE;
  virtual void DisplayBalloon(const gfx::ImageSkia& icon,
                              const string16& title,
                              const string16& contents) OVERRIDE;
 protected:
  virtual void UpdatePlatformContextMenu(ui::MenuModel* menu) OVERRIDE;

 private:
  string16 tool_tip_;
  const UINT id_;

  DISALLOW_COPY_AND_ASSIGN(StatusIconMetro);
};

#endif  // CHROME_BROWSER_UI_VIEWS_STATUS_ICONS_STATUS_ICON_WIN_H_
