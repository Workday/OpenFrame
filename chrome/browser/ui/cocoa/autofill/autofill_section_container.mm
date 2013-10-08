// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/autofill/autofill_section_container.h"

#include <algorithm>

#include "base/mac/foundation_util.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/autofill/autofill_dialog_view_delegate.h"
#import "chrome/browser/ui/cocoa/autofill/autofill_pop_up_button.h"
#import "chrome/browser/ui/cocoa/autofill/autofill_section_view.h"
#import "chrome/browser/ui/cocoa/autofill/autofill_suggestion_container.h"
#import "chrome/browser/ui/cocoa/autofill/autofill_textfield.h"
#import "chrome/browser/ui/cocoa/autofill/layout_view.h"
#include "chrome/browser/ui/cocoa/autofill/simple_grid_layout.h"
#import "chrome/browser/ui/cocoa/image_button_cell.h"
#import "chrome/browser/ui/cocoa/menu_button.h"
#include "components/autofill/core/browser/autofill_type.h"
#include "grit/theme_resources.h"
#import "ui/base/cocoa/menu_controller.h"
#include "ui/base/l10n/l10n_util_mac.h"
#include "ui/base/models/combobox_model.h"
#include "ui/base/resource/resource_bundle.h"

namespace {

// Constants used for layouting controls. These variables are copied from
// "ui/views/layout/layout_constants.h".

// Horizontal spacing between controls that are logically related.
const int kRelatedControlHorizontalSpacing = 8;

// Vertical spacing between controls that are logically related.
const int kRelatedControlVerticalSpacing = 8;

// TODO(estade): pull out these constants, and figure out better values
// for them. Note: These are duplicated from Views code.

// Fixed width for the section label.
const int kLabelWidth = 180;

// Padding between section label and section input.
const int kPadding = 30;

// Fixed width for the details section.
const int kDetailsWidth = 300;

// Top/bottom inset for contents of a detail section.
const size_t kDetailSectionInset = 10;

// Break suggestion text into two lines. TODO(groby): Should be on delegate.
void BreakSuggestionText(const string16& text,
                         string16* line1,
                         string16* line2) {
  // TODO(estade): does this localize well?
  string16 line_return(base::ASCIIToUTF16("\n"));
  size_t position = text.find(line_return);
  if (position == string16::npos) {
    *line1 = text;
    line2->clear();
  } else {
    *line1 = text.substr(0, position);
    *line2 = text.substr(position + line_return.length());
  }
}

// If the Autofill data comes from a credit card, make sure to overwrite the
// CC comboboxes (even if they already have something in them). If the
// Autofill data comes from an AutofillProfile, leave the comboboxes alone.
// TODO(groby): This kind of logic should _really_ live on the delegate.
bool ShouldOverwriteComboboxes(autofill::DialogSection section,
                               autofill::ServerFieldType type) {
  if (autofill::AutofillType(type).group() != autofill::CREDIT_CARD) {
    return false;
  }

  if (section == autofill::SECTION_CC) {
    return true;
  }

  return section == autofill::SECTION_CC_BILLING;
}

bool CompareInputRows(const autofill::DetailInput* input1,
                      const autofill::DetailInput* input2) {
  // Row ID -1 is sorted to the end of rows.
  if (input2->row_id == -1)
    return false;
  return input2->row_id < input1->row_id;
}

}

@interface AutofillSectionContainer ()

// A text field has been edited or activated - inform the delegate that it's
// time to show a suggestion popup & possibly reset the validity of the input.
- (void)textfieldEditedOrActivated:(NSControl<AutofillInputField>*)field
                            edited:(BOOL)edited;

// Convenience method to retrieve a field type via the control's tag.
- (autofill::ServerFieldType)fieldTypeForControl:(NSControl*)control;

// Find the DetailInput* associated with a field type.
- (const autofill::DetailInput*)detailInputForType:
    (autofill::ServerFieldType)type;

// Takes an NSArray of controls and builds a DetailOutputMap from them.
// Translates between Cocoa code and delegate, essentially.
// All controls must inherit from NSControl and conform to AutofillInputView.
- (void)fillDetailOutputs:(autofill::DetailOutputMap*)outputs
             fromControls:(NSArray*)controls;

