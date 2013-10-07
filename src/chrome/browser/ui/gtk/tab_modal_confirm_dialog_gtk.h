// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_GTK_TAB_MODAL_CONFIRM_DIALOG_GTK_H_
#define CHROME_BROWSER_UI_GTK_TAB_MODAL_CONFIRM_DIALOG_GTK_H_

#include <gtk/gtk.h>

#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/ui/tab_modal_confirm_dialog.h"
#include "chrome/browser/ui/gtk/constrained_window_gtk.h"
#include "ui/base/gtk/gtk_signal.h"

class TabModalConfirmDialogDelegate;

namespace content {
class WebContents;
}

// Displays a tab-modal dialog, i.e. a dialog that will block the current page
// but still allow the user to switch to a different page.
// To display the dialog, allocate this object on the heap. It will open the
// dialog from its constructor and then delete itself when the user dismisses
// the dialog.
class TabModalConfirmDialogGtk : public TabModalConfirmDialog {
 public:
  TabModalConfirmDialogGtk(TabModalConfirmDialogDelegate* delegate,
                           content::WebContents* web_contents);

 private:
  friend class TabModalConfirmDialogTest;

  virtual ~TabModalConfirmDialogGtk();

  // TabModalConfirmDialog:
  virtual void AcceptTabModalDialog() OVERRIDE;
  virtual void CancelTabModalDialog() OVERRIDE;

  // TabModalConfirmDialogCloseDelegate:
  virtual void CloseDialog() OVERRIDE;

  // Callbacks:
  CHROMEGTK_CALLBACK_0(TabModalConfirmDialogGtk, void, OnAccept);
  CHROMEGTK_CALLBACK_0(TabModalConfirmDialogGtk, void, OnCancel);
  CHROMEGTK_CALLBACK_0(TabModalConfirmDialogGtk, void, OnDestroy);
  CHROMEGTK_CALLBACK_0(TabModalConfirmDialogGtk, void, OnLinkClicked);

  scoped_ptr<TabModalConfirmDialogDelegate> delegate_;

  GtkWidget* dialog_;
  GtkWidget* ok_;
  GtkWidget* cancel_;

  GtkWidget* window_;

  bool closing_;

  DISALLOW_COPY_AND_ASSIGN(TabModalConfirmDialogGtk);
};

#endif  // CHROME_BROWSER_UI_GTK_TAB_MODAL_CONFIRM_DIALOG_GTK_H_
