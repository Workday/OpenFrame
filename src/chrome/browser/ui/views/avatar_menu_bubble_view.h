// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AVATAR_MENU_BUBBLE_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_AVATAR_MENU_BUBBLE_VIEW_H_

#include <vector>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/gtest_prod_util.h"
#include "chrome/browser/profiles/avatar_menu_model_observer.h"
#include "ui/views/bubble/bubble_delegate.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/link_listener.h"

class AvatarMenuModel;
class Browser;
class ProfileItemView;

namespace content {
class WebContents;
}

namespace views {
class CustomButton;
class ImageView;
class Label;
class Link;
class Separator;
}

// This bubble view is displayed when the user clicks on the avatar button.
// It displays a list of profiles and allows users to switch between profiles.
class AvatarMenuBubbleView : public views::BubbleDelegateView,
                             public views::ButtonListener,
                             public views::LinkListener,
                             public AvatarMenuModelObserver {
 public:
  // Helper function to show the bubble and ensure that it doesn't reshow.
  // Normally this bubble is shown when there's a mouse down event on a button.
  // If the bubble is already showing when the user clicks on the button then
  // this will cause two things to happen:
  // - (1) the button will show a new instance of the bubble
  // - (2) the old instance of the bubble will get a deactivate event and
  //   close
  // To prevent this reshow this function checks if an instance of the bubble
  // is already showing and do nothing. This means that (1) will do nothing
  // and (2) will correctly hide the old bubble instance.
  static void ShowBubble(views::View* anchor_view,
                         views::BubbleBorder::Arrow arrow,
                         views::BubbleBorder::BubbleAlignment border_alignment,
                         const gfx::Rect& anchor_rect,
                         Browser* browser);
  static bool IsShowing();
  static void Hide();

  virtual ~AvatarMenuBubbleView();

  // views::View implementation.
  virtual gfx::Size GetPreferredSize() OVERRIDE;
  virtual void Layout() OVERRIDE;
  virtual bool AcceleratorPressed(const ui::Accelerator& accelerator) OVERRIDE;

  // views::ButtonListener implementation.
  virtual void ButtonPressed(views::Button* sender,
                             const ui::Event& event) OVERRIDE;

  // views::LinkListener implementation.
  virtual void LinkClicked(views::Link* source, int event_flags) OVERRIDE;

  // BubbleDelegate implementation.
  virtual gfx::Rect GetAnchorRect() OVERRIDE;
  virtual void Init() OVERRIDE;
  virtual void WindowClosing() OVERRIDE;

  // AvatarMenuModelObserver implementation.
  virtual void OnAvatarMenuModelChanged(
      AvatarMenuModel* avatar_menu_model) OVERRIDE;

  // We normally close the bubble any time it becomes inactive but this can lead
  // to flaky tests where unexpected UI events are triggering this behavior.
  // Tests should call this with "false" for more consistent operation.
  static void set_close_on_deactivate(bool close) {
    close_on_deactivate_ = close;
  }

 private:
  AvatarMenuBubbleView(views::View* anchor_view,
                       views::BubbleBorder::Arrow arrow,
                       const gfx::Rect& anchor_rect,
                       Browser* browser);

  // Sets the colors on all the |item_views_|. Called after the
  // BubbleDelegateView is created and has loaded the colors from the
  // NativeTheme.
  void SetBackgroundColors();

  // Create the menu contents for a normal profile.
  void InitMenuContents(AvatarMenuModel* avatar_menu_model);

  // Create the managed user specific contents of the menu.
  void InitManagedUserContents(AvatarMenuModel* avatar_menu_model);

  scoped_ptr<AvatarMenuModel> avatar_menu_model_;
  gfx::Rect anchor_rect_;
  Browser* browser_;
  std::vector<ProfileItemView*> item_views_;

  // Used to separate the link entry in the avatar menu from the other entries.
  views::Separator* separator_;

  // This will be non-NULL if and only if
  // avatar_menu_model_->ShouldShowAddNewProfileLink() returns true.  See
  // OnAvatarMenuModelChanged().
  views::View* buttons_view_;

  // This will be non-NULL if and only if |expanded_| is false and
  // avatar_menu_model_->GetManagedUserInformation() returns a non-empty string.
  // See OnAvatarMenuModelChanged().
  views::Label* managed_user_info_;
  views::ImageView* icon_view_;
  views::Separator* separator_switch_users_;
  views::Link* switch_profile_link_;

  static AvatarMenuBubbleView* avatar_bubble_;
  static bool close_on_deactivate_;

  // Is set to true if the managed user has clicked on Switch Users.
  bool expanded_;

  DISALLOW_COPY_AND_ASSIGN(AvatarMenuBubbleView);
  FRIEND_TEST_ALL_PREFIXES(AvatarMenuButtonTest, SignOut);
};

#endif  // CHROME_BROWSER_UI_VIEWS_AVATAR_MENU_BUBBLE_VIEW_H_
