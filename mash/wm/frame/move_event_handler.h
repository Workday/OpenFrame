// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MASH_WM_FRAME_MOVE_EVENT_HANDLER_H_
#define MASH_WM_FRAME_MOVE_EVENT_HANDLER_H_

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "ui/events/event_handler.h"

namespace aura {
class Window;
}

namespace mus {
class Window;
}

namespace ui {
class LocatedEvent;
}

namespace mash {
namespace wm {

class MoveLoop;

// EventHandler attached to the root. Starts a MoveLoop as necessary.
class MoveEventHandler : public ui::EventHandler {
 public:
  MoveEventHandler(mus::Window* mus_window, aura::Window* aura_window);
  ~MoveEventHandler() override;

 private:
  void ProcessLocatedEvent(ui::LocatedEvent* event);
  int ShouldStartMoveLoop(const ui::LocatedEvent* event);

  // Overridden from ui::EventHandler:
  void OnMouseEvent(ui::MouseEvent* event) override;
  void OnTouchEvent(ui::TouchEvent* event) override;
  void OnCancelMode(ui::CancelModeEvent* event) override;

  mus::Window* mus_window_;
  aura::Window* aura_window_;
  scoped_ptr<MoveLoop> move_loop_;

  DISALLOW_COPY_AND_ASSIGN(MoveEventHandler);
};

}  // namespace wm
}  // namespace mash

#endif  // MASH_WM_FRAME_MOVE_EVENT_HANDLER_H_
