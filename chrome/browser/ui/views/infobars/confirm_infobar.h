// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_INFOBARS_CONFIRM_INFOBAR_H_
#define CHROME_BROWSER_UI_VIEWS_INFOBARS_CONFIRM_INFOBAR_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "chrome/browser/ui/views/infobars/infobar_view.h"
#include "ui/views/controls/link_listener.h"

class ConfirmInfoBarDelegate;

namespace views {
class Label;
}

// An infobar that shows a message, up to two optional buttons, and an optional,
// right-aligned link.  This is commonly used to do things like:
// "Would you like to do X?  [Yes]  [No]               _Learn More_ [x]"
class ConfirmInfoBar : public InfoBarView,
                       public views::LinkListener {
 public:
  ConfirmInfoBar(InfoBarService* owner, ConfirmInfoBarDelegate* delegate);

 private:
  virtual ~ConfirmInfoBar();

  // InfoBarView:
  virtual void Layout() OVERRIDE;
  virtual void ViewHierarchyChanged(
      const ViewHierarchyChangedDetails& details) OVERRIDE;
  virtual void ButtonPressed(views::Button* sender,
                             const ui::Event& event) OVERRIDE;
  virtual int ContentMinimumWidth() const OVERRIDE;

  // views::LinkListener:
  virtual void LinkClicked(views::Link* source, int event_flags) OVERRIDE;

  ConfirmInfoBarDelegate* GetDelegate();

  views::Label* label_;
  views::LabelButton* ok_button_;
  views::LabelButton* cancel_button_;
  views::Link* link_;

  DISALLOW_COPY_AND_ASSIGN(ConfirmInfoBar);
};

#endif  // CHROME_BROWSER_UI_VIEWS_INFOBARS_CONFIRM_INFOBAR_H_
