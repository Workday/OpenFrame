// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_LOCATION_BAR_EV_BUBBLE_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_LOCATION_BAR_EV_BUBBLE_VIEW_H_

#include "chrome/browser/ui/views/location_bar/icon_label_bubble_view.h"
#include "chrome/browser/ui/views/location_bar/page_info_helper.h"

class LocationBarView;

// EVBubbleView displays the EV Bubble in the LocationBarView.
class EVBubbleView : public IconLabelBubbleView {
 public:
  EVBubbleView(const gfx::FontList& font_list,
               SkColor text_color,
               SkColor parent_background_color,
               LocationBarView* parent);
  ~EVBubbleView() override;

  // IconLabelBubbleView:
  SkColor GetTextColor() const override;
  SkColor GetBorderColor() const override;

  // views::View:
  gfx::Size GetMinimumSize() const override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnGestureEvent(ui::GestureEvent* event) override;
  void OnNativeThemeChanged(const ui::NativeTheme* native_theme) override;

  // Returns what the minimum size would be if the label text were |text|.
  gfx::Size GetMinimumSizeForLabelText(const base::string16& text) const;

 private:
  // Returns what the minimum size would be if the preferred size were |size|.
  gfx::Size GetMinimumSizeForPreferredSize(gfx::Size size) const;

  // TODO(estade): this should be gleaned from the theme instead of hardcoded in
  // location_bar_view.cc and cached here.
  SkColor text_color_;

  PageInfoHelper page_info_helper_;

  DISALLOW_COPY_AND_ASSIGN(EVBubbleView);
};

#endif  // CHROME_BROWSER_UI_VIEWS_LOCATION_BAR_EV_BUBBLE_VIEW_H_
