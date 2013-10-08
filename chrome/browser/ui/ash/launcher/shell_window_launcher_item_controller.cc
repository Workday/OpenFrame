// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ash/launcher/shell_window_launcher_item_controller.h"

#include "apps/native_app_window.h"
#include "apps/shell_window.h"
#include "ash/wm/window_properties.h"
#include "ash/wm/window_util.h"
#include "chrome/browser/ui/ash/launcher/chrome_launcher_app_menu_item.h"
#include "chrome/browser/ui/ash/launcher/chrome_launcher_app_menu_item_v2app.h"
#include "chrome/browser/ui/ash/launcher/chrome_launcher_controller.h"
#include "chrome/browser/ui/ash/launcher/chrome_launcher_controller_per_app.h"
#include "chrome/browser/ui/ash/launcher/launcher_item_controller.h"
#include "content/public/browser/web_contents.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/window.h"
#include "ui/base/events/event.h"
#include "ui/views/corewm/window_animations.h"

using apps::ShellWindow;

namespace {

// Functor for std::find_if used in AppLauncherItemController.
class ShellWindowHasWindow {
 public:
  explicit ShellWindowHasWindow(aura::Window* window) : window_(window) { }

  bool operator()(ShellWindow* shell_window) const {
    return shell_window->GetNativeWindow() == window_;
  }

 private:
  const aura::Window* window_;
};

}  // namespace

ShellWindowLauncherItemController::ShellWindowLauncherItemController(
    Type type,
    const std::string& app_launcher_id,
    const std::string& app_id,
    ChromeLauncherController* controller)
    : LauncherItemController(type, app_id, controller),
      last_active_shell_window_(NULL),
      app_launcher_id_(app_launcher_id),
      observed_windows_(this) {
}

ShellWindowLauncherItemController::~ShellWindowLauncherItemController() {
}

void ShellWindowLauncherItemController::AddShellWindow(
    ShellWindow* shell_window,
    ash::LauncherItemStatus status) {
  if (shell_window->window_type_is_panel() && type() != TYPE_APP_PANEL)
    LOG(ERROR) << "ShellWindow of type Panel added to non-panel launcher item";
  shell_windows_.push_front(shell_window);
  observed_windows_.Add(shell_window->GetNativeWindow());
}

void ShellWindowLauncherItemController::RemoveShellWindowForWindow(
    aura::Window* window) {
  ShellWindowList::iterator iter =
      std::find_if(shell_windows_.begin(), shell_windows_.end(),
                   ShellWindowHasWindow(window));
  if (iter != shell_windows_.end()) {
    if (*iter == last_active_shell_window_)
      last_active_shell_window_ = NULL;
    shell_windows_.erase(iter);
  }
  observed_windows_.Remove(window);
}

void ShellWindowLauncherItemController::SetActiveWindow(aura::Window* window) {
  ShellWindowList::iterator iter =
      std::find_if(shell_windows_.begin(), shell_windows_.end(),
                   ShellWindowHasWindow(window));
  if (iter != shell_windows_.end())
    last_active_shell_window_ = *iter;
}

string16 ShellWindowLauncherItemController::GetTitle() {
  // For panels return the title of the contents if set.
  // Otherwise return the title of the app.
  if (type() == TYPE_APP_PANEL && !shell_windows_.empty()) {
    ShellWindow* shell_window = shell_windows_.front();
    if (shell_window->web_contents()) {
      string16 title = shell_window->web_contents()->GetTitle();
      if (!title.empty())
        return title;
    }
  }
  return GetAppTitle();
}

bool ShellWindowLauncherItemController::HasWindow(
    aura::Window* window) const {
  ShellWindowList::const_iterator iter =
      std::find_if(shell_windows_.begin(), shell_windows_.end(),
                   ShellWindowHasWindow(window));
  return iter != shell_windows_.end();
}

bool ShellWindowLauncherItemController::IsOpen() const {
  return !shell_windows_.empty();
}

bool ShellWindowLauncherItemController::IsVisible() const {
  // Return true if any windows are visible.
  for (ShellWindowList::const_iterator iter = shell_windows_.begin();
       iter != shell_windows_.end(); ++iter) {
    if ((*iter)->GetNativeWindow()->IsVisible())
      return true;
  }
  return false;
}

void ShellWindowLauncherItemController::Launch(
    int event_flags) {
  launcher_controller()->LaunchApp(app_id(), ui::EF_NONE);
}

void ShellWindowLauncherItemController::Activate() {
  DCHECK(!shell_windows_.empty());
  ShellWindow* window_to_activate = last_active_shell_window_ ?
      last_active_shell_window_ : shell_windows_.back();
  window_to_activate->GetBaseWindow()->Activate();
}

void ShellWindowLauncherItemController::Close() {
  // Note: Closing windows may affect the contents of shell_windows_.
  ShellWindowList windows_to_close = shell_windows_;
  for (ShellWindowList::iterator iter = windows_to_close.begin();
       iter != windows_to_close.end(); ++iter) {
    (*iter)->GetBaseWindow()->Close();
  }
}

