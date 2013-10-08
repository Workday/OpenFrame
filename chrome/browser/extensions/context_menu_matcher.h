// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_CONTEXT_MENU_MATCHER_H_
#define CHROME_BROWSER_EXTENSIONS_CONTEXT_MENU_MATCHER_H_

#include <map>

#include "base/callback.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/scoped_vector.h"
#include "chrome/browser/extensions/menu_manager.h"
#include "ui/base/models/simple_menu_model.h"

class ExtensionContextMenuBrowserTest;
class Profile;

namespace extensions {

// This class contains code that is shared between the various places where
// context menu items added by the extension or app should be shown.
class ContextMenuMatcher {
 public:
  static const size_t kMaxExtensionItemTitleLength;

  // The |filter| will be called on possibly matching menu items, and its
  // result is used to determine which items to actually append to the menu.
  ContextMenuMatcher(Profile* profile,
                     ui::SimpleMenuModel::Delegate* delegate,
                     ui::SimpleMenuModel* menu_model,
                     const base::Callback<bool(const MenuItem*)>& filter);

  // This is a helper function to append items for one particular extension.
  // The |index| parameter is used for assigning id's, and is incremented for
  // each item actually added.
  void AppendExtensionItems(const std::string& extension_id,
                            const string16& selection_text,
                            int* index);

  void Clear();

  // This function returns the top level context menu title of an extension
  // based on a printable selection text.
  base::string16 GetTopLevelContextMenuTitle(const std::string& extension_id,
                                             const string16& selection_text);

  bool IsCommandIdChecked(int command_id) const;
  bool IsCommandIdEnabled(int command_id) const;
  void ExecuteCommand(int command_id,
                      content::WebContents* web_contents,
                      const content::ContextMenuParams& params);

 private:
  friend class ::ExtensionContextMenuBrowserTest;

  bool GetRelevantExtensionTopLevelItems(
      const std::string& extension_id,
      const Extension** extension,
      bool* can_cross_incognito,
      MenuItem::List& items);

  MenuItem::List GetRelevantExtensionItems(
      const MenuItem::List& items,
      bool can_cross_incognito);

  // Used for recursively adding submenus of extension items.
  void RecursivelyAppendExtensionItems(const MenuItem::List& items,
                                       bool can_cross_incognito,
                                       const string16& selection_text,
                                       ui::SimpleMenuModel* menu_model,
                                       int* index);

  // Attempts to get an MenuItem given the id of a context menu item.
  extensions::MenuItem* GetExtensionMenuItem(int id) const;

  // This will set the icon on the most recently-added item in the menu_model_.
  void SetExtensionIcon(const std::string& extension_id);

  Profile* profile_;
  ui::SimpleMenuModel* menu_model_;
  ui::SimpleMenuModel::Delegate* delegate_;

  base::Callback<bool(const MenuItem*)> filter_;

  // Maps the id from a context menu item to the MenuItem's internal id.
  std::map<int, extensions::MenuItem::Id> extension_item_map_;

  // Keep track of and clean up menu models for submenus.
  ScopedVector<ui::SimpleMenuModel> extension_menu_models_;

  DISALLOW_COPY_AND_ASSIGN(ContextMenuMatcher);
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_CONTEXT_MENU_MATCHER_H_
