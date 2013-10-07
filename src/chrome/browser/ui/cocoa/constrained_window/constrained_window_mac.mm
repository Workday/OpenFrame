// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/cocoa/constrained_window/constrained_window_mac.h"

#include "base/logging.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_window.h"
#import "chrome/browser/ui/cocoa/constrained_window/constrained_window_sheet.h"
#import "chrome/browser/ui/cocoa/constrained_window/constrained_window_sheet_controller.h"
#import "chrome/browser/ui/cocoa/tabs/tab_strip_controller.h"
#include "components/web_modal/web_contents_modal_dialog_manager.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"

using web_modal::WebContentsModalDialogManager;
using web_modal::NativeWebContentsModalDialog;

ConstrainedWindowMac::ConstrainedWindowMac(
    ConstrainedWindowMacDelegate* delegate,
    content::WebContents* web_contents,
    id<ConstrainedWindowSheet> sheet)
    : delegate_(delegate),
      web_contents_(web_contents),
      sheet_([sheet retain]),
      shown_(false) {
  DCHECK(web_contents);
  DCHECK(sheet_.get());
  WebContentsModalDialogManager* web_contents_modal_dialog_manager =
      WebContentsModalDialogManager::FromWebContents(web_contents);
  web_contents_modal_dialog_manager->ShowDialog(this);
}

ConstrainedWindowMac::~ConstrainedWindowMac() {
  CHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
}

void ConstrainedWindowMac::ShowWebContentsModalDialog() {
  if (shown_)
    return;

  NSWindow* parent_window = GetParentWindow();
  NSView* parent_view = GetSheetParentViewForWebContents(web_contents_);
  if (!parent_window || !parent_view)
    return;

  shown_ = true;
  ConstrainedWindowSheetController* controller =
      [ConstrainedWindowSheetController
          controllerForParentWindow:parent_window];
  [controller showSheet:sheet_ forParentView:parent_view];
}

void ConstrainedWindowMac::CloseWebContentsModalDialog() {
  [[ConstrainedWindowSheetController controllerForSheet:sheet_]
      closeSheet:sheet_];
  WebContentsModalDialogManager* web_contents_modal_dialog_manager =
      WebContentsModalDialogManager::FromWebContents(web_contents_);
  web_contents_modal_dialog_manager->WillClose(this);
  if (delegate_)
    delegate_->OnConstrainedWindowClosed(this);
}

void ConstrainedWindowMac::FocusWebContentsModalDialog() {
}

void ConstrainedWindowMac::PulseWebContentsModalDialog() {
  [[ConstrainedWindowSheetController controllerForSheet:sheet_]
      pulseSheet:sheet_];
}

NativeWebContentsModalDialog ConstrainedWindowMac::GetNativeDialog() {
  // TODO(wittman): Ultimately this should be changed to the
  // ConstrainedWindowSheet pointer, in conjunction with the corresponding
  // changes to NativeWebContentsModalDialogManagerCocoa.
  return this;
}

NSWindow* ConstrainedWindowMac::GetParentWindow() const {
  // Tab contents in a tabbed browser may not be inside a window. For this
  // reason use a browser window if possible.
  Browser* browser = chrome::FindBrowserWithWebContents(web_contents_);
  if (browser)
    return browser->window()->GetNativeWindow();

  return web_contents_->GetView()->GetTopLevelNativeWindow();
}
