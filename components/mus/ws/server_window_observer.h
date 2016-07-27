// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_MUS_WS_SERVER_WINDOW_OBSERVER_H_
#define COMPONENTS_MUS_WS_SERVER_WINDOW_OBSERVER_H_

#include "components/mus/public/interfaces/mus_constants.mojom.h"

namespace gfx {
class Insets;
class Rect;
}

namespace ui {
struct TextInputState;
}

namespace mus {

namespace ws {

class ServerWindow;

// TODO(sky): rename to OnDid and OnWill everywhere.
class ServerWindowObserver {
 public:
  // Invoked when a window is about to be destroyed; before any of the children
  // have been removed and before the window has been removed from its parent.
  virtual void OnWillDestroyWindow(ServerWindow* window) {}

  // Invoked at the end of the window's destructor (after it has been removed
  // from the hierarchy.
  virtual void OnWindowDestroyed(ServerWindow* window) {}

  virtual void OnWillChangeWindowHierarchy(ServerWindow* window,
                                           ServerWindow* new_parent,
                                           ServerWindow* old_parent) {}

  virtual void OnWindowHierarchyChanged(ServerWindow* window,
                                        ServerWindow* new_parent,
                                        ServerWindow* old_parent) {}

  virtual void OnWindowBoundsChanged(ServerWindow* window,
                                     const gfx::Rect& old_bounds,
                                     const gfx::Rect& new_bounds) {}

  virtual void OnWindowClientAreaChanged(ServerWindow* window,
                                         const gfx::Insets& old_client_area,
                                         const gfx::Insets& new_client_area) {}

  virtual void OnWindowReordered(ServerWindow* window,
                                 ServerWindow* relative,
                                 mojom::OrderDirection direction) {}

  virtual void OnWillChangeWindowVisibility(ServerWindow* window) {}
  virtual void OnWindowVisibilityChanged(ServerWindow* window) {}

  virtual void OnWindowTextInputStateChanged(ServerWindow* window,
                                             const ui::TextInputState& state) {}

  virtual void OnWindowSharedPropertyChanged(
      ServerWindow* window,
      const std::string& name,
      const std::vector<uint8_t>* new_data) {}

  // Called when a transient child is added to |window|.
  virtual void OnTransientWindowAdded(ServerWindow* window,
                                      ServerWindow* transient_child) {}

  // Called when a transient child is removed from |window|.
  virtual void OnTransientWindowRemoved(ServerWindow* window,
                                        ServerWindow* transient_child) {}

 protected:
  virtual ~ServerWindowObserver() {}
};

}  // namespace ws

}  // namespace mus

#endif  // COMPONENTS_MUS_WS_SERVER_WINDOW_OBSERVER_H_
