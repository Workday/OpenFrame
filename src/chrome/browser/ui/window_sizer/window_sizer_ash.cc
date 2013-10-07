// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/window_sizer/window_sizer.h"

#include "ash/ash_switches.h"
#include "ash/shell.h"
#include "ash/wm/mru_window_tracker.h"
#include "ash/wm/window_util.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/fullscreen/fullscreen_controller.h"
#include "chrome/browser/ui/host_desktop.h"
#include "ui/aura/root_window.h"
#include "ui/aura/window.h"
#include "ui/aura/window_delegate.h"
#include "ui/gfx/screen.h"

namespace {

// When a window gets opened in default mode and the screen is less than or
// equal to this width, the window will get opened in maximized mode. This value
// can be reduced to a "tame" number if the feature is disabled.
const int kForceMaximizeWidthLimit = 1366;
const int kForceMaximizeWidthLimitDisabled = 640;

// Check if the given browser is 'valid': It is a tabbed, non minimized
// window, which intersects with the |bounds_in_screen| area of a given screen.
bool IsValidBrowser(Browser* browser, const gfx::Rect& bounds_in_screen) {
  return (browser && browser->window() &&
          !browser->is_type_popup() &&
          !browser->window()->IsMinimized() &&
          browser->window()->GetNativeWindow() &&
          bounds_in_screen.Intersects(
              browser->window()->GetNativeWindow()->GetBoundsInScreen()));
}

// Check if the window was not created as popup or as panel, it is
// on the screen defined by |bounds_in_screen| and visible.
bool IsValidToplevelWindow(aura::Window* window,
                           const gfx::Rect& bounds_in_screen) {
  const BrowserList* ash_browser_list =
      BrowserList::GetInstance(chrome::HOST_DESKTOP_TYPE_ASH);
  for (BrowserList::const_iterator iter = ash_browser_list->begin();
       iter != ash_browser_list->end();
       ++iter) {
    Browser* browser = *iter;
    if (browser && browser->window() &&
        browser->window()->GetNativeWindow() == window) {
      return IsValidBrowser(browser, bounds_in_screen);
    }
  }
  // A window which has no browser associated with it is probably not a window
  // of which we want to copy the size from.
  return false;
}

// Get the first open (non minimized) window which is on the screen defined
// by |bounds_in_screen| and visible.
aura::Window* GetTopWindow(const gfx::Rect& bounds_in_screen) {
  // Get the active window.
  aura::Window* window = ash::wm::GetActiveWindow();
  if (window && window->type() == aura::client::WINDOW_TYPE_NORMAL &&
      window->IsVisible() && IsValidToplevelWindow(window, bounds_in_screen)) {
    return window;
  }

  // Get a list of all windows.
  const std::vector<aura::Window*> windows =
      ash::MruWindowTracker::BuildWindowList(false);

  if (windows.empty())
    return NULL;

  aura::Window::Windows::const_iterator iter = windows.begin();
  // Find the index of the current window.
  if (window)
    iter = std::find(windows.begin(), windows.end(), window);

  int index = (iter == windows.end()) ? 0 : (iter - windows.begin());

  // Scan the cycle list backwards to see which is the second topmost window
  // (and so on). Note that we might cycle a few indices twice if there is no
  // suitable window. However - since the list is fairly small this should be
  // very fast anyways.
  for (int i = index + windows.size(); i >= 0; i--) {
    aura::Window* window = windows[i % windows.size()];
    if (window && window->type() == aura::client::WINDOW_TYPE_NORMAL &&
        bounds_in_screen.Intersects(window->GetBoundsInScreen()) &&
        window->IsVisible()
        && IsValidToplevelWindow(window, bounds_in_screen)) {
      return window;
    }
  }
  return NULL;
}

// Return the number of valid top level windows on the screen defined by
// the |bounds_in_screen| rectangle.
int GetNumberOfValidTopLevelBrowserWindows(const gfx::Rect& bounds_in_screen) {
  int count = 0;
  const BrowserList* ash_browser_list =
      BrowserList::GetInstance(chrome::HOST_DESKTOP_TYPE_ASH);
  for (BrowserList::const_iterator iter = ash_browser_list->begin();
       iter != ash_browser_list->end();
       ++iter) {
    if (IsValidBrowser(*iter, bounds_in_screen))
      count++;
  }
  return count;
}

// Move the given |bounds_in_screen| on the available |work_area| to the
// direction. If |move_right| is true, the rectangle gets moved to the right
// corner. Otherwise to the left side.
bool MoveRect(const gfx::Rect& work_area,
              gfx::Rect& bounds_in_screen,
              bool move_right) {
  if (move_right) {
    if (work_area.right() > bounds_in_screen.right()) {
      bounds_in_screen.set_x(work_area.right() - bounds_in_screen.width());
      return true;
    }
  } else {
    if (work_area.x() < bounds_in_screen.x()) {
      bounds_in_screen.set_x(work_area.x());
      return true;
    }
  }
  return false;
}

}  // namespace