void ShellWindowLauncherItemController::Clicked(const ui::Event& event) {
  if (shell_windows_.empty())
    return;
  if (type() == TYPE_APP_PANEL) {
    DCHECK(shell_windows_.size() == 1);
    ShellWindow* panel = shell_windows_.front();
    aura::Window* panel_window = panel->GetNativeWindow();
    // If the panel is attached on another display, move it to the current
    // display and activate it.
    if (panel_window->GetProperty(ash::internal::kPanelAttachedKey) &&
        ash::wm::MoveWindowToEventRoot(panel_window, event)) {
      if (!panel->GetBaseWindow()->IsActive())
        ShowAndActivateOrMinimize(panel);
    } else {
      ShowAndActivateOrMinimize(panel);
    }
  } else if (launcher_controller()->GetPerAppInterface() ||
      shell_windows_.size() == 1) {
    ShellWindow* window_to_show = last_active_shell_window_ ?
        last_active_shell_window_ : shell_windows_.front();
    // If the event was triggered by a keystroke, we try to advance to the next
    // item if the window we are trying to activate is already active.
    if (shell_windows_.size() >= 1 &&
        window_to_show->GetBaseWindow()->IsActive() &&
        event.type() == ui::ET_KEY_RELEASED) {
      ActivateOrAdvanceToNextShellWindow(window_to_show);
    } else {
      ShowAndActivateOrMinimize(window_to_show);
    }
  } else {
    // TODO(stevenjb): Deprecate
    if (!last_active_shell_window_ ||
        last_active_shell_window_->GetBaseWindow()->IsActive()) {
      // Restore all windows since there is no other way to restore them.
      for (ShellWindowList::iterator iter = shell_windows_.begin();
           iter != shell_windows_.end(); ++iter) {
        ShellWindow* shell_window = *iter;
        if (shell_window->GetBaseWindow()->IsMinimized())
          shell_window->GetBaseWindow()->Restore();
      }
    }
    if (last_active_shell_window_)
      ShowAndActivateOrMinimize(last_active_shell_window_);
  }
}

void ShellWindowLauncherItemController::ActivateIndexedApp(size_t index) {
  if (index >= shell_windows_.size())
    return;
  ShellWindowList::iterator it = shell_windows_.begin();
  std::advance(it, index);
  ShowAndActivateOrMinimize(*it);
}

ChromeLauncherAppMenuItems
ShellWindowLauncherItemController::GetApplicationList(int event_flags) {
  ChromeLauncherAppMenuItems items;
  items.push_back(new ChromeLauncherAppMenuItem(GetTitle(), NULL, false));
  int index = 0;
  for (ShellWindowList::iterator iter = shell_windows_.begin();
       iter != shell_windows_.end(); ++iter) {
    ShellWindow* shell_window = *iter;
    scoped_ptr<gfx::Image> image(shell_window->GetAppListIcon());
    items.push_back(new ChromeLauncherAppMenuItemV2App(
        shell_window->GetTitle(),
        image.get(),  // Will be copied
        app_id(),
        launcher_controller()->GetPerAppInterface(),
        index,
        index == 0 /* has_leading_separator */));
    ++index;
  }
  return items.Pass();
}

void ShellWindowLauncherItemController::OnWindowPropertyChanged(
    aura::Window* window,
    const void* key,
    intptr_t old) {
  if (key == aura::client::kDrawAttentionKey) {
    ash::LauncherItemStatus status;
    if (ash::wm::IsActiveWindow(window)) {
      status = ash::STATUS_ACTIVE;
    } else if (window->GetProperty(aura::client::kDrawAttentionKey)) {
      status = ash::STATUS_ATTENTION;
    } else {
      status = ash::STATUS_RUNNING;
    }
    launcher_controller()->SetItemStatus(launcher_id(), status);
  }
}

void ShellWindowLauncherItemController::ShowAndActivateOrMinimize(
    ShellWindow* shell_window) {
  // Either show or minimize windows when shown from the launcher.
  launcher_controller()->ActivateWindowOrMinimizeIfActive(
      shell_window->GetBaseWindow(),
      GetApplicationList(0).size() == 2);
}

void ShellWindowLauncherItemController::ActivateOrAdvanceToNextShellWindow(
    ShellWindow* window_to_show) {
  ShellWindowList::iterator i(
      std::find(shell_windows_.begin(),
                shell_windows_.end(),
                window_to_show));
  if (i != shell_windows_.end()) {
    if (++i != shell_windows_.end())
      window_to_show = *i;
    else
      window_to_show = shell_windows_.front();
  }
  if (window_to_show->GetBaseWindow()->IsActive()) {
    // Coming here, only a single window is active. For keyboard activations
    // the window gets animated.
    AnimateWindow(window_to_show->GetNativeWindow(),
                  views::corewm::WINDOW_ANIMATION_TYPE_BOUNCE);
  } else {
    ShowAndActivateOrMinimize(window_to_show);
  }
}
