// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_BROWSER_ZOOM_BUBBLE_CONTROLLER_H_
#define CHROME_BROWSER_UI_COCOA_BROWSER_ZOOM_BUBBLE_CONTROLLER_H_

#include "base/mac/scoped_block.h"
#include "base/mac/scoped_nsobject.h"
#include "chrome/browser/ui/cocoa/base_bubble_controller.h"
#import "ui/base/cocoa/tracking_area.h"

namespace content {
class WebContents;
}

// The ZoomBubbleController is used to display the current page zoom percent
// when not at the user's default. It is opened by the ZoomDecoration in the
// location bar.
@interface ZoomBubbleController : BaseBubbleController {
 @private
  // The contents for which the zoom percent is being displayed.
  content::WebContents* contents_;

  // Whether or not the bubble should automatically close itself after being
  // opened.
  BOOL autoClose_;

  // A block that is run when the bubble is being closed. This allows any
  // weak references to be niled.
  base::mac::ScopedBlock<void(^)(ZoomBubbleController*)> closeObserver_;

  // The text field that displays the current zoom percentage.
  base::scoped_nsobject<NSTextField> zoomPercent_;

  // Whether or not the mouse is over the bubble.
  BOOL isMouseInside_;

  // Used to prevent the bubble from auto-closing while the mouse is inside it.
  ui::ScopedCrTrackingArea trackingArea_;
}

// Creates the bubble for a parent window but does not show it.
- (id)initWithParentWindow:(NSWindow*)parentWindow
             closeObserver:(void(^)(ZoomBubbleController*))closeObserver;

// Shows the bubble for a given contents at an |anchorPoint| in window
// coordinates. If |autoClose| is YES, then the bubble was opened in response
// to a zoom change rather than a direct user action, and it will automatically
// dismiss itself after a few seconds.
- (void)showForWebContents:(content::WebContents*)contents
                anchoredAt:(NSPoint)anchorPoint
                 autoClose:(BOOL)autoClose;

// Called by the ZoomDecoration when the zoom percentage changes.
- (void)onZoomChanged;

// Button action from the bubble that resets the zoom level to the default.
- (void)resetToDefault:(id)sender;

// Button action from the bubble that increases the zoom level.
- (void)zoomIn:(id)sender;

// Button action from the bubble that decreases the zoom level.
- (void)zoomOut:(id)sender;

// Closes the bubble synchronously, bypassing any animations.
- (void)closeWithoutAnimation;

@end

namespace chrome {

void SetZoomBubbleAutoCloseDelayForTesting(NSTimeInterval time_interval);

}  // namespace chrome

#endif  // CHROME_BROWSER_UI_COCOA_BROWSER_ZOOM_BUBBLE_CONTROLLER_H_
