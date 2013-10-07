// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_OPAQUE_BROWSER_FRAME_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_OPAQUE_BROWSER_FRAME_VIEW_H_

#include "base/memory/scoped_ptr.h"
#include "chrome/browser/ui/views/frame/browser_frame.h"
#include "chrome/browser/ui/views/frame/browser_non_client_frame_view.h"
#include "chrome/browser/ui/views/tab_icon_view_model.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/window/non_client_view.h"

class BrowserView;
class TabIconView;

namespace views {
class ImageButton;
class FrameBackground;
class Label;
}

class OpaqueBrowserFrameView : public BrowserNonClientFrameView,
                               public content::NotificationObserver,
                               public views::ButtonListener,
                               public chrome::TabIconViewModel {
 public:
  // Constructs a non-client view for an BrowserFrame.
  OpaqueBrowserFrameView(BrowserFrame* frame, BrowserView* browser_view);
  virtual ~OpaqueBrowserFrameView();

  // Overridden from BrowserNonClientFrameView:
  virtual gfx::Rect GetBoundsForTabStrip(views::View* tabstrip) const OVERRIDE;
  virtual TabStripInsets GetTabStripInsets(bool restored) const OVERRIDE;
  virtual int GetThemeBackgroundXInset() const OVERRIDE;
  virtual void UpdateThrobber(bool running) OVERRIDE;
  virtual gfx::Size GetMinimumSize() OVERRIDE;

 protected:
  views::ImageButton* minimize_button() const { return minimize_button_; }
  views::ImageButton* maximize_button() const { return maximize_button_; }
  views::ImageButton* restore_button() const { return restore_button_; }
  views::ImageButton* close_button() const { return close_button_; }

  // Used to allow subclasses to reserve height for other components they
  // will add.  The space is reserved below the ClientView.
  virtual int GetReservedHeight() const;
  virtual gfx::Rect GetBoundsForReservedArea() const;

  // Returns the height of the entire nonclient top border, including the window
  // frame, any title area, and any connected client edge.  If |restored| is
  // true, acts as if the window is restored regardless of the real mode.
  int NonClientTopBorderHeight(bool restored) const;

  // Overridden from views::NonClientFrameView:
  virtual gfx::Rect GetBoundsForClientView() const OVERRIDE;
  virtual gfx::Rect GetWindowBoundsForClientBounds(
      const gfx::Rect& client_bounds) const OVERRIDE;
  virtual int NonClientHitTest(const gfx::Point& point) OVERRIDE;
  virtual void GetWindowMask(const gfx::Size& size, gfx::Path* window_mask)
      OVERRIDE;
  virtual void ResetWindowControls() OVERRIDE;
  virtual void UpdateWindowIcon() OVERRIDE;
  virtual void UpdateWindowTitle() OVERRIDE;

  // Overridden from views::View:
  virtual void OnPaint(gfx::Canvas* canvas) OVERRIDE;
  virtual void Layout() OVERRIDE;
  virtual bool HitTestRect(const gfx::Rect& rect) const OVERRIDE;
  virtual void GetAccessibleState(ui::AccessibleViewState* state) OVERRIDE;

  // Overridden from views::ButtonListener:
  virtual void ButtonPressed(views::Button* sender, const ui::Event& event)
      OVERRIDE;

  // Overridden from chrome::TabIconViewModel:
  virtual bool ShouldTabIconViewAnimate() const OVERRIDE;
  virtual gfx::ImageSkia GetFaviconForTabIconView() OVERRIDE;

  // content::NotificationObserver implementation:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

 private:
  // Creates, adds and returns a new image button with |this| as its listener.
  // Memory is owned by the caller.
  views::ImageButton* InitWindowCaptionButton(int normal_image_id,
                                              int hot_image_id,
                                              int pushed_image_id,
                                              int mask_image_id,
                                              int accessibility_string_id);

  // Returns the thickness of the border that makes up the window frame edges.
  // This does not include any client edge.  If |restored| is true, acts as if
  // the window is restored regardless of the real mode.
  int FrameBorderThickness(bool restored) const;

  // Returns the height of the top resize area.  This is smaller than the frame
  // border height in order to increase the window draggable area.
  int TopResizeHeight() const;

  // Returns the thickness of the entire nonclient left, right, and bottom
  // borders, including both the window frame and any client edge.
  int NonClientBorderThickness() const;

  // Returns the y-coordinate of the caption buttons.  If |restored| is true,
  // acts as if the window is restored regardless of the real mode.
  int CaptionButtonY(bool restored) const;

  // Returns the thickness of the 3D edge along the bottom of the titlebar.  If
  // |restored| is true, acts as if the window is restored regardless of the
  // real mode.
  int TitlebarBottomThickness(bool restored) const;

  // Returns the size of the titlebar icon.  This is used even when the icon is
  // not shown, e.g. to set the titlebar height.
  int IconSize() const;

  // Returns the bounds of the titlebar icon (or where the icon would be if
  // there was one).
  gfx::Rect IconBounds() const;

  // Returns the combined bounds for the tab strip and avatar area.
  gfx::Rect GetBoundsForTabStripAndAvatarArea(views::View* tabstrip) const;

  // Paint various sub-components of this view.  The *FrameBorder() functions
  // also paint the background of the titlebar area, since the top frame border
  // and titlebar background are a contiguous component.
  void PaintRestoredFrameBorder(gfx::Canvas* canvas);
  void PaintMaximizedFrameBorder(gfx::Canvas* canvas);
  void PaintToolbarBackground(gfx::Canvas* canvas);
  void PaintRestoredClientEdge(gfx::Canvas* canvas);

  // Compute aspects of the frame needed to paint the frame background.
  SkColor GetFrameColor() const;
  gfx::ImageSkia* GetFrameImage() const;
  gfx::ImageSkia* GetFrameOverlayImage() const;
  int GetTopAreaHeight() const;

  // Layout various sub-components of this view.
  void LayoutWindowControls();
  void LayoutTitleBar();
  void LayoutAvatar();

  // Returns the bounds of the client area for the specified view size.
  gfx::Rect CalculateClientAreaBounds(int width, int height) const;

  // The layout rect of the avatar icon, if visible.
  gfx::Rect avatar_bounds_;

  // Window controls.
  views::ImageButton* minimize_button_;
  views::ImageButton* maximize_button_;
  views::ImageButton* restore_button_;
  views::ImageButton* close_button_;

  // The window icon and title.
  TabIconView* window_icon_;
  views::Label* window_title_;

  // The bounds of the ClientView.
  gfx::Rect client_view_bounds_;

  content::NotificationRegistrar registrar_;

  // Background painter for the window frame.
  scoped_ptr<views::FrameBackground> frame_background_;

  DISALLOW_COPY_AND_ASSIGN(OpaqueBrowserFrameView);
};

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_OPAQUE_BROWSER_FRAME_VIEW_H_
