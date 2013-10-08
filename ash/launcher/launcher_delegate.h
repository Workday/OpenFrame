// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_LAUNCHER_LAUNCHER_DELEGATE_H_
#define ASH_LAUNCHER_LAUNCHER_DELEGATE_H_

#include "ash/ash_export.h"
#include "ash/launcher/launcher_types.h"
#include "base/strings/string16.h"
#include "ui/base/models/simple_menu_model.h"

namespace aura {
class RootWindow;
}

namespace ui {
class Event;
}

namespace ash {
class Launcher;

// A special menu model which keeps track of an "active" menu item.
class ASH_EXPORT LauncherMenuModel : public ui::SimpleMenuModel {
 public:
  explicit LauncherMenuModel(ui::SimpleMenuModel::Delegate* delegate)
      : ui::SimpleMenuModel(delegate) {}

  // Returns |true| when the given |command_id| is active and needs to be drawn
  // in a special state.
  virtual bool IsCommandActive(int command_id) const = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(LauncherMenuModel);
};

// Delegate for the Launcher.
class ASH_EXPORT LauncherDelegate {
 public:
  // Launcher owns the delegate.
  virtual ~LauncherDelegate() {}

  // Invoked when the user clicks on a window entry in the launcher.
  // |event| is the click event. The |event| is dispatched by a view
  // and has an instance of |views::View| as the event target
  // but not |aura::Window|. If the |event| is of type KeyEvent, it is assumed
  // that this was triggered by keyboard action (Alt+<number>) and special
  // handling might happen (PerApp launcher).
  virtual void ItemSelected(const LauncherItem& item,
                            const ui::Event& event) = 0;

  // Returns the title to display for the specified launcher item.
  virtual base::string16 GetTitle(const LauncherItem& item) = 0;

  // Returns the context menumodel for the specified item on
  // |root_window|.  Return NULL if there should be no context
  // menu. The caller takes ownership of the returned model.
  virtual ui::MenuModel* CreateContextMenu(const LauncherItem& item,
                                           aura::RootWindow* root_window) = 0;

  // Returns the application menu model for the specified item. There are three
  // possible return values:
  //  - A return of NULL indicates that no menu is wanted for this item.
  //  - A return of a menu with one item means that only the name of the
  //    application/item was added and there are no active applications.
  //    Note: This is useful for hover menus which also show context help.
  //  - A list containing the title and the active list of items.
  // The caller takes ownership of the returned model.
  // |event_flags| specifies the flags of the event which triggered this menu.
  virtual LauncherMenuModel* CreateApplicationMenu(
      const LauncherItem& item,
      int event_flags) = 0;

  // Returns the id of the item associated with the specified window, or 0 if
  // there isn't one.
  virtual LauncherID GetIDByWindow(aura::Window* window) = 0;

  // Whether the given launcher item is draggable.
  virtual bool IsDraggable(const LauncherItem& item) = 0;

  // Returns true if a tooltip should be shown for the item.
  virtual bool ShouldShowTooltip(const LauncherItem& item) = 0;

  // Callback used to allow delegate to perform initialization actions that
  // depend on the Launcher being in a known state.
  virtual void OnLauncherCreated(Launcher* launcher) = 0;

  // Callback used to inform the delegate that a specific launcher no longer
  // exists.
  virtual void OnLauncherDestroyed(Launcher* launcher) = 0;

  // True if the running launcher is the per application launcher.
  virtual bool IsPerAppLauncher() = 0;

  // Get the launcher ID from an application ID.
  virtual LauncherID GetLauncherIDForAppID(const std::string& app_id) = 0;

  // Pins an app with |app_id| to launcher. A running instance will get pinned.
  // In case there is no running instance a new launcher item is created and
  // pinned.
  virtual void PinAppWithID(const std::string& app_id) = 0;

  // Check if the app with |app_id_| is pinned to the launcher.
  virtual bool IsAppPinned(const std::string& app_id) = 0;

  // Unpins any app item(s) whose id is |app_id|. The new launcher will collect
  // all items under one item, the old launcher might have multiple items.
  virtual void UnpinAppsWithID(const std::string& app_id) = 0;
};

}  // namespace ash

#endif  // ASH_LAUNCHER_LAUNCHER_DELEGATE_H_
