// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_DETACHABLE_TOOLBAR_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_DETACHABLE_TOOLBAR_VIEW_H_

#include "chrome/browser/ui/host_desktop.h"
#include "ui/views/accessible_pane_view.h"

struct SkRect;

// DetachableToolbarView contains functionality common to views that can detach
// from the Chrome frame, such as the BookmarkBarView and the Extension shelf.
class DetachableToolbarView : public views::AccessiblePaneView {
 public:
  // The color gradient start value close to the edge of the divider.
  static const SkColor kEdgeDividerColor;
  // The color gradient value for the middle of the divider.
  static const SkColor kMiddleDividerColor;

  DetachableToolbarView() {}
  virtual ~DetachableToolbarView() {}

  // Whether the view is currently detached from the Chrome frame.
  virtual bool IsDetached() const = 0;

  // Gets the current state of the resize animation (show/hide).
  virtual double GetAnimationValue() const = 0;

  // Gets the current amount of overlap atop the browser toolbar.
  virtual int GetToolbarOverlap() const = 0;

  // Paints the background (including the theme image behind content area) for
  // the bar/shelf when it is attached to the top toolbar into |bounds|.
  // |background_origin| is the origin to use for painting the theme image.
  static void PaintBackgroundAttachedMode(
      gfx::Canvas* canvas,
      ui::ThemeProvider* theme_provider,
      const gfx::Rect& bounds,
      const gfx::Point& background_origin,
     chrome::HostDesktopType host_desktop_type);

  // Calculate the rect for the content area of the bar/shelf. This is only
  // needed when the bar/shelf is detached from the Chrome frame (otherwise the
  // content area is the whole area of the bar/shelf. When detached, however,
  // only a small round rectangle is for drawing our content on. This calculates
  // how big this area is, where it is located within the shelf and how round
  // the edges should be.
  static void CalculateContentArea(double animation_state,
                                   double horizontal_padding,
                                   double vertical_padding,
                                   SkRect* rect,
                                   double* roundness,
                                   views::View* view);

  // Paint the horizontal border separating the shelf/bar from the toolbar or
  // page content according to |at_top| with |color|.
  static void PaintHorizontalBorder(gfx::Canvas* canvas,
                                    DetachableToolbarView* view,
                                    bool at_top,
                                    SkColor color);

  // Paint the background of the content area (the surface behind the
  // bookmarks). |rect| is the rectangle to paint the background within.
  // |roundness| describes the roundness of the corners.
  static void PaintContentAreaBackground(gfx::Canvas* canvas,
                                         ui::ThemeProvider* theme_provider,
                                         const SkRect& rect,
                                         double roundness);
  // Paint the border around the content area (when in detached mode).
  static void PaintContentAreaBorder(gfx::Canvas* canvas,
                                     ui::ThemeProvider* theme_provider,
                                     const SkRect& rect,
                                     double roundness);

  // Paint a themed gradient divider at location |x|. |height| is the full
  // height of the view you want to paint the divider into, not the height of
  // the divider. The height of the divider will become:
  //   |height| - 2 * |vertical_padding|.
  // The color of the divider is a gradient starting with |top_color| at the
  // top, and changing into |middle_color| and then over to |bottom_color| as
  // you go further down.
  static void PaintVerticalDivider(gfx::Canvas* canvas,
                                   int x,
                                   int height,
                                   int vertical_padding,
                                   SkColor top_color,
                                   SkColor middle_color,
                                   SkColor bottom_color);

 private:
  DISALLOW_COPY_AND_ASSIGN(DetachableToolbarView);
};

#endif  // CHROME_BROWSER_UI_VIEWS_DETACHABLE_TOOLBAR_VIEW_H_
