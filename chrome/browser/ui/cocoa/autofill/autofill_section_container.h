// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_AUTOFILL_AUTOFILL_SECTION_CONTAINER_H_
#define CHROME_BROWSER_UI_COCOA_AUTOFILL_AUTOFILL_SECTION_CONTAINER_H_

#import <Cocoa/Cocoa.h>

#include "base/mac/scoped_nsobject.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/ui/autofill/autofill_dialog_types.h"
#import "chrome/browser/ui/cocoa/autofill/autofill_input_field.h"
#import "chrome/browser/ui/cocoa/autofill/autofill_layout.h"

namespace autofill {
  class AutofillDialogViewDelegate;
}

@class AutofillSectionView;
@class AutofillSuggestionContainer;
@class LayoutView;
@class MenuButton;
@class MenuController;

// Delegate to handle display of validation messages.
@protocol AutofillValidationDisplay

// Updates the validation message for a given field.
- (void)updateMessageForField:(NSControl<AutofillInputField>*)field;

@end


// View delegate for a section of the payment details. Contains a label
// describing the section as well as associated inputs and controls. Built
// dynamically based on data retrieved from AutofillDialogViewDelegate.
@interface AutofillSectionContainer :
    NSViewController<AutofillLayout, AutofillInputDelegate> {
 @private
  base::scoped_nsobject<LayoutView> inputs_;
  base::scoped_nsobject<MenuButton> suggestButton_;
  base::scoped_nsobject<AutofillSuggestionContainer> suggestContainer_;
  base::scoped_nsobject<NSTextField> label_;

  // The view for the container.
  base::scoped_nsobject<AutofillSectionView> view_;

  // List of weak pointers, which constitute unique field IDs.
  std::vector<const autofill::DetailInput*> detailInputs_;

  // A delegate to handle display of validation messages. Not owned.
  id<AutofillValidationDisplay> validationDelegate_;

  base::scoped_nsobject<MenuController> menuController_;
  autofill::DialogSection section_;
  autofill::AutofillDialogViewDelegate* delegate_;  // Not owned.
}

@property(readonly, nonatomic) autofill::DialogSection section;
@property(assign, nonatomic) id<AutofillValidationDisplay> validationDelegate;

// Designated initializer. Queries |delegate| for the list of desired input
// fields for |section|.
- (id)initWithDelegate:(autofill::AutofillDialogViewDelegate*)delegate
              forSection:(autofill::DialogSection)section;

// Populates |output| with mappings from field identification to input value.
- (void)getInputs:(autofill::DetailOutputMap*)output;

// Called when the delegate-maintained suggestions model has changed.
- (void)modelChanged;

// Called when the contents of a section have changed.
- (void)update;

// Fills the section with Autofill data that was triggered by a user
// interaction with the originating |input|.
- (void)fillForInput:(const autofill::DetailInput&)input;

// Validate this section. Validation rules depend on |validationType|.
- (BOOL)validateFor:(autofill::ValidationType)validationType;

@end

@interface AutofillSectionContainer (ForTesting)

// Retrieve the field associated with the given type.
- (NSControl<AutofillInputField>*)getField:(autofill::ServerFieldType)type;

// Sets the value for the field matching |input|. Does nothing if the field is
// not part of this section.
- (void)setFieldValue:(NSString*)text
              forInput:(const autofill::DetailInput&)input;

// Sets the value for the suggestion text field.
- (void)setSuggestionFieldValue:(NSString*)text;

// Activates a given input field, determined by |input|. Does nothing if the
// field is not part of this section.
- (void)activateFieldForInput:(const autofill::DetailInput&)input;

@end

#endif  // CHROME_BROWSER_UI_COCOA_AUTOFILL_AUTOFILL_SECTION_CONTAINER_H_
