// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/panels/detached_panel_drag_handler.h"

#include "base/logging.h"
#include "chrome/browser/ui/panels/panel.h"
#include "chrome/browser/ui/panels/panel_collection.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"

// static
void DetachedPanelDragHandler::HandleDrag(Panel* panel,
                                          const gfx::Point& target_position) {
  DCHECK_EQ(PanelCollection::DETACHED, panel->collection()->type());

  gfx::Rect new_bounds(panel->GetBounds());
  new_bounds.set_origin(target_position);
  panel->SetPanelBoundsInstantly(new_bounds);
}
