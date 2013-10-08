// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_AUTOFILL_AUTOFILL_DETAILS_CONTAINER_H_
#define CHROME_BROWSER_UI_COCOA_AUTOFILL_AUTOFILL_DETAILS_CONTAINER_H_

#import <Cocoa/Cocoa.h>

#include "base/mac/scoped_nsobject.h"
#include "chrome/browser/ui/autofill/autofill_dialog_types.h"
#import "chrome/browser/ui/cocoa/autofill/autofill_layout.h"
#import "chrome/browser/ui/cocoa/autofill/autofill_section_container.h"


namespace autofill {
class AutofillDialogViewDelegate;
}

@class InfoBubbleView;

// UI controller for details for current payment instrument.
@interface AutofillDetailsContainer
    : NSViewController<AutofillLayout,
                       AutofillValidationDisplay> {
 @private
  // Scroll view containing all detail sections.
  base::scoped_nsobject<NSScrollView> scrollView_;

  // The individual detail sections.
  base::scoped_nsobject<NSMutableArray> details_;

  // An info bubble to display validation errors.
  base::scoped_nsobject<InfoBubbleView> infoBubble_;

  autofill::AutofillDialogViewDelegate* delegate_;  // Not owned.
}

// Designated initializer.
- (id)initWithDelegate:(autofill::AutofillDialogViewDelegate*)delegate;

// Retrieve the container for the specified |section|.
- (AutofillSectionContainer*)sectionForId:(autofill::DialogSection)section;

// Called when the delegate-maintained suggestions model has changed.
- (void)modelChanged;

// Validate every visible details section.
- (BOOL)validate;

@end

#endif  // CHROME_BROWSER_UI_COCOA_AUTOFILL_AUTOFILL_DETAILS_CONTAINER_H_