// Updates input fields based on delegate status. If |shouldClobber| is YES,
// will clobber existing data and reset fields to the initial values.
- (void)updateAndClobber:(BOOL)shouldClobber;

// Create properly styled label for section. Autoreleased.
- (NSTextField*)makeDetailSectionLabel:(NSString*)labelText;

// Create a button offering input suggestions.
- (MenuButton*)makeSuggestionButton;

// Create a view with all inputs requested by |delegate_|. Autoreleased.
- (LayoutView*)makeInputControls;

@end

@implementation AutofillSectionContainer

@synthesize section = section_;
@synthesize validationDelegate = validationDelegate_;

- (id)initWithDelegate:(autofill::AutofillDialogViewDelegate*)delegate
              forSection:(autofill::DialogSection)section {
  if (self = [super init]) {
    section_ = section;
    delegate_ = delegate;
  }
  return self;
}

- (void)getInputs:(autofill::DetailOutputMap*)output {
  [self fillDetailOutputs:output fromControls:[inputs_ subviews]];
}

// Note: This corresponds to Views' "UpdateDetailsGroupState".
- (void)modelChanged {
  ui::MenuModel* suggestionModel = delegate_->MenuModelForSection(section_);
  menuController_.reset([[MenuController alloc] initWithModel:suggestionModel
                                       useWithPopUpButtonCell:YES]);
  NSMenu* menu = [menuController_ menu];

  const BOOL hasSuggestions = [menu numberOfItems] > 0;
  [suggestButton_ setHidden:!hasSuggestions];

  [suggestButton_ setAttachedMenu:menu];

  [self updateSuggestionState];

  // TODO(groby): "Save in Chrome" handling.

  if (![[self view] isHidden])
    [self validateFor:autofill::VALIDATE_EDIT];

  // Always request re-layout on state change.
  id delegate = [[view_ window] windowController];
  if ([delegate respondsToSelector:@selector(requestRelayout)])
    [delegate performSelector:@selector(requestRelayout)];
}

- (void)loadView {
  // Keep a list of weak pointers to DetailInputs.
  const autofill::DetailInputs& inputs =
      delegate_->RequestedFieldsForSection(section_);
  for (size_t i = 0; i < inputs.size(); ++i) {
    detailInputs_.push_back(&(inputs[i]));
  }

  inputs_.reset([[self makeInputControls] retain]);
  string16 labelText = delegate_->LabelForSection(section_);
  label_.reset([[self makeDetailSectionLabel:
                   base::SysUTF16ToNSString(labelText)] retain]);

  suggestButton_.reset([[self makeSuggestionButton] retain]);
  suggestContainer_.reset([[AutofillSuggestionContainer alloc] init]);

  view_.reset([[AutofillSectionView alloc] initWithFrame:NSZeroRect]);
  [self setView:view_];
  [[self view] setSubviews:
      @[label_, inputs_, [suggestContainer_ view], suggestButton_]];

  if (section_ == autofill::SECTION_CC) {
    // SECTION_CC *MUST* have a CREDIT_CARD_VERIFICATION_CODE input.
    DCHECK([self detailInputForType:autofill::CREDIT_CARD_VERIFICATION_CODE]);
    [[suggestContainer_ inputField] setTag:
        autofill::CREDIT_CARD_VERIFICATION_CODE];
    [[suggestContainer_ inputField] setDelegate:self];
  }

  [self modelChanged];
}

- (NSSize)preferredSize {
  if ([view_ isHidden])
    return NSZeroSize;

  NSSize labelSize = [label_ frame].size;  // Assumes sizeToFit was called.
  CGFloat controlHeight = [inputs_ preferredHeightForWidth:kDetailsWidth];
  if ([inputs_ isHidden])
    controlHeight = [suggestContainer_ preferredSize].height;
  CGFloat contentHeight = std::max(controlHeight, labelSize.height);
  contentHeight = std::max(contentHeight, labelSize.height);
  contentHeight = std::max(contentHeight, NSHeight([suggestButton_ frame]));

  return NSMakeSize(kLabelWidth + kPadding + kDetailsWidth,
                    contentHeight + 2 * kDetailSectionInset);
}

