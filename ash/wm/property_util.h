// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_WM_PROPERTY_UTIL_H_
#define ASH_WM_PROPERTY_UTIL_H_

#include "ash/ash_export.h"

namespace aura {
class RootWindow;
class Window;
}

namespace gfx {
class Rect;
}

namespace ash {
namespace internal {
class RootWindowController;
}

// Sets the restore bounds property on |window| in the virtual screen
// coordinates.  Deletes existing bounds value if exists.
ASH_EXPORT void SetRestoreBoundsInScreen(aura::Window* window,
                                         const gfx::Rect& screen_bounds);
// Same as |SetRestoreBoundsInScreen| except that the bounds is in the
// parent's coordinates.
ASH_EXPORT void SetRestoreBoundsInParent(aura::Window* window,
                                         const gfx::Rect& parent_bounds);

// Returns the restore bounds property on |window| in the virtual screen
// coordinates. The bounds can be NULL if the bounds property does not
// exist for |window|. |window| owns the bounds object.
ASH_EXPORT const gfx::Rect* GetRestoreBoundsInScreen(aura::Window* window);
// Same as |GetRestoreBoundsInScreen| except that it returns the
// bounds in the parent's coordinates.
ASH_EXPORT gfx::Rect GetRestoreBoundsInParent(aura::Window* window);

// Deletes and clears the restore bounds property on |window|.
ASH_EXPORT void ClearRestoreBounds(aura::Window* window);

// Sets whether |window| is ignored when determining whether the shelf should
// be darkened when overlapped.
ASH_EXPORT void SetIgnoredByShelf(aura::Window* window, bool value);
ASH_EXPORT bool GetIgnoredByShelf(const aura::Window* window);

// Sets whether |window| should always be restored to the restore bounds
// (sometimes the workspace layout manager restores the window to its original
// bounds instead of the restore bounds. Setting this key overrides that
// behaviour). The flag is reset to the default value after the window is
// restored.
ASH_EXPORT void SetWindowAlwaysRestoresToRestoreBounds(aura::Window* window,
                                                       bool value);
ASH_EXPORT bool GetWindowAlwaysRestoresToRestoreBounds(
    const aura::Window* window);

// Sets whether the specified window is tracked by workspace code. Default is
// true. If set to false the workspace does not switch the current workspace,
// nor does it attempt to impose constraints on the bounds of the window. This
// is intended for tab dragging.
ASH_EXPORT void SetTrackedByWorkspace(aura::Window* window, bool value);
ASH_EXPORT bool GetTrackedByWorkspace(const aura::Window* window);

// Sets the default value for whether windows persist across all workspaces.
// The default is false.
ASH_EXPORT void SetDefaultPersistsAcrossAllWorkspaces(bool value);

// Sets/Gets the RootWindowController for |root_window|.
ASH_EXPORT void SetRootWindowController(
    aura::RootWindow* root_window,
    internal::RootWindowController* controller);
ASH_EXPORT internal::RootWindowController* GetRootWindowController(
    const aura::RootWindow* root_window);

}

#endif  // ASH_WM_PROPERTY_UTIL_H_
