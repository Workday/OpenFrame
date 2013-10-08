// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/browser/avatar_label_button.h"

#include "chrome/browser/themes/theme_properties.h"
#include "chrome/browser/ui/cocoa/themed_window.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "ui/base/l10n/l10n_util_mac.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/theme_provider.h"

namespace {

// Space between the left edge of the label background and the left edge of the
// label text.
const CGFloat kLabelTextLeftSpacing = 10;

// Space between the right edge of the label text and the avatar icon.
const CGFloat kLabelTextRightSpacing = 4;

// Space between the top edge of the label background and the top edge of the
// label text.
const CGFloat kLabelTextTopSpacing = 3;

// Space between the bottom edge of the label background and the bottom edge of
// the label text.
const CGFloat kLabelTextBottomSpacing = 4;

}  // namespace

@implementation AvatarLabelButton

- (id)initWithFrame:(NSRect)frameRect {
  if ((self = [super initWithFrame:frameRect])) {
    // Increase the frame by the size of the label to be displayed.
    NSSize textSize = [[self cell] labelTextSize];
    frameRect.size = NSMakeSize(frameRect.size.width + textSize.width,
                                frameRect.size.height + textSize.height);
    [self setFrame:frameRect];
  }
  return self;
}

+ (Class)cellClass {
  return [AvatarLabelButtonCell class];
}

@end

@implementation AvatarLabelButtonCell

- (id)init {
  const int resourceIds[9] = {
    IDR_MANAGED_USER_LABEL_TOP_LEFT, IDR_MANAGED_USER_LABEL_TOP,
    IDR_MANAGED_USER_LABEL_TOP_RIGHT, IDR_MANAGED_USER_LABEL_LEFT,
    IDR_MANAGED_USER_LABEL_CENTER, IDR_MANAGED_USER_LABEL_RIGHT,
    IDR_MANAGED_USER_LABEL_BOTTOM_LEFT, IDR_MANAGED_USER_LABEL_BOTTOM,
    IDR_MANAGED_USER_LABEL_BOTTOM_RIGHT
  };
  if ((self = [super initWithResourceIds:resourceIds])) {
    NSString* title = l10n_util::GetNSString(IDS_MANAGED_USER_AVATAR_LABEL);
    [self accessibilitySetOverrideValue:title
                           forAttribute:NSAccessibilityTitleAttribute];
    [self setTitle:title];
    [self setFont:[NSFont labelFontOfSize:12.0]];
  }
  return self;
}

- (NSSize)labelTextSize {
  NSSize size = [[self attributedTitle] size];
  size.width += kLabelTextLeftSpacing + kLabelTextRightSpacing;
  size.height += kLabelTextTopSpacing + kLabelTextBottomSpacing;
  return size;
}

- (NSRect)titleRectForBounds:(NSRect)theRect {
  theRect.origin = NSMakePoint(kLabelTextLeftSpacing, kLabelTextBottomSpacing);
  theRect.size = [[self attributedTitle] size];
  return theRect;
}

- (NSRect)drawTitle:(NSAttributedString*)title
          withFrame:(NSRect)frame
             inView:(NSView*)controlView {
  base::scoped_nsobject<NSMutableAttributedString> themedTitle(
      [[NSMutableAttributedString alloc] initWithAttributedString:title]);
  ui::ThemeProvider* themeProvider = [[controlView window] themeProvider];
  if (themeProvider) {
    NSColor* textColor = themeProvider->GetNSColor(
        ThemeProperties::COLOR_MANAGED_USER_LABEL);
    [themedTitle addAttribute:NSForegroundColorAttributeName
                        value:textColor
                        range:NSMakeRange(0, title.length)];
  }
  [themedTitle drawInRect:frame];
  return frame;
}

- (void)drawInteriorWithFrame:(NSRect)cellFrame inView:(NSView*)controlView {
  ui::ThemeProvider* themeProvider = [[controlView window] themeProvider];
  if (themeProvider) {
    // Draw the label button background using the color provided by
    // |themeProvider|. First paint the border.
    NSColor* borderColor = themeProvider->GetNSColor(
        ThemeProperties::COLOR_MANAGED_USER_LABEL_BORDER);
    if ([self isHighlighted]) {
      borderColor = [borderColor blendedColorWithFraction:0.5
                                                  ofColor:[NSColor blackColor]];
    }
    NSSize frameSize = cellFrame.size;
    NSRect backgroundRect;
    backgroundRect.origin = NSMakePoint(1, 1);
    backgroundRect.size = NSMakeSize(frameSize.width - 2, frameSize.height - 2);
    NSBezierPath* path =
        [NSBezierPath bezierPathWithRoundedRect:backgroundRect
                                        xRadius:2.0
                                        yRadius:2.0];
    [borderColor set];
    [path fill];

    // Now paint the background.
    NSColor* backgroundColor = themeProvider->GetNSColor(
        ThemeProperties::COLOR_MANAGED_USER_LABEL_BACKGROUND);
    if ([self isHighlighted]) {
      backgroundColor =
          [backgroundColor blendedColorWithFraction:0.5
                                            ofColor:[NSColor blackColor]];
    }
    backgroundRect.origin = NSMakePoint(2, 2);
    backgroundRect.size = NSMakeSize(frameSize.width - 4, frameSize.height - 4);
    path = [NSBezierPath bezierPathWithRoundedRect:backgroundRect
                                           xRadius:2.0
                                           yRadius:2.0];
    [backgroundColor set];
    [path fill];
  }
  [super drawInteriorWithFrame:cellFrame inView:controlView];
}

@end
