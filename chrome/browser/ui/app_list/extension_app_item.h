// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_APP_LIST_EXTENSION_APP_ITEM_H_
#define CHROME_BROWSER_UI_APP_LIST_EXTENSION_APP_ITEM_H_

#include <string>

#include "base/memory/scoped_ptr.h"
#include "chrome/browser/extensions/extension_icon_image.h"
#include "chrome/browser/ui/app_list/app_context_menu_delegate.h"
#include "chrome/browser/ui/app_list/chrome_app_list_item.h"
#include "chrome/browser/ui/extensions/extension_enable_flow_delegate.h"
#include "sync/api/string_ordinal.h"
#include "ui/gfx/image/image_skia.h"

class AppListControllerDelegate;
class ExtensionEnableFlow;
class Profile;

namespace app_list {
class AppContextMenu;
}

namespace extensions {
class ContextMenuMatcher;
class Extension;
}

// ExtensionAppItem represents an extension app in app list.
class ExtensionAppItem : public ChromeAppListItem,
                         public extensions::IconImage::Observer,
                         public ExtensionEnableFlowDelegate,
                         public app_list::AppContextMenuDelegate {
 public:
  ExtensionAppItem(Profile* profile,
                   const std::string& extension_id,
                   AppListControllerDelegate* controller,
                   const std::string& extension_name,
                   const gfx::ImageSkia& installing_icon,
                   bool is_platform_app);
  virtual ~ExtensionAppItem();

  // Reload the title and icon from the underlying extension.
  void Reload();

  syncer::StringOrdinal GetPageOrdinal() const;
  syncer::StringOrdinal GetAppLaunchOrdinal() const;

  // Update page and app launcher ordinals to put the app in between |prev| and
  // |next|. Note that |prev| and |next| could be NULL when the app is put at
  // the beginning or at the end.
  void Move(const ExtensionAppItem* prev, const ExtensionAppItem* next);

  // Updates the app item's icon, if necessary adding an overlay and/or making
  // it gray.
  void UpdateIcon();

  const std::string& extension_id() const { return extension_id_; }

 private:
  // Gets extension associated with this model. Returns NULL if extension
  // no longer exists.
  const extensions::Extension* GetExtension() const;

  // Loads extension icon.
  void LoadImage(const extensions::Extension* extension);

  // Checks if extension is disabled and if enable flow should be started.
  // Returns true if extension enable flow is started or there is already one
  // running.
  bool RunExtensionEnableFlow();

  // Private equivalent to Activate(), without refocus for already-running apps.
  void Launch(int event_flags);

  // Whether or not the app item has an overlay.
  bool HasOverlay() const;

  // Overridden from extensions::IconImage::Observer:
  virtual void OnExtensionIconImageChanged(
      extensions::IconImage* image) OVERRIDE;

  // Overridden from ExtensionEnableFlowDelegate:
  virtual void ExtensionEnableFlowFinished() OVERRIDE;
  virtual void ExtensionEnableFlowAborted(bool user_initiated) OVERRIDE;

  // Overridden from ChromeAppListItem:
  virtual void Activate(int event_flags) OVERRIDE;
  virtual ui::MenuModel* GetContextMenuModel() OVERRIDE;

  // Overridden from app_list::AppContextMenuDelegate:
  virtual void ExecuteLaunchCommand(int event_flags) OVERRIDE;

  Profile* profile_;
  const std::string extension_id_;
  AppListControllerDelegate* controller_;

  scoped_ptr<extensions::IconImage> icon_;
  scoped_ptr<app_list::AppContextMenu> context_menu_;
  scoped_ptr<ExtensionEnableFlow> extension_enable_flow_;

  // Name to use for the extension if we can't access it.
  std::string extension_name_;

  // Icon for the extension if we can't access the installed extension.
  gfx::ImageSkia installing_icon_;

  // Whether or not this app is a platform app.
  bool is_platform_app_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionAppItem);
};

#endif  // CHROME_BROWSER_UI_APP_LIST_EXTENSION_APP_ITEM_H_
