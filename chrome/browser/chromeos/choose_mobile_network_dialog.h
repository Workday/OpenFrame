// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_CHOOSE_MOBILE_NETWORK_DIALOG_H_
#define CHROME_BROWSER_CHROMEOS_CHOOSE_MOBILE_NETWORK_DIALOG_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/web_dialogs/web_dialog_delegate.h"

namespace chromeos {

// Dialog for manual selection of cellular network.
class ChooseMobileNetworkDialog : public ui::WebDialogDelegate {
 public:
  // Shows the dialog box.
  static void ShowDialog(gfx::NativeWindow owning_window);

 private:
  ChooseMobileNetworkDialog();

  // Overridden from ui::WebDialogDelegate:
  virtual ui::ModalType GetDialogModalType() const OVERRIDE;
  virtual string16 GetDialogTitle() const OVERRIDE;
  virtual GURL GetDialogContentURL() const OVERRIDE;
  virtual void GetWebUIMessageHandlers(
      std::vector<content::WebUIMessageHandler*>* handlers) const OVERRIDE;
  virtual void GetDialogSize(gfx::Size* size) const OVERRIDE;
  virtual std::string GetDialogArgs() const OVERRIDE;
  virtual void OnDialogClosed(const std::string& json_retval) OVERRIDE;
  virtual void OnCloseContents(
      content::WebContents* source, bool* out_close_dialog) OVERRIDE;
  virtual bool ShouldShowDialogTitle() const OVERRIDE;
  virtual bool HandleContextMenu(
      const content::ContextMenuParams& params) OVERRIDE;

  DISALLOW_COPY_AND_ASSIGN(ChooseMobileNetworkDialog);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_CHOOSE_MOBILE_NETWORK_DIALOG_H_
