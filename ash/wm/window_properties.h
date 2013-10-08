// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_WM_WINDOW_PROPERTIES_H_
#define ASH_WM_WINDOW_PROPERTIES_H_

#include "ash/ash_export.h"
#include "ash/wm/property_util.h"
#include "ui/aura/window.h"
#include "ui/base/ui_base_types.h"

namespace gfx {
class Rect;
}

namespace ash {
class FramePainter;
namespace internal {
class RootWindowController;

// Shell-specific window property keys.

// Alphabetical sort.

// A property key to indicate that an in progress drag should be continued
// after the window is reparented to another container.
extern const aura::WindowProperty<bool>* const kContinueDragAfterReparent;

// A property key to store display_id an aura::RootWindow is mapped to.
extern const aura::WindowProperty<int64>* const kDisplayIdKey;

// A property key to indicate whether there is any chrome at all that cannot be
// hidden when the window is fullscreen. This is unrelated to whether the full
// chrome can be revealed by hovering the mouse at the top of the screen.
ASH_EXPORT extern const aura::WindowProperty<bool>* const
    kFullscreenUsesMinimalChromeKey;

// A property key to disable the frame painter policy for solo windows.
// It is only available for root windows.
ASH_EXPORT extern const aura::WindowProperty<bool>* const
    kIgnoreSoloWindowFramePainterPolicy;

// True if the window is ignored by the shelf layout manager for purposes of
// darkening the shelf.
extern const aura::WindowProperty<bool>* const
    kIgnoredByShelfKey;

// True if this window is an attached panel.
ASH_EXPORT extern const aura::WindowProperty<bool>* const kPanelAttachedKey;

extern const aura::WindowProperty<RootWindowController*>* const
    kRootWindowControllerKey;

// RootWindow property to indicate if the window in the active workspace should
// use the transparent "solo-window" header style.
ASH_EXPORT extern const aura::WindowProperty<bool>* const
    kSoloWindowHeaderKey;

// If this is set to true, the window stays in the same root window
// even if the bounds outside of its root window is set.
// This is exported as it's used in the tests.
ASH_EXPORT extern const aura::WindowProperty<bool>* const
    kStayInSameRootWindowKey;

// A property key to remember if a windows position or size was changed by a
// user.
ASH_EXPORT extern const aura::WindowProperty<bool>* const
    kUserChangedWindowPositionOrSizeKey;

// A property to remember the window position which was set before the
// auto window position manager changed the window bounds, so that it can get
// restored when only this one window gets shown.
ASH_EXPORT extern const aura::WindowProperty<gfx::Rect*>* const
    kPreAutoManagedWindowBoundsKey;

// Property to tell if the container uses the screen coordinates.
extern const aura::WindowProperty<bool>* const kUsesScreenCoordinatesKey;

// A property key to remember if a windows position can be managed by the
// workspace manager or not.
ASH_EXPORT extern const aura::WindowProperty<bool>* const
    kWindowPositionManagedKey;

// A property key to tell the workspace layout manager to always restore the
// window to the restore-bounds (false by default).
extern const aura::WindowProperty<bool>* const kWindowRestoresToRestoreBounds;

// True if the window is controlled by the workspace manager.
extern const aura::WindowProperty<bool>* const
    kWindowTrackedByWorkspaceKey;

// Alphabetical sort.

}  // namespace internal
}  // namespace ash

#endif  // ASH_WM_WINDOW_PROPERTIES_H_
