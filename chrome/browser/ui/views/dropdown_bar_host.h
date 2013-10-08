// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_DROPDOWN_BAR_HOST_H_
#define CHROME_BROWSER_UI_VIEWS_DROPDOWN_BAR_HOST_H_

#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "content/public/browser/native_web_keyboard_event.h"
#include "ui/base/animation/animation_delegate.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/gfx/rect.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/focus/focus_manager.h"

class BrowserView;
class DropdownBarHostDelegate;
class DropdownBarView;

namespace content {
class WebContents;
}

namespace ui {
class SlideAnimation;
}  // namespace ui

namespace views {
class ExternalFocusTracker;
class View;
class Widget;
}  // namespace views

////////////////////////////////////////////////////////////////////////////////
//
// The DropdownBarHost implements the container widget for the UI that
// is shown at the top of browser contents. It uses the appropriate
// implementation from dropdown_bar_host_win.cc or dropdown_bar_host_aura.cc to
// draw its content and is responsible for showing, hiding, animating, closing,
// and moving the bar if needed, for example if the widget is
// obscuring the selection results in FindBar.
//
////////////////////////////////////////////////////////////////////////////////
class DropdownBarHost : public ui::AcceleratorTarget,
                        public views::FocusChangeListener,
                        public ui::AnimationDelegate {
 public:
  explicit DropdownBarHost(BrowserView* browser_view);
  virtual ~DropdownBarHost();

  // Initializes the DropdownBarHost. This creates the widget that |view| paints
  // into.
  // |host_view| is the view whose position in the |browser_view_| view
  // hierarchy determines the z-order of the widget relative to views with
  // layers and views with associated NativeViews.
  void Init(views::View* host_view,
            views::View* view,
            DropdownBarHostDelegate* delegate);

  // Whether we are animating the position of the dropdown widget.
  bool IsAnimating() const;
  // Returns true if the dropdown bar view is visible, or false otherwise.
  bool IsVisible() const;
  // Selects text in the entry field and set focus.
  void SetFocusAndSelection();
  // Stops the animation.
  void StopAnimation();

  // Shows the dropdown bar.
  virtual void Show(bool animate);
  // Hides the dropdown bar.
  virtual void Hide(bool animate);

  // Returns the rectangle representing where to position the dropdown widget.
  virtual gfx::Rect GetDialogPosition(gfx::Rect avoid_overlapping_rect) = 0;

  // Moves the widget to the provided location, moves it to top
  // in the z-order (HWND_TOP, not HWND_TOPMOST for windows) and shows
  // the widget (if hidden).
  virtual void SetDialogPosition(const gfx::Rect& new_pos, bool no_redraw) = 0;

  // Overridden from views::FocusChangeListener:
  virtual void OnWillChangeFocus(views::View* focused_before,
                                 views::View* focused_now) OVERRIDE;
  virtual void OnDidChangeFocus(views::View* focused_before,
                                views::View* focused_now) OVERRIDE;

  // Overridden from ui::AcceleratorTarget:
  virtual bool AcceleratorPressed(const ui::Accelerator& accelerator) = 0;
  virtual bool CanHandleAccelerators() const = 0;

  // ui::AnimationDelegate implementation:
  virtual void AnimationProgressed(const ui::Animation* animation) OVERRIDE;
  virtual void AnimationEnded(const ui::Animation* animation) OVERRIDE;

  // During testing we can disable animations by setting this flag to true,
  // so that opening and closing the dropdown bar is shown instantly, instead of
  // having to poll it while it animates to open/closed status.
  static bool disable_animations_during_testing_;

  // Returns the browser view that the dropdown belongs to.
  BrowserView* browser_view() const { return browser_view_; }

  // Registers this class as the handler for when Escape is pressed. Once we
  // loose focus we will unregister Escape and (any accelerators the derived
  // classes registers by using overrides of RegisterAccelerators). See also:
  // SetFocusChangeListener().
  virtual void RegisterAccelerators();

  // When we loose focus, we unregister all accelerator handlers. See also:
  // SetFocusChangeListener().
  virtual void UnregisterAccelerators();

 protected:
  // Called when the drop down bar visibility, aka the value of IsVisible(),
  // changes.
  virtual void OnVisibilityChanged();

  // Returns the dropdown bar view.
  views::View* view() const { return view_; }

  // Returns the focus tracker.
  views::ExternalFocusTracker* focus_tracker() const {
    return focus_tracker_.get();
  }

  // Resets the focus tracker.
  void ResetFocusTracker();

  // The focus manager we register with to keep track of focus changes.
  views::FocusManager* focus_manager() const { return focus_manager_; }

  // Returns the host widget.
  views::Widget* host() const { return host_.get(); }

  // Returns the animation offset.
  int animation_offset() const { return animation_offset_; }

  // Retrieves the boundary that the dropdown widget has to work with
  // within the Chrome frame window. The boundary differs depending on
  // the dropdown bar implementation. The default implementation
  // returns the boundary of browser_view and the drop down
  // can be shown in any client area.
  virtual void GetWidgetBounds(gfx::Rect* bounds);

  // The find bar widget needs rounded edges, so we create a polygon
  // that corresponds to the background images for this window (and
  // make the polygon only contain the pixels that we want to
  // draw). The polygon is then given to SetWindowRgn which changes
  // the window from being a rectangle in shape, to being a rect with
  // curved edges. We also check to see if the region should be
  // truncated to prevent from drawing onto Chrome's window border.
  void UpdateWindowEdges(const gfx::Rect& new_pos);

  // Allows implementation to tweak widget position.
  void SetWidgetPositionNative(const gfx::Rect& new_pos, bool no_redraw);

  // Returns a keyboard event suitable for forwarding.
  content::NativeWebKeyboardEvent GetKeyboardEvent(
      const content::WebContents* contents,
      const ui::KeyEvent& key_event);

  // Returns the animation for the dropdown.
  ui::SlideAnimation* animation() {
    return animation_.get();
  }

 private:
  // Set the view whose position in the |browser_view_| view hierarchy
  // determines the z-order of |host_| relative to views with layers and
  // views with associated NativeViews.
  void SetHostViewNative(views::View* host_view);

  // The BrowserView that created us.
  BrowserView* browser_view_;

  // Our view, which is responsible for drawing the UI.
  views::View* view_;
  DropdownBarHostDelegate* delegate_;

  // The y position pixel offset of the widget while animating the
  // dropdown widget.
  int animation_offset_;

  // The animation class to use when opening the Dropdown widget.
  scoped_ptr<ui::SlideAnimation> animation_;

  // The focus manager we register with to keep track of focus changes.
  views::FocusManager* focus_manager_;

  // True if the accelerator target for Esc key is registered.
  bool esc_accel_target_registered_;

  // Tracks and stores the last focused view which is not the DropdownBarView
  // or any of its children. Used to restore focus once the DropdownBarView is
  // closed.
  scoped_ptr<views::ExternalFocusTracker> focus_tracker_;

  // Host is the Widget implementation that is created and maintained by the
  // dropdown bar. It contains the DropdownBarView.
  scoped_ptr<views::Widget> host_;

  // A flag to manually manage visibility. GTK/X11 is asynchronous and
  // the state of the widget can be out of sync.
  bool is_visible_;

  DISALLOW_COPY_AND_ASSIGN(DropdownBarHost);
};

#endif  // CHROME_BROWSER_UI_VIEWS_DROPDOWN_BAR_HOST_H_
