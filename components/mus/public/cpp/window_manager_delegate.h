// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_MUS_PUBLIC_CPP_WINDOW_MANAGER_DELEGATE_H_
#define COMPONENTS_MUS_PUBLIC_CPP_WINDOW_MANAGER_DELEGATE_H_

#include <vector>

#include "base/memory/scoped_ptr.h"

namespace gfx {
class Rect;
}

namespace mus {

class Window;

// Used by clients implementing a window manager.
// TODO(sky): this should be called WindowManager, but that's rather confusing
// currently.
class WindowManagerDelegate {
 public:
  // A client requested the bounds of |window| to change to |bounds|. Return
  // true if the bounds are allowed to change. A return value of false
  // indicates the change is not allowed.
  // NOTE: This should not change the bounds of |window|. Instead return the
  // bounds the window should be in |bounds|.
  virtual bool OnWmSetBounds(Window* window, gfx::Rect* bounds) = 0;

  // A client requested the shared property named |name| to change to
  // |new_data|. Return true to allow the change to |new_data|, false
  // otherwise.
  virtual bool OnWmSetProperty(Window* window,
                               const std::string& name,
                               scoped_ptr<std::vector<uint8_t>>* new_data) = 0;

 protected:
  virtual ~WindowManagerDelegate() {}
};

}  // namespace mus

#endif  // COMPONENTS_MUS_PUBLIC_CPP_WINDOW_MANAGER_DELEGATE_H_
