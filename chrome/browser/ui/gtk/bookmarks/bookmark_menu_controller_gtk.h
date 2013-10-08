// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_GTK_BOOKMARKS_BOOKMARK_MENU_CONTROLLER_GTK_H_
#define CHROME_BROWSER_UI_GTK_BOOKMARKS_BOOKMARK_MENU_CONTROLLER_GTK_H_

#include <map>
#include <vector>

#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/bookmarks/base_bookmark_model_observer.h"
#include "chrome/browser/ui/bookmarks/bookmark_context_menu_controller.h"
#include "ui/base/glib/glib_integers.h"
#include "ui/base/gtk/gtk_signal.h"
#include "ui/base/gtk/gtk_signal_registrar.h"
#include "ui/base/gtk/owned_widget_gtk.h"
#include "ui/base/window_open_disposition.h"

class Browser;
class BookmarkModel;
class BookmarkNode;
class MenuGtk;

namespace content {
class PageNavigator;
}

typedef struct _GdkDragContext GdkDragContext;
typedef struct _GdkEventButton GdkEventButton;
typedef struct _GtkSelectionData GtkSelectionData;
typedef struct _GtkWidget GtkWidget;

class BookmarkMenuController : public BaseBookmarkModelObserver,
                               public BookmarkContextMenuControllerDelegate {
 public:
  // Creates a BookmarkMenuController showing the children of |node| starting
  // at index |start_child_index|.
  BookmarkMenuController(Browser* browser,
                         content::PageNavigator* page_navigator,
                         GtkWindow* window,
                         const BookmarkNode* node,
                         int start_child_index);
  virtual ~BookmarkMenuController();

  GtkWidget* widget() { return menu_; }

  // Pops up the menu. |widget| must be a GtkChromeButton.
  void Popup(GtkWidget* widget, gint button_type, guint32 timestamp);

  // Overridden from BaseBookmarkModelObserver:
  virtual void BookmarkModelChanged() OVERRIDE;
  virtual void BookmarkNodeFaviconChanged(BookmarkModel* model,
                                          const BookmarkNode* node) OVERRIDE;

  // Overridden from BookmarkContextMenuController::Delegate:
  virtual void WillExecuteCommand(
      int command_id,
      const std::vector<const BookmarkNode*>& bookmarks) OVERRIDE;
  virtual void CloseMenu() OVERRIDE;

 private:
  // Recursively change the bookmark hierarchy rooted in |parent| into a set of
  // gtk menus rooted in |menu|.
  void BuildMenu(const BookmarkNode* parent,
                 int start_child_index,
                 GtkWidget* menu);

  // Calls the page navigator to navigate to the node represented by
  // |menu_item|.
  void NavigateToMenuItem(GtkWidget* menu_item,
                          WindowOpenDisposition disposition);

  // Button press and release events for a GtkMenu.
  CHROMEGTK_CALLBACK_1(BookmarkMenuController, gboolean,
                       OnMenuButtonPressedOrReleased, GdkEventButton*);

  // Button release event for a GtkMenuItem.
  CHROMEGTK_CALLBACK_1(BookmarkMenuController, gboolean, OnButtonReleased,
                       GdkEventButton*);

  // We connect this handler to the button-press-event signal for folder nodes.
  // It suppresses the normal behavior (popping up the submenu) to allow these
  // nodes to be draggable. The submenu is instead popped up on a
  // button-release-event.
  CHROMEGTK_CALLBACK_1(BookmarkMenuController, gboolean, OnFolderButtonPressed,
                       GdkEventButton*);

  // We have to stop drawing |triggering_widget_| as active when the menu
  // closes.
  CHROMEGTK_CALLBACK_0(BookmarkMenuController, void, OnMenuHidden)

  // We respond to the activate signal because things other than mouse button
  // events can trigger it.
  CHROMEGTK_CALLBACK_0(BookmarkMenuController, void, OnMenuItemActivated);

  // The individual GtkMenuItems in the BookmarkMenu are all drag sources.
  CHROMEGTK_CALLBACK_1(BookmarkMenuController, void, OnMenuItemDragBegin,
                       GdkDragContext*);
  CHROMEGTK_CALLBACK_1(BookmarkMenuController, void, OnMenuItemDragEnd,
                       GdkDragContext*);
  CHROMEGTK_CALLBACK_4(BookmarkMenuController, void, OnMenuItemDragGet,
                       GdkDragContext*, GtkSelectionData*, guint, guint);

  Browser* browser_;
  content::PageNavigator* page_navigator_;

  // Parent window of this menu.
  GtkWindow* parent_window_;

  // The bookmark model.
  BookmarkModel* model_;

  // The node we're showing the contents of.
  const BookmarkNode* node_;

  // Our bookmark menus. We don't use the MenuGtk class because we have to do
  // all sorts of weird non-standard things with this menu, like:
  // - The menu is a drag target
  // - The menu items have context menus.
  GtkWidget* menu_;

  // The visual representation that follows the cursor during drags.
  GtkWidget* drag_icon_;

  // Whether we should ignore the next button release event (because we were
  // dragging).
  bool ignore_button_release_;

  // The widget we are showing for (i.e. the bookmark bar folder button).
  GtkWidget* triggering_widget_;

  // Mapping from node to GtkMenuItem menu id. This only contains entries for
  // nodes of type URL.
  std::map<const BookmarkNode*, GtkWidget*> node_to_menu_widget_map_;

  // The controller and view for the right click context menu.
  scoped_ptr<BookmarkContextMenuController> context_menu_controller_;
  scoped_ptr<MenuGtk> context_menu_;

  ui::GtkSignalRegistrar signals_;

  DISALLOW_COPY_AND_ASSIGN(BookmarkMenuController);
};

#endif  // CHROME_BROWSER_UI_GTK_BOOKMARKS_BOOKMARK_MENU_CONTROLLER_GTK_H_
