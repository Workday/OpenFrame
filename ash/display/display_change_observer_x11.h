// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_DISPLAY_DISPLAY_CHANGE_OBSERVER_X11_H
#define ASH_DISPLAY_DISPLAY_CHANGE_OBSERVER_X11_H

#include <X11/Xlib.h>

// Xlib.h defines RootWindow.
#undef RootWindow

#include "ash/ash_export.h"
#include "ash/shell_observer.h"
#include "base/basictypes.h"
#include "chromeos/display/output_configurator.h"

namespace ash {
namespace internal {

// An object that observes changes in display configuration and
// update DisplayManagers.
class DisplayChangeObserverX11
    : public chromeos::OutputConfigurator::StateController,
      public chromeos::OutputConfigurator::Observer,
      public ShellObserver {
 public:
  DisplayChangeObserverX11();
  virtual ~DisplayChangeObserverX11();

  // chromeos::OutputConfigurator::StateController overrides:
  virtual chromeos::OutputState GetStateForDisplayIds(
      const std::vector<int64>& outputs) const OVERRIDE;
  virtual bool GetResolutionForDisplayId(int64 display_id,
                                         int* width,
                                         int* height) const OVERRIDE;

  // Overriden from chromeos::OutputConfigurator::Observer:
  virtual void OnDisplayModeChanged() OVERRIDE;

  // Overriden from ShellObserver:
  virtual void OnAppTerminating() OVERRIDE;

 private:
  Display* xdisplay_;

  ::Window x_root_window_;

  int xrandr_event_base_;

  DISALLOW_COPY_AND_ASSIGN(DisplayChangeObserverX11);
};

}  // namespace internal
}  // namespace ash

#endif  // ASH_DISPLAY_AURA_DISPLAY_CHANGE_OBSERVER_X11_H
