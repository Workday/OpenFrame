// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/infobars/infobar_background.h"

#include "chrome/browser/ui/infobar_container_delegate.h"
#include "chrome/browser/ui/views/infobars/infobar_view.h"
#include "components/infobars/core/infobar.h"
#include "third_party/skia/include/effects/SkGradientShader.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/views/view.h"

InfoBarBackground::InfoBarBackground(
    infobars::InfoBarDelegate::Type infobar_type)
    : separator_color_(SK_ColorBLACK),
      top_color_(infobars::InfoBar::GetTopColor(infobar_type)),
      bottom_color_(infobars::InfoBar::GetBottomColor(infobar_type)) {
  SetNativeControlColor(
      color_utils::AlphaBlend(top_color_, bottom_color_, 128));
}

InfoBarBackground::~InfoBarBackground() {
}

void InfoBarBackground::Paint(gfx::Canvas* canvas, views::View* view) const {
  SkPoint gradient_points[2];
  gradient_points[0].iset(0, 0);
  gradient_points[1].iset(0, view->height());
  SkColor gradient_colors[2] = { top_color_, bottom_color_ };
  skia::RefPtr<SkShader> gradient_shader = skia::AdoptRef(
      SkGradientShader::CreateLinear(gradient_points, gradient_colors, NULL, 2,
                                     SkShader::kClamp_TileMode));
  SkPaint paint;
  paint.setStrokeWidth(
      SkIntToScalar(InfoBarContainerDelegate::kSeparatorLineHeight));
  paint.setStyle(SkPaint::kFill_Style);
  paint.setStrokeCap(SkPaint::kRound_Cap);
  paint.setShader(gradient_shader.get());

  InfoBarView* infobar = static_cast<InfoBarView*>(view);
  SkCanvas* canvas_skia = canvas->sk_canvas();
  canvas_skia->drawPath(infobar->fill_path(), paint);

  paint.setShader(NULL);
  paint.setColor(SkColorSetA(separator_color_,
                             SkColorGetA(gradient_colors[0])));
  paint.setStyle(SkPaint::kStroke_Style);
  // Anti-alias the path so it doesn't look goofy when the edges are not at 45
  // degree angles, but don't anti-alias anything else, especially not the fill,
  // lest we get weird color bleeding problems.
  paint.setAntiAlias(true);
  canvas_skia->drawPath(infobar->stroke_path(), paint);
  paint.setAntiAlias(false);

  // Now draw the separator at the bottom.
  canvas->FillRect(
      gfx::Rect(0,
                view->height() - InfoBarContainerDelegate::kSeparatorLineHeight,
                view->width(), InfoBarContainerDelegate::kSeparatorLineHeight),
      separator_color_);
}
