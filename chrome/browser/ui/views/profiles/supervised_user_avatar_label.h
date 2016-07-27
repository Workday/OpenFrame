// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_PROFILES_SUPERVISED_USER_AVATAR_LABEL_H_
#define CHROME_BROWSER_UI_VIEWS_PROFILES_SUPERVISED_USER_AVATAR_LABEL_H_

#include "base/compiler_specific.h"
#include "chrome/browser/supervised_user/supervised_users.h"
#include "ui/views/controls/button/label_button.h"

class BrowserView;

// SupervisedUserAvatarLabel
//
// A label used to display a string indicating that the current profile belongs
// to a supervised user.
class SupervisedUserAvatarLabel : public views::LabelButton {
 public:
  explicit SupervisedUserAvatarLabel(BrowserView* browser_view);
  ~SupervisedUserAvatarLabel() override;

  // views::LabelButton:
  bool OnMousePressed(const ui::MouseEvent& event) override;

  // Update the style of the label according to the provided theme.
  void UpdateLabelStyle();

  // Sets whether the label should be displayed on the right or on the left. A
  // new button border is created which has the right insets for the positioning
  // of the button.
  void SetLabelOnRight(bool label_on_right);

 private:
  BrowserView* browser_view_;

  DISALLOW_COPY_AND_ASSIGN(SupervisedUserAvatarLabel);
};

#endif  // CHROME_BROWSER_UI_VIEWS_PROFILES_SUPERVISED_USER_AVATAR_LABEL_H_