// static
int WindowSizer::GetForceMaximizedWidthLimit() {
  static int maximum_limit = 0;
  if (!maximum_limit) {
    maximum_limit = CommandLine::ForCurrentProcess()->HasSwitch(
                        ash::switches::kAshDisableAutoMaximizing) ?
        kForceMaximizeWidthLimitDisabled : kForceMaximizeWidthLimit;
  }
  return maximum_limit;
}

bool WindowSizer::GetBoundsOverrideAsh(gfx::Rect* bounds_in_screen,
                                       ui::WindowShowState* show_state) const {
  DCHECK(show_state);
  DCHECK(bounds_in_screen);

  if (browser_ &&
      browser_->host_desktop_type() != chrome::HOST_DESKTOP_TYPE_ASH) {
    return false;
  }
  bounds_in_screen->SetRect(0, 0, 0, 0);

  // Experiment: Force the maximize mode for all windows.
  if (ash::Shell::IsForcedMaximizeMode()) {
    // Exceptions: Do not maximize popups and do not maximize windowed V1 apps
    // which explicitly specify a |show_state| (they might be tuned for a
    // particular resolution / type).
    bool is_tabbed = browser_ && browser_->is_type_tabbed();
    bool is_popup = browser_ && browser_->is_type_popup();
    if (!is_popup && (is_tabbed || *show_state == ui::SHOW_STATE_DEFAULT))
      *show_state = ui::SHOW_STATE_MAXIMIZED;
  }

  ui::WindowShowState passed_show_state = *show_state;
  bool has_saved_bounds = true;
  if (!GetSavedWindowBounds(bounds_in_screen, show_state)) {
    has_saved_bounds = false;
    GetDefaultWindowBounds(bounds_in_screen);
  }

  if (browser_ && browser_->is_type_tabbed()) {
    aura::RootWindow* active = ash::Shell::GetActiveRootWindow();
    // Always open new window in the active display.
    gfx::Rect active_area = active->GetBoundsInScreen();
    gfx::Rect work_area =
        monitor_info_provider_->GetMonitorWorkAreaMatching(active_area);

    // This is a window / app. See if there is no window and try to place it.
    int count = GetNumberOfValidTopLevelBrowserWindows(work_area);
    aura::Window* top_window = GetTopWindow(work_area);
    // Our window should not have any impact if we are already on top.
    if (browser_->window() &&
        top_window == browser_->window()->GetNativeWindow())
      top_window = NULL;

    // If there is no valid other window we take the coordinates as is.
    if ((!count || !top_window)) {
      if (has_saved_bounds) {
        // Restore to previous state - if there is one.
        bounds_in_screen->AdjustToFit(work_area);
        return true;
      }
      // When using "small screens" we want to always open in full screen mode.
      if (passed_show_state == ui::SHOW_STATE_DEFAULT &&
          !browser_->is_session_restore() &&
          work_area.width() <= GetForceMaximizedWidthLimit() &&
          (!browser_->window() || !browser_->window()->IsFullscreen()) &&
          (!browser_->fullscreen_controller() ||
           !browser_->fullscreen_controller()->IsFullscreenForBrowser()))
        *show_state = ui::SHOW_STATE_MAXIMIZED;
      return true;
    }
    bool maximized = ash::wm::IsWindowMaximized(top_window);
    // We ignore the saved show state, but look instead for the top level
    // window's show state.
    if (passed_show_state == ui::SHOW_STATE_DEFAULT) {
      *show_state = maximized ? ui::SHOW_STATE_MAXIMIZED :
                                ui::SHOW_STATE_DEFAULT;
    }

    if (maximized)
      return true;

    // Use the size of the other window, and mirror the location to the
    // opposite side. Then make sure that it is inside our work area
    // (if possible).
    *bounds_in_screen = top_window->GetBoundsInScreen();

    bool move_right =
        bounds_in_screen->CenterPoint().x() < work_area.CenterPoint().x();

    MoveRect(work_area, *bounds_in_screen, move_right);
    bounds_in_screen->AdjustToFit(work_area);
    return true;
  }

  return false;
}

void WindowSizer::GetDefaultWindowBoundsAsh(gfx::Rect* default_bounds) const {
  DCHECK(default_bounds);
  DCHECK(monitor_info_provider_.get());

  gfx::Rect work_area = monitor_info_provider_->GetPrimaryDisplayWorkArea();

  // There should be a 'desktop' border around the window at the left and right
  // side.
  int default_width = work_area.width() - 2 * kDesktopBorderSize;
  // There should also be a 'desktop' border around the window at the top.
  // Since the workspace excludes the tray area we only need one border size.
  int default_height = work_area.height() - kDesktopBorderSize;
  // We align the size to the grid size to avoid any surprise when the
  // monitor height isn't divide-able by our alignment factor.
  default_width -= default_width % kDesktopBorderSize;
  default_height -= default_height % kDesktopBorderSize;
  int offset_x = kDesktopBorderSize;
  if (default_width > kMaximumWindowWidth) {
    // The window should get centered on the screen and not follow the grid.
    offset_x = (work_area.width() - kMaximumWindowWidth) / 2;
    default_width = kMaximumWindowWidth;
  }
  default_bounds->SetRect(work_area.x() + offset_x,
                          work_area.y() + kDesktopBorderSize,
                          default_width,
                          default_height);
}