- (void)performLayout {
  if ([view_ isHidden])
    return;

  NSSize buttonSize = [suggestButton_ frame].size;  // Assume sizeToFit.
  NSSize labelSize = [label_ frame].size;  // Assumes sizeToFit was called.
  CGFloat controlHeight = [inputs_ preferredHeightForWidth:kDetailsWidth];
  if ([inputs_ isHidden])
    controlHeight = [suggestContainer_ preferredSize].height;

  NSRect viewFrame = NSZeroRect;
  viewFrame.size = [self preferredSize];

  NSRect contentFrame = NSInsetRect(viewFrame, 0, kDetailSectionInset);
  NSRect dummy;

  // Set up three content columns. kLabelWidth is first column width,
  // then padding, then have suggestButton and inputs share kDetailsWidth.
  NSRect column[3];
  NSDivideRect(contentFrame, &column[0], &dummy, kLabelWidth, NSMinXEdge);
  NSDivideRect(contentFrame, &column[1], &dummy, kDetailsWidth, NSMaxXEdge);
  NSDivideRect(column[1],
               &column[2], &column[1], buttonSize.width, NSMaxXEdge);

  // Center inputs by height in column 1.
  NSRect controlFrame = column[1];
  int centerOffset = (NSHeight(controlFrame) - controlHeight) / 2;
  controlFrame.origin.x += centerOffset;
  controlFrame.size.height = controlHeight;

  // Align label to right top in column 0.
  NSRect labelFrame;
  NSDivideRect(column[0], &labelFrame, &dummy, labelSize.height, NSMaxYEdge);
  NSDivideRect(labelFrame, &labelFrame, &dummy, labelSize.width, NSMaxXEdge);

  // suggest button is top left of column 2.
  NSRect buttonFrame = column[2];
  NSDivideRect(column[2], &buttonFrame, &dummy, buttonSize.height, NSMaxYEdge);

  [[suggestContainer_ view] setFrame:controlFrame];
  [suggestContainer_ performLayout];
  [inputs_ setFrame:controlFrame];
  [label_ setFrame:labelFrame];
  [suggestButton_ setFrame:buttonFrame];
  [view_ setFrameSize:viewFrame.size];
}

- (void)fieldBecameFirstResponder:(NSControl<AutofillInputField>*)field {
  [self textfieldEditedOrActivated:field edited:NO];
  [validationDelegate_ updateMessageForField:field];
}

- (void)didChange:(id)sender {
  [self textfieldEditedOrActivated:sender edited:YES];
}

- (void)didEndEditing:(id)sender {
  [self validateFor:autofill::VALIDATE_EDIT];
}

- (void)updateSuggestionState {
  const autofill::SuggestionState& suggestionState =
      delegate_->SuggestionStateForSection(section_);
  // TODO(estade): use |vertically_compact_text| when it fits.
  const base::string16& text = suggestionState.horizontally_compact_text;
  bool showSuggestions = suggestionState.visible;

  [[suggestContainer_ view] setHidden:!showSuggestions];
  [inputs_ setHidden:showSuggestions];

  base::string16 line1;
  base::string16 line2;
  BreakSuggestionText(text, &line1, &line2);
  [suggestContainer_ setSuggestionText:base::SysUTF16ToNSString(line1)
                                 line2:base::SysUTF16ToNSString(line2)];
  [suggestContainer_ setIcon:suggestionState.icon.AsNSImage()];
  if (!suggestionState.extra_text.empty()) {
    NSString* extraText =
        base::SysUTF16ToNSString(suggestionState.extra_text);
    NSImage* extraIcon = suggestionState.extra_icon.AsNSImage();
    [suggestContainer_ showInputField:extraText withIcon:extraIcon];
  }
  [view_ setShouldHighlightOnHover:showSuggestions];
  [view_ setHidden:!delegate_->SectionIsActive(section_)];
}

- (void)update {
  [self updateAndClobber:YES];
}

