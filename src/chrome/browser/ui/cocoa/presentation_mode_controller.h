// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_PRESENTATION_MODE_CONTROLLER_H_
#define CHROME_BROWSER_UI_COCOA_PRESENTATION_MODE_CONTROLLER_H_

#import <Cocoa/Cocoa.h>

#include "base/mac/mac_util.h"
#include "chrome/browser/ui/cocoa/location_bar/location_bar_view_mac.h"

@class BrowserWindowController;
@class DropdownAnimation;

// Provides a controller to manage presentation mode for a single browser
// window.  This class handles running animations, showing and hiding the
// floating dropdown bar, and managing the tracking area associated with the
// dropdown.  This class does not directly manage any views -- the
// BrowserWindowController is responsible for positioning and z-ordering views.
//
// Tracking areas are disabled while animations are running.  If
// |overlayFrameChanged:| is called while an animation is running, the
// controller saves the new frame and installs the appropriate tracking area
// when the animation finishes.  This is largely done for ease of
// implementation; it is easier to check the mouse location at each animation
// step than it is to manage a constantly-changing tracking area.
@interface PresentationModeController : NSObject<NSAnimationDelegate> {
 @private
  // Our parent controller.
  BrowserWindowController* browserController_;  // weak

  // The content view for the window.  This is nil when not in presentation
  // mode.
  NSView* contentView_;  // weak

  // YES while this controller is in the process of entering presentation mode.
  BOOL enteringPresentationMode_;

  // Whether or not we are in presentation mode.
  BOOL inPresentationMode_;

  // The tracking area associated with the floating dropdown bar.  This tracking
  // area is attached to |contentView_|, because when the dropdown is completely
  // hidden, we still need to keep a 1px tall tracking area visible.  Attaching
  // to the content view allows us to do this.  |trackingArea_| can be nil if
  // not in presentation mode or during animations.
  base::scoped_nsobject<NSTrackingArea> trackingArea_;

  // Pointer to the currently running animation.  Is nil if no animation is
  // running.
  base::scoped_nsobject<DropdownAnimation> currentAnimation_;

  // Timers for scheduled showing/hiding of the bar (which are always done with
  // animation).
  base::scoped_nsobject<NSTimer> showTimer_;
  base::scoped_nsobject<NSTimer> hideTimer_;

  // Holds the current bounds of |trackingArea_|, even if |trackingArea_| is
  // currently nil.  Used to restore the tracking area when an animation
  // completes.
  NSRect trackingAreaBounds_;

  // Tracks the currently requested system fullscreen mode, used to show or hide
  // the menubar.  This should be |kFullScreenModeNormal| when the window is not
  // main or not fullscreen, |kFullScreenModeHideAll| while the overlay is
  // hidden, and |kFullScreenModeHideDock| while the overlay is shown.  If the
  // window is not on the primary screen, this should always be
  // |kFullScreenModeNormal|.  This value can get out of sync with the correct
  // state if we miss a notification (which can happen when a window is closed).
  // Used to track the current state and make sure we properly restore the menu
  // bar when this controller is destroyed.
  base::mac::FullScreenMode systemFullscreenMode_;
}

@property(readonly, nonatomic) BOOL inPresentationMode;

// Designated initializer.
- (id)initWithBrowserController:(BrowserWindowController*)controller;

// Informs the controller that the browser has entered or exited presentation
// mode. |-enterPresentationModeForContentView:showDropdown:| should be called
// after the window is setup, just before it is shown. |-exitPresentationMode|
// should be called before any views are moved back to the non-fullscreen
// window.  If |-enterPresentationModeForContentView:showDropdown:| is called,
// it must be balanced with a call to |-exitPresentationMode| before the
// controller is released.
- (void)enterPresentationModeForContentView:(NSView*)contentView
                               showDropdown:(BOOL)showDropdown;
- (void)exitPresentationMode;

// Returns the amount by which the floating bar should be offset downwards (to
// avoid the menu) and by which the overlay view should be enlarged vertically.
// Generally, this is > 0 when the window is on the primary screen and 0
// otherwise.
- (CGFloat)floatingBarVerticalOffset;

// Informs the controller that the overlay's frame has changed.  The controller
// uses this information to update its tracking areas.
- (void)overlayFrameChanged:(NSRect)frame;

// Informs the controller that the overlay should be shown/hidden, possibly with
// animation, possibly after a delay (only applicable for the animated case).
- (void)ensureOverlayShownWithAnimation:(BOOL)animate delay:(BOOL)delay;
- (void)ensureOverlayHiddenWithAnimation:(BOOL)animate delay:(BOOL)delay;

// Cancels any running animation and timers.
- (void)cancelAnimationAndTimers;

// Gets the current floating bar shown fraction.
- (CGFloat)floatingBarShownFraction;

// Sets a new current floating bar shown fraction.  NOTE: This function has side
// effects, such as modifying the system fullscreen mode (menu bar shown state).
- (void)changeFloatingBarShownFraction:(CGFloat)fraction;

@end

// Notification posted when we're about to enter or leave fullscreen.
extern NSString* const kWillEnterFullscreenNotification;
extern NSString* const kWillLeaveFullscreenNotification;

#endif  // CHROME_BROWSER_UI_COCOA_PRESENTATION_MODE_CONTROLLER_H_
