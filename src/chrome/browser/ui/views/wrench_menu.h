// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_WRENCH_MENU_H_
#define CHROME_BROWSER_UI_VIEWS_WRENCH_MENU_H_

#include <map>
#include <utility>

#include "base/memory/scoped_ptr.h"
#include "chrome/browser/bookmarks/base_bookmark_model_observer.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "ui/base/models/menu_model.h"
#include "ui/views/controls/menu/menu_delegate.h"

class BookmarkMenuDelegate;
class Browser;

namespace ui {
class NativeTheme;
}

namespace views {
class MenuButton;
struct MenuConfig;
class MenuItemView;
class MenuRunner;
class View;
}  // namespace views

// WrenchMenu adapts the WrenchMenuModel to view's menu related classes.
class WrenchMenu : public views::MenuDelegate,
                   public BaseBookmarkModelObserver,
                   public content::NotificationObserver {
 public:
  // TODO: remove |use_new_menu| and |supports_new_separators|.
  WrenchMenu(Browser* browser,
             bool use_new_menu,
             bool supports_new_separators);
  virtual ~WrenchMenu();

  void Init(ui::MenuModel* model);

  // Shows the menu relative to the specified view.
  void RunMenu(views::MenuButton* host);

  // Whether the menu is currently visible to the user.
  bool IsShowing();

  const views::MenuConfig& GetMenuConfig() const;

  bool use_new_menu() const { return use_new_menu_; }

  // MenuDelegate overrides:
  virtual const gfx::Font* GetLabelFont(int index) const OVERRIDE;
  virtual bool GetForegroundColor(int command_id,
                                  bool is_hovered,
                                  SkColor* override_color) const OVERRIDE;
  virtual string16 GetTooltipText(int id, const gfx::Point& p) const OVERRIDE;
  virtual bool IsTriggerableEvent(views::MenuItemView* menu,
                                  const ui::Event& e) OVERRIDE;
  virtual bool GetDropFormats(
      views::MenuItemView* menu,
      int* formats,
      std::set<ui::OSExchangeData::CustomFormat>* custom_formats) OVERRIDE;
  virtual bool AreDropTypesRequired(views::MenuItemView* menu) OVERRIDE;
  virtual bool CanDrop(views::MenuItemView* menu,
                       const ui::OSExchangeData& data) OVERRIDE;
  virtual int GetDropOperation(views::MenuItemView* item,
                               const ui::DropTargetEvent& event,
                               DropPosition* position) OVERRIDE;
  virtual int OnPerformDrop(views::MenuItemView* menu,
                            DropPosition position,
                            const ui::DropTargetEvent& event) OVERRIDE;
  virtual bool ShowContextMenu(views::MenuItemView* source,
                               int id,
                               const gfx::Point& p,
                               ui::MenuSourceType source_type) OVERRIDE;
  virtual bool CanDrag(views::MenuItemView* menu) OVERRIDE;
  virtual void WriteDragData(views::MenuItemView* sender,
                             ui::OSExchangeData* data) OVERRIDE;
  virtual int GetDragOperations(views::MenuItemView* sender) OVERRIDE;
  virtual int GetMaxWidthForMenu(views::MenuItemView* menu) OVERRIDE;
  virtual bool IsItemChecked(int id) const OVERRIDE;
  virtual bool IsCommandEnabled(int id) const OVERRIDE;
  virtual void ExecuteCommand(int id, int mouse_event_flags) OVERRIDE;
  virtual bool GetAccelerator(int id, ui::Accelerator* accelerator) OVERRIDE;
  virtual void WillShowMenu(views::MenuItemView* menu) OVERRIDE;
  virtual void WillHideMenu(views::MenuItemView* menu) OVERRIDE;

  // BaseBookmarkModelObserver overrides:
  virtual void BookmarkModelChanged() OVERRIDE;

  // content::NotificationObserver overrides:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

 private:
  class CutCopyPasteView;
  class RecentTabsMenuModelDelegate;
  class ZoomView;

  typedef std::pair<ui::MenuModel*,int> Entry;
  typedef std::map<int,Entry> IDToEntry;

  const ui::NativeTheme* GetNativeTheme() const;

  // Populates |parent| with all the child menus in |model|. Recursively invokes
  // |PopulateMenu| for any submenu. |next_id| is incremented for every menu
  // that is created.
  void PopulateMenu(views::MenuItemView* parent,
                    ui::MenuModel* model,
                    int* next_id);

  // Adds a new menu to |parent| to represent the MenuModel/index pair passed
  // in.
  // Fur button containing menu items a |height| override can be specified with
  // a number bigger then 0.
  views::MenuItemView* AppendMenuItem(views::MenuItemView* parent,
                                      ui::MenuModel* model,
                                      int index,
                                      ui::MenuModel::ItemType menu_type,
                                      int* next_id,
                                      int height);

  // Invoked from the cut/copy/paste menus. Cancels the current active menu and
  // activates the menu item in |model| at |index|.
  void CancelAndEvaluate(ui::MenuModel* model, int index);

  // Creates the bookmark menu if necessary. Does nothing if already created or
  // the bookmark model isn't loaded.
  void CreateBookmarkMenu();

  // Returns true if |id| identifies a bookmark menu item.
  bool is_bookmark_command(int id) const {
    return bookmark_menu_delegate_.get() && id >= first_bookmark_command_id_;
  }

  // Returns true if |id| identifies a recent tabs menu item.
  bool is_recent_tabs_command(int id) const {
    return (recent_tabs_menu_model_delegate_.get() &&
            id >= first_recent_tabs_command_id_ &&
            id <= last_recent_tabs_command_id_);
  }

  // The views menu. Owned by |menu_runner_|.
  views::MenuItemView* root_;

  scoped_ptr<views::MenuRunner> menu_runner_;

  // Maps from the ID as understood by MenuItemView to the model/index pair the
  // item came from.
  IDToEntry id_to_entry_;

  // Browser the menu is being shown for.
  Browser* browser_;

  // |CancelAndEvaluate| sets |selected_menu_model_| and |selected_index_|.
  // If |selected_menu_model_| is non-null after the menu completes
  // ActivatedAt is invoked. This is done so that ActivatedAt isn't invoked
  // while the message loop is nested.
  ui::MenuModel* selected_menu_model_;
  int selected_index_;

  // Used for managing the bookmark menu items.
  scoped_ptr<BookmarkMenuDelegate> bookmark_menu_delegate_;

  // Menu corresponding to IDC_BOOKMARKS_MENU.
  views::MenuItemView* bookmark_menu_;

  // Menu corresponding to IDC_FEEDBACK.
  views::MenuItemView* feedback_menu_item_;

  // Used for managing "Recent tabs" menu items.
  scoped_ptr<RecentTabsMenuModelDelegate> recent_tabs_menu_model_delegate_;

  // First ID to use for the items representing bookmarks in the bookmark menu.
  int first_bookmark_command_id_;

  // First/last IDs to use for the items of the recent tabs sub-menu.
  int first_recent_tabs_command_id_;
  int last_recent_tabs_command_id_;

  content::NotificationRegistrar registrar_;

  const bool use_new_menu_;

  const bool supports_new_separators_;

  DISALLOW_COPY_AND_ASSIGN(WrenchMenu);
};

#endif  // CHROME_BROWSER_UI_VIEWS_WRENCH_MENU_H_
