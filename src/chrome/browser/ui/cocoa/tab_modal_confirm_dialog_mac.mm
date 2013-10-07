// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/cocoa/tab_modal_confirm_dialog_mac.h"

#include "base/mac/scoped_nsobject.h"
#include "chrome/browser/ui/browser_dialogs.h"
#import "chrome/browser/ui/cocoa/constrained_window/constrained_window_alert.h"
#import "chrome/browser/ui/cocoa/constrained_window/constrained_window_custom_sheet.h"
#import "chrome/browser/ui/cocoa/key_equivalent_constants.h"
#include "chrome/browser/ui/tab_modal_confirm_dialog_delegate.h"
#include "chrome/common/chrome_switches.h"
#include "ui/base/cocoa/cocoa_event_utils.h"
#include "ui/base/l10n/l10n_util_mac.h"
#include "ui/gfx/image/image.h"

// static
TabModalConfirmDialog* TabModalConfirmDialog::Create(
    TabModalConfirmDialogDelegate* delegate,
    content::WebContents* web_contents) {
  // Deletes itself when closed.
  return new TabModalConfirmDialogMac(delegate, web_contents);
}

@interface TabModalConfirmDialogMacBridge : NSObject {
  TabModalConfirmDialogDelegate* delegate_;  // weak
}
@end

@implementation TabModalConfirmDialogMacBridge

- (id)initWithDelegate:(TabModalConfirmDialogDelegate*)delegate {
  if ((self = [super init])) {
    delegate_ = delegate;
    DCHECK(delegate_);
  }
  return self;
}

- (void)onAcceptButton:(id)sender {
  delegate_->Accept();
}

- (void)onCancelButton:(id)sender {
  delegate_->Cancel();
}

- (void)onLinkClicked:(id)sender {
  WindowOpenDisposition disposition =
      ui::WindowOpenDispositionFromNSEvent([NSApp currentEvent]);
  delegate_->LinkClicked(disposition);
}

@end

TabModalConfirmDialogMac::TabModalConfirmDialogMac(
    TabModalConfirmDialogDelegate* delegate,
    content::WebContents* web_contents)
    : closing_(false),
      delegate_(delegate) {
  bridge_.reset([[TabModalConfirmDialogMacBridge alloc]
      initWithDelegate:delegate]);

  alert_.reset([[ConstrainedWindowAlert alloc] init]);
  [alert_ setMessageText:
      l10n_util::FixUpWindowsStyleLabel(delegate->GetTitle())];
  [alert_ setLinkText:l10n_util::FixUpWindowsStyleLabel(
                          delegate->GetLinkText())
               target:bridge_
               action:@selector(onLinkClicked:)];
  [alert_ setInformativeText:
      l10n_util::FixUpWindowsStyleLabel(delegate->GetMessage())];
  [alert_ addButtonWithTitle:
      l10n_util::FixUpWindowsStyleLabel(delegate->GetAcceptButtonTitle())
              keyEquivalent:kKeyEquivalentReturn
                     target:bridge_
                     action:@selector(onAcceptButton:)];
  [alert_ addButtonWithTitle:
      l10n_util::FixUpWindowsStyleLabel(delegate->GetCancelButtonTitle())
              keyEquivalent:kKeyEquivalentEscape
                     target:bridge_
                     action:@selector(onCancelButton:)];
  [[alert_ closeButton] setTarget:bridge_];
  [[alert_ closeButton] setAction:@selector(onCancelButton:)];
  [alert_ layout];

  base::scoped_nsobject<CustomConstrainedWindowSheet> sheet(
      [[CustomConstrainedWindowSheet alloc]
          initWithCustomWindow:[alert_ window]]);
  window_.reset(new ConstrainedWindowMac(this, web_contents, sheet));
  delegate_->set_close_delegate(this);
}

TabModalConfirmDialogMac::~TabModalConfirmDialogMac() {
}

void TabModalConfirmDialogMac::AcceptTabModalDialog() {
  delegate_->Accept();
}

void TabModalConfirmDialogMac::CancelTabModalDialog() {
  delegate_->Cancel();
}

void TabModalConfirmDialogMac::CloseDialog() {
  if (!closing_) {
    closing_ = true;
    window_->CloseWebContentsModalDialog();
  }
}

void TabModalConfirmDialogMac::OnConstrainedWindowClosed(
    ConstrainedWindowMac* window) {
  // Provide a disposition in case the dialog was closed without accepting or
  // cancelling.
  delegate_->Close();
  delete this;
}
