// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_PANELS_PANEL_FRAME_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_PANELS_PANEL_FRAME_VIEW_H_

#include "chrome/browser/ui/panels/panel_constants.h"
#include "chrome/browser/ui/views/tab_icon_view_model.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/window/non_client_view.h"

class PanelView;
class TabIconView;

namespace views {
class ImageButton;
class Label;
}

class PanelFrameView : public views::NonClientFrameView,
                       public views::ButtonListener,
                       public TabIconViewModel {
 public:
  enum PaintState {
    PAINT_AS_INACTIVE,
    PAINT_AS_ACTIVE,
    PAINT_AS_MINIMIZED,
    PAINT_FOR_ATTENTION
  };

  explicit PanelFrameView(PanelView* panel_view);
  ~PanelFrameView() override;

  void Init();
  void UpdateTitle();
  void UpdateIcon();
  void UpdateThrobber();
  void UpdateTitlebarMinimizeRestoreButtonVisibility();
  void SetWindowCornerStyle(panel::CornerStyle corner_style);

  // Returns the size of the non-client area, that is, the window size minus
  // the size of the client area.
  gfx::Size NonClientAreaSize() const;

  int BorderThickness() const;

  PaintState GetPaintState() const;

  views::ImageButton* close_button() const { return close_button_; }
  views::ImageButton* minimize_button() const { return minimize_button_; }
  views::ImageButton* restore_button() const { return restore_button_; }
  TabIconView* title_icon() const { return title_icon_; }
  views::Label* title_label() const { return title_label_; }
  panel::CornerStyle corner_style() const { return corner_style_; }

 private:
  // Overridden from views::NonClientFrameView:
  gfx::Rect GetBoundsForClientView() const override;
  gfx::Rect GetWindowBoundsForClientBounds(
      const gfx::Rect& client_bounds) const override;
  int NonClientHitTest(const gfx::Point& point) override;
  void GetWindowMask(const gfx::Size& size, gfx::Path* window_mask) override;
  void ResetWindowControls() override;
  void UpdateWindowIcon() override;
  void UpdateWindowTitle() override;
  void SizeConstraintsChanged() override;

  // Overridden from views::View:
  gfx::Size GetPreferredSize() const override;
  const char* GetClassName() const override;
  gfx::Size GetMinimumSize() const override;
  gfx::Size GetMaximumSize() const override;
  void Layout() override;
  void OnPaint(gfx::Canvas* canvas) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool OnMouseDragged(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnMouseCaptureLost() override;

  // Overridden from views::ButtonListener:
  void ButtonPressed(views::Button* sender, const ui::Event& event) override;

  // Overridden from TabIconViewModel:
  bool ShouldTabIconViewAnimate() const override;
  gfx::ImageSkia GetFaviconForTabIconView() override;

  int TitlebarHeight() const;

  const gfx::ImageSkia* GetFrameBackground(PaintState paint_state) const;

  // Update control styles to indicate if the titlebar is active or not.
  void UpdateControlStyles(PaintState paint_state);

  // Custom draw the frame.
  void PaintFrameBackground(gfx::Canvas* canvas);
  void PaintFrameEdge(gfx::Canvas* canvas);

  // Retrieves the drawing metrics based on the current painting state.
  SkColor GetTitleColor(PaintState paint_state) const;

  // Returns true if |mouse_location| is within the panel's resizing area.
  bool IsWithinResizingArea(const gfx::Point& mouse_location) const;

  static const char kViewClassName[];

  bool is_frameless_;
  PanelView* panel_view_;
  views::ImageButton* close_button_;
  views::ImageButton* minimize_button_;
  views::ImageButton* restore_button_;
  TabIconView* title_icon_;
  views::Label* title_label_;
  panel::CornerStyle corner_style_;

  DISALLOW_COPY_AND_ASSIGN(PanelFrameView);
};

#endif  // CHROME_BROWSER_UI_VIEWS_PANELS_PANEL_FRAME_VIEW_H_