- (void)fillForInput:(const autofill::DetailInput&)input {
  // Make sure to overwrite the originating input if it is a text field.
  AutofillTextField* field =
      base::mac::ObjCCast<AutofillTextField>([inputs_ viewWithTag:input.type]);
  [field setFieldValue:@""];

  if (ShouldOverwriteComboboxes(section_, input.type)) {
    for (NSControl* control in [inputs_ subviews]) {
      AutofillPopUpButton* popup =
          base::mac::ObjCCast<AutofillPopUpButton>(control);
      if (popup) {
        autofill::ServerFieldType fieldType =
            [self fieldTypeForControl:popup];
        if (autofill::AutofillType(fieldType).group() ==
                autofill::CREDIT_CARD) {
          ui::ComboboxModel* model =
              delegate_->ComboboxModelForAutofillType(fieldType);
          DCHECK(model);
          [popup selectItemAtIndex:model->GetDefaultIndex()];
        }
      }
    }
  }

  [self updateAndClobber:NO];
}

- (void)editLinkClicked {
  delegate_->EditClickedForSection(section_);
}

- (NSString*)editLinkTitle {
  return base::SysUTF16ToNSString(delegate_->EditSuggestionText());
}

- (BOOL)validateFor:(autofill::ValidationType)validationType {
  DCHECK(![[self view] isHidden]);

  NSArray* fields = nil;
  if (![inputs_ isHidden]) {
    fields = [inputs_ subviews];
  } else if (section_ == autofill::SECTION_CC) {
    fields = @[[suggestContainer_ inputField]];
  }

  autofill::DetailOutputMap detailOutputs;
  [self fillDetailOutputs:&detailOutputs fromControls:fields];
  autofill::ValidityData invalidInputs = delegate_->InputsAreValid(
      section_, detailOutputs, validationType);

  for (NSControl<AutofillInputField>* input in fields) {
    const autofill::ServerFieldType type = [self fieldTypeForControl:input];
    if (invalidInputs.count(type))
      [input setValidityMessage:base::SysUTF16ToNSString(invalidInputs[type])];
    else
      [input setValidityMessage:@""];
    [validationDelegate_ updateMessageForField:input];
  }

  return invalidInputs.empty();
}

#pragma mark Internal API for AutofillSectionContainer.

- (void)textfieldEditedOrActivated:(NSControl<AutofillInputField>*)field
                            edited:(BOOL)edited {
  AutofillTextField* textfield =
      base::mac::ObjCCastStrict<AutofillTextField>(field);

  // This only applies to textfields.
  if (!textfield)
    return;

  autofill::ServerFieldType type = [self fieldTypeForControl:field];
  string16 fieldValue = base::SysNSStringToUTF16([textfield fieldValue]);

  // Get the frame rectangle for the designated field, in screen coordinates.
  NSRect textFrameInScreen = [field convertRect:[field frame] toView:nil];
  textFrameInScreen.origin =
      [[field window] convertBaseToScreen:textFrameInScreen.origin];

  // And adjust for gfx::Rect being flipped compared to OSX coordinates.
  NSScreen* screen = [[NSScreen screens] objectAtIndex:0];
  textFrameInScreen.origin.y =
      NSMaxY([screen frame]) - NSMaxY(textFrameInScreen);
  gfx::Rect textFrameRect(NSRectToCGRect(textFrameInScreen));

  delegate_->UserEditedOrActivatedInput(section_,
                                          [self detailInputForType:type],
                                          [self view],
                                          textFrameRect,
                                          fieldValue,
                                          edited);

  // If the field is marked as invalid, check if the text is now valid.
  // Many fields (i.e. CC#) are invalid for most of the duration of editing,
  // so flagging them as invalid prematurely is not helpful. However,
  // correcting a minor mistake (i.e. a wrong CC digit) should immediately
  // result in validation - positive user feedback.
  if ([textfield invalid]) {
    string16 message = delegate_->InputValidityMessage(section_,
                                                         type,
                                                         fieldValue);
    [textfield setValidityMessage:base::SysUTF16ToNSString(message)];
    [validationDelegate_ updateMessageForField:textfield];

    // If the field transitioned from invalid to valid, re-validate the group,
    // since inter-field checks become meaningful with valid fields.
    if (![textfield invalid])
      [self validateFor:autofill::VALIDATE_EDIT];
  }

  // Update the icon for the textfield.
  gfx::Image icon = delegate_->IconForField(type, fieldValue);
  if (!icon.IsEmpty()) {
    [[textfield cell] setIcon:icon.ToNSImage()];
  }
}

