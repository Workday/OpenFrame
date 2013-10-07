// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cmath>

#import "chrome/browser/ui/cocoa/location_bar/bubble_decoration.h"

#include "base/logging.h"

namespace {

// Padding between the icon/label and bubble edges.
const CGFloat kBubblePadding = 3.0;

// The image needs to be in the same position as for the location
// icon, which implies that the bubble's padding in the Omnibox needs
// to differ from the location icon's.  Indeed, that's how the views
// implementation handles the problem.  This draws the bubble edge a
// little bit further left, which is easier but no less hacky.
const CGFloat kLeftSideOverdraw = 2.0;

// Omnibox corner radius is |4.0|, this needs to look tight WRT that.
const CGFloat kBubbleCornerRadius = 2.0;

// Padding between the icon and label.
const CGFloat kIconLabelPadding = 4.0;

// Inset for the background.
const CGFloat kBackgroundYInset = 4.0;

}  // namespace

BubbleDecoration::BubbleDecoration() {
  attributes_.reset([[NSMutableDictionary alloc] init]);
  [attributes_ setObject:GetFont() forKey:NSFontAttributeName];
}

BubbleDecoration::~BubbleDecoration() {
}

CGFloat BubbleDecoration::GetWidthForImageAndLabel(NSImage* image,
                                                   NSString* label) {
  if (!image && !label)
    return kOmittedWidth;

  const CGFloat image_width = image ? [image size].width : 0.0;
  if (!label)
    return kBubblePadding + image_width;

  // The bubble needs to take up an integral number of pixels.
  // Generally -sizeWithAttributes: seems to overestimate rather than
  // underestimate, so floor() seems to work better.
  const CGFloat label_width =
      std::floor([label sizeWithAttributes:attributes_].width);
  return kBubblePadding + image_width + kIconLabelPadding + label_width;
}

NSRect BubbleDecoration::GetImageRectInFrame(NSRect frame) {
  NSRect imageRect = NSInsetRect(frame, 0.0, kBackgroundYInset);
  if (image_) {
    // Center the image vertically.
    const NSSize imageSize = [image_ size];
    imageRect.origin.y +=
        std::floor((NSHeight(frame) - imageSize.height) / 2.0);
    imageRect.size = imageSize;
  }
  return imageRect;
}

CGFloat BubbleDecoration::GetWidthForSpace(CGFloat width) {
  const CGFloat all_width = GetWidthForImageAndLabel(image_, label_);
  if (all_width <= width)
    return all_width;

  const CGFloat image_width = GetWidthForImageAndLabel(image_, nil);
  if (image_width <= width)
    return image_width;

  return kOmittedWidth;
}

void BubbleDecoration::DrawInFrame(NSRect frame, NSView* control_view) {
  const NSRect decorationFrame = NSInsetRect(frame, 0.0, kBackgroundYInset);

  // The inset is to put the border down the middle of the pixel.
  NSRect bubbleFrame = NSInsetRect(decorationFrame, 0.5, 0.5);
  bubbleFrame.origin.x -= kLeftSideOverdraw;
  bubbleFrame.size.width += kLeftSideOverdraw;
  NSBezierPath* path =
      [NSBezierPath bezierPathWithRoundedRect:bubbleFrame
                                      xRadius:kBubbleCornerRadius
                                      yRadius:kBubbleCornerRadius];

  [background_color_ setFill];
  [path fill];

  [border_color_ setStroke];
  [path setLineWidth:1.0];
  [path stroke];

  CGFloat textOffset = NSMinX(decorationFrame);
  if (image_) {
    // Center the image vertically.
    const NSSize imageSize = [image_ size];
    NSRect imageRect = decorationFrame;
    imageRect.origin.y +=
        std::floor((NSHeight(decorationFrame) - imageSize.height) / 2.0);
    imageRect.size = imageSize;
    [image_ drawInRect:imageRect
              fromRect:NSZeroRect  // Entire image
             operation:NSCompositeSourceOver
              fraction:1.0
        respectFlipped:YES
                 hints:nil];
    textOffset = NSMaxX(imageRect) + kIconLabelPadding;
  }

  if (label_) {
    NSRect textRect = frame;
    textRect.origin.x = textOffset;
    textRect.size.width = NSMaxX(decorationFrame) - NSMinX(textRect);
    DrawLabel(label_, attributes_, textRect);
  }
}

NSImage* BubbleDecoration::GetImage() {
  return image_;
}

void BubbleDecoration::SetImage(NSImage* image) {
  image_.reset([image retain]);
}

void BubbleDecoration::SetLabel(NSString* label) {
  // If the initializer was called with |nil|, then the code cannot
  // process a label.
  DCHECK(attributes_);
  if (attributes_)
    label_.reset([label copy]);
}

void BubbleDecoration::SetColors(NSColor* border_color,
                                 NSColor* background_color,
                                 NSColor* text_color) {
  border_color_.reset([border_color retain]);
  background_color_.reset([background_color retain]);
  [attributes_ setObject:text_color forKey:NSForegroundColorAttributeName];
}
