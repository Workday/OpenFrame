// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_TAB_MODAL_CONFIRM_DIALOG_DELEGATE_H_
#define CHROME_BROWSER_UI_TAB_MODAL_CONFIRM_DIALOG_DELEGATE_H_

#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/strings/string16.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "ui/base/window_open_disposition.h"

namespace content {
class WebContents;
}

namespace gfx {
class Image;
}

class TabModalConfirmDialogCloseDelegate {
 public:
  TabModalConfirmDialogCloseDelegate() {}
  virtual ~TabModalConfirmDialogCloseDelegate() {}

  virtual void CloseDialog() = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(TabModalConfirmDialogCloseDelegate);
};

// This class acts as the delegate for a simple tab-modal dialog confirming
// whether the user wants to execute a certain action.
class TabModalConfirmDialogDelegate : public content::NotificationObserver {
 public:
  explicit TabModalConfirmDialogDelegate(content::WebContents* web_contents);
  virtual ~TabModalConfirmDialogDelegate();

  void set_close_delegate(TabModalConfirmDialogCloseDelegate* close_delegate) {
    close_delegate_ = close_delegate;
  }

  // Accepts the confirmation prompt and calls |OnAccepted| if no other call
  // to |Accept|, |Cancel|, |LinkClicked| or |Close| has been made before.
  // This method is safe to call even from an |OnAccepted| or |OnCanceled|
  // callback.
  void Accept();

  // Cancels the confirmation prompt and calls |OnCanceled| if no other call
  // to |Accept|, |Cancel|, |LinkClicked| or |Close| has been made before.
  // This method is safe to call even from an |OnAccepted| or |OnCanceled|
  // callback.
  void Cancel();

  // Called when the link (if any) is clicked. Calls |OnLinkClicked| and closes
  // the dialog if no other call to |Accept|, |Cancel|, |LinkClicked| or
  // |Close| has been made before. The |disposition| specifies how the
  // resulting document should be loaded (based on the event flags present when
  // the link was clicked).
  void LinkClicked(WindowOpenDisposition disposition);

  // Called when the dialog is closed without selecting an option, e.g. by
  // pressing the close button on the dialog, using a window manager gesture,
  // closing the parent tab or navigating in the parent tab.
  // Calls |OnClosed| and closes the dialog if no other call to |Accept|,
  // |Cancel|, |LinkClicked| or |Close| has been made before.
  void Close();

  // The title of the dialog. Note that the title is not shown on all platforms.
  virtual string16 GetTitle() = 0;
  virtual string16 GetMessage() = 0;

  // Icon to show for the dialog. If this method is not overridden, a default
  // icon (like the application icon) is shown.
  virtual gfx::Image* GetIcon();

  // Title for the accept and the cancel buttons.
  // The default implementation uses IDS_OK and IDS_CANCEL.
  virtual string16 GetAcceptButtonTitle();
  virtual string16 GetCancelButtonTitle();

  // Returns the text of the link to be displayed, if any. Otherwise returns
  // an empty string.
  virtual string16 GetLinkText() const;

  // GTK stock icon names for the accept and cancel buttons, respectively.
  // The icons are only used on GTK. If these methods are not overriden,
  // the buttons have no stock icons.
  virtual const char* GetAcceptButtonIcon();
  virtual const char* GetCancelButtonIcon();

 protected:
  TabModalConfirmDialogCloseDelegate* close_delegate() {
    return close_delegate_;
  }

  // content::NotificationObserver implementation.
  // Watch for a new load or a closed tab and dismiss the dialog if they occur.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  content::NotificationRegistrar registrar_;

 private:
  // It is guaranteed that exactly one of the |On...| methods is eventually
  // called. These method are private to enforce this guarantee. Access to them
  // is  controlled by |Accept|, |Cancel|, |LinkClicked| and |Close|.

  // Called when the user accepts or cancels the dialog, respectively.
  virtual void OnAccepted();
  virtual void OnCanceled();

  // Called when the user clicks on the link (if any).
  virtual void OnLinkClicked(WindowOpenDisposition disposition);

  // Called when the dialog is closed.
  virtual void OnClosed();

  // Close the dialog.
  void CloseDialog();

  TabModalConfirmDialogCloseDelegate* close_delegate_;
  // True iff we are in the process of closing, to avoid running callbacks
  // multiple times.
  bool closing_;

  DISALLOW_COPY_AND_ASSIGN(TabModalConfirmDialogDelegate);
};

#endif  // CHROME_BROWSER_UI_TAB_MODAL_CONFIRM_DIALOG_DELEGATE_H_
