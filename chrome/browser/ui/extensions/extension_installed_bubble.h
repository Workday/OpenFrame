// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_EXTENSIONS_EXTENSION_INSTALLED_BUBBLE_H_
#define CHROME_BROWSER_UI_EXTENSIONS_EXTENSION_INSTALLED_BUBBLE_H_

#include <string>

#include "base/strings/string16.h"
#include "components/bubble/bubble_delegate.h"
#include "third_party/skia/include/core/SkBitmap.h"

class Browser;

namespace extensions {
class Command;
class Extension;
}

// Provides feedback to the user upon successful installation of an
// extension. Depending on the type of extension, the Bubble will
// point to:
//    OMNIBOX_KEYWORD-> The omnibox.
//    BROWSER_ACTION -> The browser action icon in the toolbar.
//    PAGE_ACTION    -> A preview of the page action icon in the location
//                      bar which is shown while the Bubble is shown.
//    GENERIC        -> The app menu. This case includes page actions that
//                      don't specify a default icon.
class ExtensionInstalledBubble : public BubbleDelegate {
 public:
  // The behavior and content of this Bubble comes in these varieties:
  enum BubbleType {
    OMNIBOX_KEYWORD,
    BROWSER_ACTION,
    PAGE_ACTION,
    GENERIC
  };

  // Creates the ExtensionInstalledBubble and schedules it to be shown once
  // the extension has loaded. |extension| is the installed extension. |browser|
  // is the browser window which will host the bubble. |icon| is the install
  // icon of the extension.
  static void ShowBubble(const extensions::Extension* extension,
                         Browser* browser,
                         const SkBitmap& icon);

  ExtensionInstalledBubble(const extensions::Extension* extension,
                           Browser* browser,
                           const SkBitmap& icon);

  ~ExtensionInstalledBubble() override;

  const extensions::Extension* extension() const { return extension_; }
  Browser* browser() { return browser_; }
  const Browser* browser() const { return browser_; }
  const SkBitmap& icon() const { return icon_; }
  BubbleType type() const { return type_; }
  bool has_command_keybinding() const { return action_command_; }

  // BubbleDelegate:
  scoped_ptr<BubbleUi> BuildBubbleUi() override;
  bool ShouldClose(BubbleCloseReason reason) const override;
  std::string GetName() const override;

  // Returns false if the bubble could not be shown immediately, because of an
  // animation (eg. adding a new browser action to the toolbar).
  // TODO(hcarmona): Detect animation in a platform-agnostic manner.
  bool ShouldShow();

  // Returns the string describing how to use the new extension.
  base::string16 GetHowToUseDescription() const;

  // Handle initialization with the extension.
  void OnExtensionLoaded();

 private:
  // |extension_| is NULL when we are deleted.
  const extensions::Extension* extension_;
  Browser* browser_;
  const SkBitmap icon_;
  BubbleType type_;

  // The command to execute the extension action, if one exists.
  scoped_ptr<extensions::Command> action_command_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionInstalledBubble);
};

#endif  // CHROME_BROWSER_UI_EXTENSIONS_EXTENSION_INSTALLED_BUBBLE_H_
