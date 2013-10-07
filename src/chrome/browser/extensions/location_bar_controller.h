// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_LOCATION_BAR_CONTROLLER_H_
#define CHROME_BROWSER_EXTENSIONS_LOCATION_BAR_CONTROLLER_H_

#include <set>
#include <string>
#include <vector>

class ExtensionAction;

namespace extensions {

// Interface for a class that controls the the extension icons that show up in
// the location bar. Depending on switches, these icons can have differing
// behavior.
class LocationBarController {
 public:
  // The reaction that the UI should take after executing |OnClicked|.
  enum Action {
    ACTION_NONE,
    ACTION_SHOW_POPUP,
    ACTION_SHOW_CONTEXT_MENU,
    ACTION_SHOW_SCRIPT_POPUP,
  };

  virtual ~LocationBarController() {}

  // Gets the action data for all extensions.
  virtual std::vector<ExtensionAction*> GetCurrentActions() const = 0;

  // Invites the user to click on |extension_id|'s script badge, due to a
  // scriptBadge.getAttention() call.
  virtual void GetAttentionFor(const std::string& extension_id) = 0;

  // Notifies this that the badge for an extension has been clicked with some
  // mouse button (1 for left, 2 for middle, and 3 for right click), and
  // returns the action that should be taken in response (if any).
  // TODO(kalman): make mouse_button an enum.
  virtual Action OnClicked(const std::string& extension_id,
                           int mouse_button) = 0;

  // Notifies clients that the icons have changed.
  virtual void NotifyChange() = 0;
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_LOCATION_BAR_CONTROLLER_H_
