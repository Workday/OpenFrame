// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_ABOUT_IPC_DIALOG_H_
#define CHROME_BROWSER_UI_VIEWS_ABOUT_IPC_DIALOG_H_

#include "ipc/ipc_message.h"  // For IPC_MESSAGE_LOG_ENABLED.

#if defined(OS_WIN) && defined(IPC_MESSAGE_LOG_ENABLED)

#include <atlbase.h>
#include <atlapp.h>
#include <atlctrls.h>
#include <atlwin.h>

#include "ipc/ipc_logging.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/window/dialog_delegate.h"

template <typename T> struct DefaultSingletonTraits;

namespace views {
class LabelButton;
class NativeViewHost;
}

class AboutIPCDialog : public views::DialogDelegateView,
                       public views::ButtonListener,
                       public IPC::Logging::Consumer {
 public:
  // This dialog is a singleton. If the dialog is already opened, it won't do
  // anything, so you can just blindly call this function all you want.
  static void RunDialog();

  virtual ~AboutIPCDialog();

 private:
  friend struct DefaultSingletonTraits<AboutIPCDialog>;

  AboutIPCDialog();

  // Sets up all UI controls for the dialog.
  void SetupControls();

  // views::View overrides.
  virtual gfx::Size GetPreferredSize() OVERRIDE;
  virtual int GetDialogButtons() const OVERRIDE;
  virtual string16 GetWindowTitle() const OVERRIDE;
  virtual void Layout() OVERRIDE;

  // IPC::Logging::Consumer implementation.
  virtual void Log(const IPC::LogData& data) OVERRIDE;

  // views::WidgetDelegate (via views::DialogDelegateView).
  virtual bool CanResize() const OVERRIDE;
  virtual bool UseNewStyleForThisDialog() const OVERRIDE;

  // views::ButtonListener.
  virtual void ButtonPressed(views::Button* button,
                             const ui::Event& event) OVERRIDE;

  WTL::CListViewCtrl message_list_;

  views::LabelButton* track_toggle_;
  views::LabelButton* clear_button_;
  views::LabelButton* filter_button_;
  views::NativeViewHost* table_;

  // Set to true when we're tracking network status.
  bool tracking_;

  DISALLOW_COPY_AND_ASSIGN(AboutIPCDialog);
};

#endif  // OS_WIN && IPC_MESSAGE_LOG_ENABLED

#endif  // CHROME_BROWSER_UI_VIEWS_ABOUT_IPC_DIALOG_H_
