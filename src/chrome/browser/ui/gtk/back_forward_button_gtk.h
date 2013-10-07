// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_GTK_BACK_FORWARD_BUTTON_GTK_H_
#define CHROME_BROWSER_UI_GTK_BACK_FORWARD_BUTTON_GTK_H_

#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/gtk/custom_button.h"
#include "chrome/browser/ui/gtk/menu_gtk.h"
#include "ui/base/gtk/gtk_signal.h"

class BackForwardMenuModel;
class Browser;

typedef struct _GtkWidget GtkWidget;

// When clicked, these buttons will navigate forward or backward. When
// pressed and held, they show a dropdown menu of recent web sites.
class BackForwardButtonGtk : MenuGtk::Delegate {
 public:
  BackForwardButtonGtk(Browser* browser, bool is_forward);
  virtual ~BackForwardButtonGtk();

  // MenuGtk::Delegate implementation.
  virtual void StoppedShowing() OVERRIDE;
  virtual bool AlwaysShowIconForCmd(int command_id) const OVERRIDE;

  GtkWidget* widget() { return button_->widget(); }

 private:
  // Executes the browser command.
  CHROMEGTK_CALLBACK_0(BackForwardButtonGtk, void, OnClick);

  // Starts a timer to show the dropdown menu.
  CHROMEGTK_CALLBACK_1(BackForwardButtonGtk, gboolean, OnButtonPress,
                       GdkEventButton*);

  // If there is a timer to show the dropdown menu, and the mouse has moved
  // sufficiently down the screen, cancel the timer and immediately show the
  // menu.
  CHROMEGTK_CALLBACK_1(BackForwardButtonGtk, gboolean, OnMouseMove,
                       GdkEventMotion*);

  // Shows the dropdown menu.
  void ShowBackForwardMenu(int button, guint32 event_time);

  // The menu gets reset every time it is shown.
  scoped_ptr<MenuGtk> menu_;

  scoped_ptr<CustomDrawButton> button_;

  // The browser to which we will send commands.
  Browser* browser_;

  // Whether this button is a forward button.
  bool is_forward_;

  // The dropdown menu model.
  scoped_ptr<BackForwardMenuModel> menu_model_;

  // The y position of the last mouse down event.
  int y_position_of_last_press_;

  base::WeakPtrFactory<BackForwardButtonGtk> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(BackForwardButtonGtk);
};

#endif  // CHROME_BROWSER_UI_GTK_BACK_FORWARD_BUTTON_GTK_H_
