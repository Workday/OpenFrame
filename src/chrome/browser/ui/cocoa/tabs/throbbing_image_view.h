// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_TABS_THROBBING_IMAGE_VIEW_H_
#define CHROME_BROWSER_UI_COCOA_TABS_THROBBING_IMAGE_VIEW_H_

#import <Cocoa/Cocoa.h>

#include "base/mac/scoped_nsobject.h"
#include "base/memory/scoped_ptr.h"
#include "ui/base/animation/throb_animation.h"

class ThrobbingImageViewAnimationDelegate;

// Where to position the throb image. For the overlay position, the throb image
// will be drawn with the same size as the background image. For the bottom
// right position, it will have its original size.
enum ThrobPosition {
  kThrobPositionOverlay,
  kThrobPositionBottomRight
};

@interface ThrobbingImageView : NSView {
 @protected
  base::scoped_nsobject<NSImage> backgroundImage_;
  base::scoped_nsobject<NSImage> throbImage_;
  scoped_ptr<ui::ThrobAnimation> throbAnimation_;

 @private
  scoped_ptr<ThrobbingImageViewAnimationDelegate> delegate_;
  ThrobPosition throbPosition_;
}

- (id)initWithFrame:(NSRect)rect
       backgroundImage:(NSImage*)backgroundImage
            throbImage:(NSImage*)throbImage
            durationMS:(int)durationMS
         throbPosition:(ThrobPosition)throbPosition
    animationContainer:(ui::AnimationContainer*)animationContainer;

@end

#endif  // CHROME_BROWSER_UI_COCOA_TABS_THROBBING_IMAGE_VIEW_H_
