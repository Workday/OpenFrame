// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/extensions/extension_install_dialog_controller.h"

#include "base/bind.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/message_loop/message_loop.h"
#include "chrome/browser/extensions/api/experience_sampling_private/experience_sampling.h"
#include "chrome/browser/extensions/extension_install_prompt_show_params.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#import "chrome/browser/ui/cocoa/constrained_window/constrained_window_custom_sheet.h"
#include "chrome/browser/ui/cocoa/constrained_window/constrained_window_custom_window.h"
#import "chrome/browser/ui/cocoa/extensions/extension_install_view_controller.h"
#import "chrome/browser/ui/cocoa/extensions/windowed_install_dialog_controller.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"

using extensions::ExperienceSamplingEvent;

namespace {

void ShowExtensionInstallDialogImpl(
    ExtensionInstallPromptShowParams* show_params,
    ExtensionInstallPrompt::Delegate* delegate,
    scoped_refptr<ExtensionInstallPrompt::Prompt> prompt) {
  // These objects will delete themselves when the dialog closes.
  if (!show_params->GetParentWebContents()) {
    new WindowedInstallDialogController(show_params, delegate, prompt);
    return;
  }

  new ExtensionInstallDialogController(show_params, delegate, prompt);
}

}  // namespace

ExtensionInstallDialogController::ExtensionInstallDialogController(
    ExtensionInstallPromptShowParams* show_params,
    ExtensionInstallPrompt::Delegate* delegate,
    scoped_refptr<ExtensionInstallPrompt::Prompt> prompt)
    : delegate_(delegate) {
  view_controller_.reset([[ExtensionInstallViewController alloc]
      initWithProfile:show_params->profile()
            navigator:show_params->GetParentWebContents()
             delegate:this
               prompt:prompt]);

  base::scoped_nsobject<NSWindow> window([[ConstrainedWindowCustomWindow alloc]
      initWithContentRect:[[view_controller_ view] bounds]]);
  [[window contentView] addSubview:[view_controller_ view]];

  base::scoped_nsobject<CustomConstrainedWindowSheet> sheet(
      [[CustomConstrainedWindowSheet alloc] initWithCustomWindow:window]);
  constrained_window_.reset(new ConstrainedWindowMac(
      this, show_params->GetParentWebContents(), sheet));

  std::string event_name = ExperienceSamplingEvent::kExtensionInstallDialog;
  event_name.append(ExtensionInstallPrompt::PromptTypeToString(prompt->type()));
  sampling_event_ = ExperienceSamplingEvent::Create(event_name);
}

ExtensionInstallDialogController::~ExtensionInstallDialogController() {
}

void ExtensionInstallDialogController::InstallUIProceed() {
  if (sampling_event_.get())
    sampling_event_->CreateUserDecisionEvent(ExperienceSamplingEvent::kProceed);
  delegate_->InstallUIProceed();
  delegate_ = NULL;
  constrained_window_->CloseWebContentsModalDialog();
}

void ExtensionInstallDialogController::InstallUIAbort(bool user_initiated) {
  if (sampling_event_.get())
    sampling_event_->CreateUserDecisionEvent(ExperienceSamplingEvent::kDeny);
  delegate_->InstallUIAbort(user_initiated);
  delegate_ = NULL;
  constrained_window_->CloseWebContentsModalDialog();
}

void ExtensionInstallDialogController::OnConstrainedWindowClosed(
    ConstrainedWindowMac* window) {
  if (delegate_)
    delegate_->InstallUIAbort(false);
  base::MessageLoop::current()->DeleteSoon(FROM_HERE, this);
}

// static
ExtensionInstallPrompt::ShowDialogCallback
ExtensionInstallPrompt::GetDefaultShowDialogCallback() {
  return base::Bind(&ShowExtensionInstallDialogImpl);
}