- (autofill::ServerFieldType)fieldTypeForControl:(NSControl*)control {
  DCHECK([control tag]);
  return static_cast<autofill::ServerFieldType>([control tag]);
}

- (const autofill::DetailInput*)detailInputForType:
    (autofill::ServerFieldType)type {
  for (size_t i = 0; i < detailInputs_.size(); ++i) {
    if (detailInputs_[i]->type == type)
      return detailInputs_[i];
  }
  // TODO(groby): Needs to be NOTREACHED. Can't, due to the fact that tests
  // blindly call setFieldValue:forInput:, even for non-existing inputs.
  return NULL;
}

- (void)fillDetailOutputs:(autofill::DetailOutputMap*)outputs
             fromControls:(NSArray*)controls {
  for (NSControl<AutofillInputField>* input in controls) {
    DCHECK([input isKindOfClass:[NSControl class]]);
    DCHECK([input conformsToProtocol:@protocol(AutofillInputField)]);
    autofill::ServerFieldType fieldType = [self fieldTypeForControl:input];
    DCHECK([self detailInputForType:fieldType]);
    NSString* value = [input fieldValue];
    outputs->insert(std::make_pair([self detailInputForType:fieldType],
                                   base::SysNSStringToUTF16(value)));
  }
}

- (NSTextField*)makeDetailSectionLabel:(NSString*)labelText {
  base::scoped_nsobject<NSTextField> label([[NSTextField alloc] init]);
  [label setFont:
      [[NSFontManager sharedFontManager] convertFont:[label font]
                                         toHaveTrait:NSBoldFontMask]];
  [label setStringValue:labelText];
  [label setEditable:NO];
  [label setBordered:NO];
  [label sizeToFit];
  return label.autorelease();
}

- (void)updateAndClobber:(BOOL)shouldClobber {
  const autofill::DetailInputs& updatedInputs =
      delegate_->RequestedFieldsForSection(section_);

  for (autofill::DetailInputs::const_iterator iter = updatedInputs.begin();
       iter != updatedInputs.end();
       ++iter) {
    NSControl<AutofillInputField>* field = [inputs_ viewWithTag:iter->type];
    DCHECK(field);

    [field setEnabled:iter->editable];

    // TODO(groby): For comboboxes, "empty" means "set to default index"
    if (shouldClobber || [[field fieldValue] length] == 0) {
      [field setFieldValue:base::SysUTF16ToNSString(iter->initial_value)];
      AutofillTextField* textField =
          base::mac::ObjCCast<AutofillTextField>(field);
      if (textField) {
        gfx::Image icon =
            delegate_->IconForField(iter->type, iter->initial_value);
        if (!icon.IsEmpty()) {
          [[textField cell] setIcon:icon.ToNSImage()];
        }
      }
    }
  }
  [self modelChanged];
}

- (MenuButton*)makeSuggestionButton {
  base::scoped_nsobject<MenuButton> button([[MenuButton alloc] init]);

  [button setOpenMenuOnClick:YES];
  [button setBordered:NO];
  [button setShowsBorderOnlyWhileMouseInside:YES];

  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  NSImage* image =
      rb.GetNativeImageNamed(IDR_AUTOFILL_DIALOG_MENU_BUTTON).ToNSImage();
  [[button cell] setImage:image
             forButtonState:image_button_cell::kDefaultState];
  image = rb.GetNativeImageNamed(IDR_AUTOFILL_DIALOG_MENU_BUTTON_H).
      ToNSImage();
  [[button cell] setImage:image
             forButtonState:image_button_cell::kHoverState];
  image = rb.GetNativeImageNamed(IDR_AUTOFILL_DIALOG_MENU_BUTTON_P).
      ToNSImage();
  [[button cell] setImage:image
             forButtonState:image_button_cell::kPressedState];
  image = rb.GetNativeImageNamed(IDR_AUTOFILL_DIALOG_MENU_BUTTON_D).
      ToNSImage();
  [[button cell] setImage:image
             forButtonState:image_button_cell::kDisabledState];

  [button sizeToFit];
  return button.autorelease();
}

