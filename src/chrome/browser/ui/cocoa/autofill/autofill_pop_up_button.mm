// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/autofill/autofill_pop_up_button.h"

#include <ApplicationServices/ApplicationServices.h>

#include "base/mac/scoped_nsobject.h"
#include "ui/gfx/scoped_ns_graphics_context_save_gstate_mac.h"

@interface AutofillPopUpButton ()
- (void)didSelectItem:(id)sender;
@end

@implementation AutofillPopUpButton

@synthesize delegate = delegate_;

+ (Class)cellClass {
  return [AutofillPopUpCell class];
}

- (id)initWithFrame:(NSRect)frame pullsDown:(BOOL)pullsDown{
  if (self = [super initWithFrame:frame pullsDown:pullsDown]) {
    [self setTarget:self];
    [self setAction:@selector(didSelectItem:)];
  }
  return self;
}

- (NSString*)fieldValue {
  return [[self cell] fieldValue];
}

- (void)setFieldValue:(NSString*)fieldValue {
  [[self cell] setFieldValue:fieldValue];
}

- (NSString*)validityMessage {
  return validityMessage_;
}

- (void)setValidityMessage:(NSString*)validityMessage {
  validityMessage_.reset([validityMessage copy]);
  [[self cell] setInvalid:[self invalid]];
  [self setNeedsDisplay:YES];
}

- (BOOL)invalid {
  return [validityMessage_ length] != 0;
}

- (void)didSelectItem:(id)sender {
  if (delegate_)
    [delegate_ didEndEditing:self];
}

@end


@implementation AutofillPopUpCell

@synthesize invalid = invalid_;

// Draw a bezel that's highlighted.
- (void)drawBezelWithFrame:(NSRect)frame inView:(NSView*)controlView {
  if (invalid_) {
    CGContextRef context = static_cast<CGContextRef>(
        [[NSGraphicsContext currentContext] graphicsPort]);

    // Create a highlight-shaded bezel in a transparency layer.
    CGContextBeginTransparencyLayerWithRect(context, NSRectToCGRect(frame), 0);
    // 1. Draw bezel.
    [super drawBezelWithFrame:frame inView:controlView];

    // 2. Use that as stencil against solid color rect.
    [[NSColor redColor] set];
    NSRectFillUsingOperation(frame, NSCompositeSourceAtop);

    // 3. Composite the solid color bezel and the actual bezel.
    CGContextSetBlendMode(context, kCGBlendModePlusDarker);
    [super drawBezelWithFrame:frame inView:controlView];
    CGContextEndTransparencyLayer(context);
  } else {
    [super drawBezelWithFrame:frame inView:controlView];
  }
}

- (NSString*)fieldValue {
  return [self titleOfSelectedItem];
}

- (void)setFieldValue:(NSString*)fieldValue {
  [self selectItemWithTitle:fieldValue];
}

@end
