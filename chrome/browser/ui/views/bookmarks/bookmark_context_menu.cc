// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/bookmarks/bookmark_context_menu.h"

#include "base/i18n/rtl.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/chrome_notification_types.h"
#include "content/public/browser/notification_service.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/widget/widget.h"

using content::PageNavigator;

namespace {

// Returns true if |command_id| corresponds to a command that causes one or more
// bookmarks to be removed.
bool IsRemoveBookmarksCommand(int command_id) {
  return command_id == IDC_CUT || command_id == IDC_BOOKMARK_BAR_REMOVE;
}

}  // namespace

////////////////////////////////////////////////////////////////////////////////
// BookmarkContextMenu, public:

BookmarkContextMenu::BookmarkContextMenu(
    views::Widget* parent_widget,
    Browser* browser,
    Profile* profile,
    PageNavigator* page_navigator,
    const BookmarkNode* parent,
    const std::vector<const BookmarkNode*>& selection,
    bool close_on_remove)
    : controller_(new BookmarkContextMenuController(
          parent_widget ? parent_widget->GetNativeWindow() : NULL, this,
          browser, profile, page_navigator, parent, selection)),
      parent_widget_(parent_widget),
      menu_(new views::MenuItemView(this)),
      menu_runner_(new views::MenuRunner(menu_)),
      parent_node_(parent),
      observer_(NULL),
      close_on_remove_(close_on_remove) {

  ui::SimpleMenuModel* menu_model = controller_->menu_model();
  for (int i = 0; i < menu_model->GetItemCount(); ++i) {
    menu_->AppendMenuItemFromModel(
        menu_model, i, menu_model->GetCommandIdAt(i));
  }
}

BookmarkContextMenu::~BookmarkContextMenu() {
}

void BookmarkContextMenu::RunMenuAt(const gfx::Point& point,
                                    ui::MenuSourceType source_type) {
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_BOOKMARK_CONTEXT_MENU_SHOWN,
      content::Source<BookmarkContextMenu>(this),
      content::NotificationService::NoDetails());
  // width/height don't matter here.
  if (menu_runner_->RunMenuAt(
          parent_widget_, NULL, gfx::Rect(point.x(), point.y(), 0, 0),
          views::MenuItemView::TOPLEFT, source_type,
          (views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::IS_NESTED |
           views::MenuRunner::CONTEXT_MENU)) ==
      views::MenuRunner::MENU_DELETED)
    return;
}

void BookmarkContextMenu::SetPageNavigator(PageNavigator* navigator) {
  controller_->set_navigator(navigator);
}

////////////////////////////////////////////////////////////////////////////////
// BookmarkContextMenu, views::MenuDelegate implementation:

void BookmarkContextMenu::ExecuteCommand(int command_id, int event_flags) {
  controller_->ExecuteCommand(command_id, event_flags);
}

bool BookmarkContextMenu::IsItemChecked(int command_id) const {
  return controller_->IsCommandIdChecked(command_id);
}

bool BookmarkContextMenu::IsCommandEnabled(int command_id) const {
  return controller_->IsCommandIdEnabled(command_id);
}

bool BookmarkContextMenu::ShouldCloseAllMenusOnExecute(int id) {
  return (id != IDC_BOOKMARK_BAR_REMOVE) || close_on_remove_;
}

////////////////////////////////////////////////////////////////////////////////
// BookmarkContextMenuControllerDelegate
// implementation:

void BookmarkContextMenu::CloseMenu() {
  menu_->Cancel();
}

void BookmarkContextMenu::WillExecuteCommand(
    int command_id,
    const std::vector<const BookmarkNode*>& bookmarks) {
  if (observer_ && IsRemoveBookmarksCommand(command_id))
    observer_->WillRemoveBookmarks(bookmarks);
}

void BookmarkContextMenu::DidExecuteCommand(int command_id) {
  if (observer_ && IsRemoveBookmarksCommand(command_id))
    observer_->DidRemoveBookmarks();
}
