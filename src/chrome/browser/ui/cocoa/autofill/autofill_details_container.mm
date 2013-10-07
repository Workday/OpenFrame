// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/autofill/autofill_details_container.h"

#include <algorithm>

#include "base/mac/foundation_util.h"
#include "chrome/browser/ui/autofill/autofill_dialog_view_delegate.h"
#import "chrome/browser/ui/cocoa/autofill/autofill_section_container.h"
#import "chrome/browser/ui/cocoa/info_bubble_view.h"
#include "skia/ext/skia_utils_mac.h"

namespace {

// Imported constant from Views version. TODO(groby): Share.
SkColor const kWarningColor = 0xffde4932;  // SkColorSetRGB(0xde, 0x49, 0x32);

}  // namespace

@interface AutofillDetailsContainer ()
// Compute infobubble origin based on anchor/view.
- (NSPoint)originFromAnchorView:(NSView*)view;
@end

@implementation AutofillDetailsContainer

- (id)initWithDelegate:(autofill::AutofillDialogViewDelegate*)delegate {
  if (self = [super init]) {
    delegate_ = delegate;
  }
  return self;
}

- (void)addSection:(autofill::DialogSection)section {
  base::scoped_nsobject<AutofillSectionContainer> sectionContainer(
      [[AutofillSectionContainer alloc] initWithDelegate:delegate_
                                                forSection:section]);
  [sectionContainer setValidationDelegate:self];
  [details_ addObject:sectionContainer];
}

- (void)loadView {
  details_.reset([[NSMutableArray alloc] init]);

  [self addSection:autofill::SECTION_EMAIL];
  [self addSection:autofill::SECTION_CC];
  [self addSection:autofill::SECTION_BILLING];
  [self addSection:autofill::SECTION_CC_BILLING];
  [self addSection:autofill::SECTION_SHIPPING];

  scrollView_.reset([[NSScrollView alloc] initWithFrame:NSZeroRect]);
  [scrollView_ setHasVerticalScroller:YES];
  [scrollView_ setHasHorizontalScroller:NO];
  [scrollView_ setBorderType:NSNoBorder];
  [scrollView_ setAutohidesScrollers:YES];
  [self setView:scrollView_];

  [scrollView_ setDocumentView:[[NSView alloc] initWithFrame:NSZeroRect]];

  for (AutofillSectionContainer* container in details_.get())
    [[scrollView_ documentView] addSubview:[container view]];

  infoBubble_.reset([[InfoBubbleView alloc] initWithFrame:NSZeroRect]);
  [infoBubble_ setBackgroundColor:
      gfx::SkColorToCalibratedNSColor(kWarningColor)];
  [infoBubble_ setArrowLocation:info_bubble::kTopRight];
  [infoBubble_ setAlignment:info_bubble::kAlignArrowToAnchor];
  [infoBubble_ setHidden:YES];

  base::scoped_nsobject<NSTextField> label([[NSTextField alloc] init]);
  [label setEditable:NO];
  [label setBordered:NO];
  [label setDrawsBackground:NO];
  [infoBubble_ addSubview:label];

  [[scrollView_ documentView] addSubview:infoBubble_];

  [self performLayout];
}

- (NSSize)preferredSize {
  NSSize size = NSZeroSize;
  for (AutofillSectionContainer* container in details_.get()) {
    NSSize containerSize = [container preferredSize];
    size.height += containerSize.height;
    size.width = std::max(containerSize.width, size.width);
  }
  return size;
}

- (void)performLayout {
  NSRect rect = NSZeroRect;
  for (AutofillSectionContainer* container in
      [details_ reverseObjectEnumerator]) {
    if (![[container view] isHidden]) {
      [container performLayout];
      [[container view] setFrameOrigin:NSMakePoint(0, NSMaxY(rect))];
      rect = NSUnionRect(rect, [[container view] frame]);
    }
  }

  [[scrollView_ documentView] setFrameSize:[self preferredSize]];
}

- (AutofillSectionContainer*)sectionForId:(autofill::DialogSection)section {
  for (AutofillSectionContainer* details in details_.get()) {
    if ([details section] == section)
      return details;
  }
  return nil;
}

- (void)modelChanged {
  for (AutofillSectionContainer* details in details_.get())
    [details modelChanged];
}

- (BOOL)validate {
  bool allValid = true;
  for (AutofillSectionContainer* details in details_.get()) {
    if (![[details view] isHidden])
      allValid = [details validateFor:autofill::VALIDATE_FINAL] && allValid;
  }
  return allValid;
}

// TODO(groby): Unify with BaseBubbleController's originFromAnchor:view:.
- (NSPoint)originFromAnchorView:(NSView*)view {
  NSView* bubbleParent = [infoBubble_ superview];
  NSPoint origin = [[view superview] convertPoint:[view frame].origin
                                           toView:nil];
  NSRect bubbleFrame =
      [bubbleParent convertRect:[infoBubble_ frame] toView:nil];

  NSSize offsets = NSMakeSize(info_bubble::kBubbleArrowXOffset +
                              info_bubble::kBubbleArrowWidth / 2.0, 0);
  offsets = [view convertSize:offsets toView:nil];
  origin.x -= NSWidth(bubbleFrame) - offsets.width;

  origin.y -= NSHeight(bubbleFrame);
  return [bubbleParent convertPoint:origin fromView:nil];
}

- (void)updateMessageForField:(NSControl<AutofillInputField>*)field {
  // Ignore fields that are not first responder. Testing this is a bit
  // convoluted, since for NSTextFields with firstResponder status, the
  // firstResponder is a subview of the NSTextField, not the field itself.
  NSView* firstResponderView =
      base::mac::ObjCCast<NSView>([[field window] firstResponder]);
  if (![firstResponderView isDescendantOf:field]) {
    return;
  }

  if ([field invalid]) {
    const CGFloat labelInset = 3.0;

    NSTextField* label = [[infoBubble_ subviews] objectAtIndex:0];
    [label setStringValue:[field validityMessage]];
    [label sizeToFit];
    NSSize bubbleSize = [label frame].size;
    bubbleSize.width += 2 * labelInset;
    bubbleSize.height += 2 * labelInset + info_bubble::kBubbleArrowHeight;
    [infoBubble_ setFrameSize:bubbleSize];
    [label setFrameOrigin:NSMakePoint(labelInset, labelInset)];
    [infoBubble_ setFrameOrigin:[self originFromAnchorView:field]];
    [infoBubble_ setHidden:NO];
  } else {
    [infoBubble_ setHidden:YES];
  }
}

@end
