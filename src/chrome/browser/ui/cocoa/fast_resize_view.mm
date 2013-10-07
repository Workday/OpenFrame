// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <Cocoa/Cocoa.h>
#import "chrome/browser/ui/cocoa/fast_resize_view.h"

#include "base/logging.h"

@interface FastResizeView (PrivateMethods)
// Lays out this views subviews.  If fast resize mode is on, does not resize any
// subviews and instead pegs them to the top left.  If fast resize mode is off,
// sets the subviews' frame to be equal to this view's bounds.
- (void)layoutSubviews;
@end

@implementation FastResizeView

- (void)setFastResizeMode:(BOOL)fastResizeMode {
  if (fastResizeMode_ == fastResizeMode)
    return;

  fastResizeMode_ = fastResizeMode;

  // Force a relayout when coming out of fast resize mode.
  if (!fastResizeMode_)
    [self layoutSubviews];

  [self setNeedsDisplay:YES];
}

- (void)resizeSubviewsWithOldSize:(NSSize)oldSize {
  [self layoutSubviews];
}

- (void)drawRect:(NSRect)dirtyRect {
  // If we are in fast resize mode, our subviews may not completely cover our
  // bounds, so we fill with white.  If we are not in fast resize mode, we do
  // not need to draw anything.
  if (!fastResizeMode_)
    return;

  [[NSColor whiteColor] set];
  NSRectFill(dirtyRect);
}

@end

@implementation FastResizeView (PrivateMethods)

- (void)layoutSubviews {
  // There should never be more than one subview.  There can be zero, if we are
  // in the process of switching tabs or closing the window.  In those cases, no
  // layout is needed.
  NSArray* subviews = [self subviews];
  DCHECK([subviews count] <= 1);
  if ([subviews count] < 1)
    return;

  NSView* subview = [subviews objectAtIndex:0];
  NSRect bounds = [self bounds];

  if (fastResizeMode_) {
    NSRect frame = [subview frame];
    frame.origin.x = 0;
    frame.origin.y = NSHeight(bounds) - NSHeight(frame);
    [subview setFrame:frame];
  } else {
    [subview setFrame:bounds];
  }
}

@end