// TODO(estade): we should be using Chrome-style constrained window padding
// values.
- (LayoutView*)makeInputControls {
  base::scoped_nsobject<LayoutView> view([[LayoutView alloc] init]);
  [view setLayoutManager:
      scoped_ptr<SimpleGridLayout>(new SimpleGridLayout(view))];
  SimpleGridLayout* layout = [view layoutManager];

  // Reverse order of rows, but keep order of fields stable. stable_sort
  // guarantees that field order within a row is not affected.
  // Necessary since OSX builds forms from the bottom left.
  std::stable_sort(
      detailInputs_.begin(), detailInputs_.end(), CompareInputRows);
  for (size_t i = 0; i < detailInputs_.size(); ++i) {
    const autofill::DetailInput& input = *detailInputs_[i];
    int kColumnSetId = input.row_id;
    ColumnSet* column_set = layout->GetColumnSet(kColumnSetId);
    if (!column_set) {
      // Create a new column set and row.
      column_set = layout->AddColumnSet(kColumnSetId);
      if (i != 0 && kColumnSetId != -1)
        layout->AddPaddingRow(kRelatedControlVerticalSpacing);
      layout->StartRow(0, kColumnSetId);
    } else {
      // Add a new column to existing row.
      column_set->AddPaddingColumn(kRelatedControlHorizontalSpacing);
      // Must explicitly skip the padding column since we've already started
      // adding views.
      layout->SkipColumns(1);
    }

    column_set->AddColumn(input.expand_weight ? input.expand_weight : 1.0f);

    ui::ComboboxModel* input_model =
        delegate_->ComboboxModelForAutofillType(input.type);
    base::scoped_nsprotocol<NSControl<AutofillInputField>*> control;
    if (input_model) {
      base::scoped_nsobject<AutofillPopUpButton> popup(
          [[AutofillPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO]);
      for (int i = 0; i < input_model->GetItemCount(); ++i) {
        [popup addItemWithTitle:
            base::SysUTF16ToNSString(input_model->GetItemAt(i))];
      }
      control.reset(popup.release());
    } else {
      base::scoped_nsobject<AutofillTextField> field(
          [[AutofillTextField alloc] init]);
      [[field cell] setPlaceholderString:
          l10n_util::GetNSStringWithFixup(input.placeholder_text_rid)];
      [[field cell] setIcon:
          delegate_->IconForField(
              input.type, input.initial_value).AsNSImage()];
      control.reset(field.release());
    }
    [control setFieldValue:base::SysUTF16ToNSString(input.initial_value)];
    [control sizeToFit];
    [control setTag:input.type];
    [control setDelegate:self];
    // Hide away fields that cannot be edited.
    if (kColumnSetId == -1) {
      [control setFrame:NSZeroRect];
      [control setHidden:YES];
    }
    layout->AddView(control);
  }

  return view.autorelease();
}

@end


@implementation AutofillSectionContainer (ForTesting)

- (NSControl*)getField:(autofill::ServerFieldType)type {
  return [inputs_ viewWithTag:type];
}

- (void)setFieldValue:(NSString*)text
             forInput:(const autofill::DetailInput&)input {
  if ([self detailInputForType:input.type] != &input)
    return;

  NSControl<AutofillInputField>* field = [inputs_ viewWithTag:input.type];
  [field setFieldValue:text];
}

- (void)setSuggestionFieldValue:(NSString*)text {
  [[suggestContainer_ inputField] setFieldValue:text];
}

- (void)activateFieldForInput:(const autofill::DetailInput&)input {
  if ([self detailInputForType:input.type] != &input)
    return;

  NSControl<AutofillInputField>* field = [inputs_ viewWithTag:input.type];
  [[field window] makeFirstResponder:field];
}

@end
