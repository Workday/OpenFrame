// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/toolbar/back_button.h"

#include "ui/gfx/geometry/insets.h"
#include "ui/views/animation/ink_drop_animation_controller.h"
#include "ui/views/controls/button/label_button_border.h"
#include "ui/views/painter.h"

BackButton::BackButton(views::ButtonListener* listener,
                       ui::MenuModel* model)
    : ToolbarButton(listener, model),
      margin_leading_(0) {}

BackButton::~BackButton() {}

void BackButton::SetLeadingMargin(int margin) {
  margin_leading_ = margin;

  UpdateThemedBorder();

  const int inset = LabelButton::kFocusRectInset;
  const bool is_rtl = base::i18n::IsRTL();
  const gfx::Insets insets(inset, inset + (is_rtl ? 0 : margin),
                           inset, inset + (is_rtl ? margin : 0));
  SetFocusPainter(views::Painter::CreateDashedFocusPainterWithInsets(insets));
  InvalidateLayout();
}

gfx::Point BackButton::CalculateInkDropCenter() const {
  int visible_width = GetPreferredSize().width();
  return gfx::Point(
      GetMirroredXWithWidthInView(margin_leading_, visible_width) +
          visible_width / 2,
      height() / 2);
}

const char* BackButton::GetClassName() const {
  return "BackButton";
}

scoped_ptr<views::LabelButtonBorder> BackButton::CreateDefaultBorder() const {
  scoped_ptr<views::LabelButtonBorder> border =
      ToolbarButton::CreateDefaultBorder();

  // Adjust border insets to follow the margin change,
  // which will be reflected in where the border is painted
  // through GetThemePaintRect().
  const gfx::Insets insets(border->GetInsets());
  border->set_insets(gfx::Insets(insets.top(), insets.left() + margin_leading_,
                                 insets.bottom(), insets.right()));

  return border.Pass();
}

gfx::Rect BackButton::GetThemePaintRect() const {
  gfx::Rect rect(LabelButton::GetThemePaintRect());
  const bool is_rtl = base::i18n::IsRTL();
  rect.Inset(is_rtl ? 0 : margin_leading_, 0, is_rtl ? margin_leading_ : 0, 0);
  return rect;
}

