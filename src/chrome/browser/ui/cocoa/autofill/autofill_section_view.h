// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_AUTOFILL_AUTOFILL_SECTION_VIEW_H_
#define CHROME_BROWSER_UI_COCOA_AUTOFILL_AUTOFILL_SECTION_VIEW_H_

#import <Cocoa/Cocoa.h>

#import "ui/base/cocoa/base_view.h"

// Main view for autofill sections. Takes care of hover highlight if needed.
// Tracking areas are subtle and quick to anger. BaseView does the right thing.
@interface AutofillSectionView : BaseView {
 @private
  BOOL isHighlighted_;  // Track current highlight state.
  BOOL shouldHighlightOnHover_;  // Indicates if view should highlight on hover
}

// Color used to highlight the view on hover.
@property (readonly, nonatomic, getter=hoverColor) NSColor* hoverColor;

// Controls if the view should show a highlight when hovered over.
@property(assign, nonatomic) BOOL shouldHighlightOnHover;

// Current highlighting state.
@property(readonly, nonatomic) BOOL isHighlighted;

@end

#endif  // CHROME_BROWSER_UI_COCOA_AUTOFILL_AUTOFILL_SECTION_VIEW_H_
