// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/cocoa/autofill/autofill_dialog_cocoa.h"

#include "base/bind.h"
#include "base/mac/bundle_locations.h"
#include "base/mac/scoped_nsobject.h"
#include "base/message_loop/message_loop.h"
#include "base/strings/sys_string_conversions.h"
#include "chrome/browser/ui/autofill/autofill_dialog_view_delegate.h"
#include "chrome/browser/ui/chrome_style.h"
#include "chrome/browser/ui/chrome_style.h"
#include "chrome/browser/ui/chrome_style.h"
#import "chrome/browser/ui/cocoa/autofill/autofill_account_chooser.h"
#import "chrome/browser/ui/cocoa/autofill/autofill_details_container.h"
#include "chrome/browser/ui/cocoa/autofill/autofill_dialog_constants.h"
#import "chrome/browser/ui/cocoa/autofill/autofill_main_container.h"
#import "chrome/browser/ui/cocoa/autofill/autofill_section_container.h"
#import "chrome/browser/ui/cocoa/autofill/autofill_sign_in_container.h"
#import "chrome/browser/ui/cocoa/constrained_window/constrained_window_button.h"
#import "chrome/browser/ui/cocoa/constrained_window/constrained_window_custom_sheet.h"
#import "chrome/browser/ui/cocoa/constrained_window/constrained_window_custom_window.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#import "ui/base/cocoa/flipped_view.h"
#include "ui/base/cocoa/window_size_constants.h"

namespace {

const CGFloat kAccountChooserHeight = 20.0;
const CGFloat kRelatedControlVerticalSpacing = 8.0;

}  // namespace;

namespace autofill {

// static
AutofillDialogView* AutofillDialogView::Create(
    AutofillDialogViewDelegate* delegate) {
  return new AutofillDialogCocoa(delegate);
}

AutofillDialogCocoa::AutofillDialogCocoa(AutofillDialogViewDelegate* delegate)
    : close_weak_ptr_factory_(this),
      delegate_(delegate) {
}

AutofillDialogCocoa::~AutofillDialogCocoa() {
}

void AutofillDialogCocoa::Show() {
  // This should only be called once.
  DCHECK(!sheet_delegate_.get());
  sheet_delegate_.reset([[AutofillDialogWindowController alloc]
       initWithWebContents:delegate_->GetWebContents()
            autofillDialog:this]);
  base::scoped_nsobject<CustomConstrainedWindowSheet> sheet(
      [[CustomConstrainedWindowSheet alloc]
          initWithCustomWindow:[sheet_delegate_ window]]);
  constrained_window_.reset(
      new ConstrainedWindowMac(this, delegate_->GetWebContents(), sheet));
}

void AutofillDialogCocoa::Hide() {
  [sheet_delegate_ hide];
}

void AutofillDialogCocoa::PerformClose() {
  if (!close_weak_ptr_factory_.HasWeakPtrs()) {
    base::MessageLoop::current()->PostTask(
        FROM_HERE,
        base::Bind(&AutofillDialogCocoa::CloseNow,
                   close_weak_ptr_factory_.GetWeakPtr()));
  }
}

void AutofillDialogCocoa::CloseNow() {
  constrained_window_->CloseWebContentsModalDialog();
}

void AutofillDialogCocoa::UpdateAccountChooser() {
  [sheet_delegate_ updateAccountChooser];
}

void AutofillDialogCocoa::UpdateButtonStrip() {
}

void AutofillDialogCocoa::UpdateDetailArea() {
}

void AutofillDialogCocoa::UpdateForErrors() {
}

void AutofillDialogCocoa::UpdateNotificationArea() {
  [sheet_delegate_ updateNotificationArea];
}

void AutofillDialogCocoa::UpdateAutocheckoutStepsArea() {
}

void AutofillDialogCocoa::UpdateSection(DialogSection section) {
  [sheet_delegate_ updateSection:section];
}

void AutofillDialogCocoa::FillSection(DialogSection section,
                                      const DetailInput& originating_input) {
  [sheet_delegate_ fillSection:section forInput:originating_input];
}

void AutofillDialogCocoa::GetUserInput(DialogSection section,
                                       DetailOutputMap* output) {
  [sheet_delegate_ getInputs:output forSection:section];
}

string16 AutofillDialogCocoa::GetCvc() {
  return string16();
}

bool AutofillDialogCocoa::SaveDetailsLocally() {
  return [sheet_delegate_ saveDetailsLocally];
}

const content::NavigationController* AutofillDialogCocoa::ShowSignIn() {
  return [sheet_delegate_ showSignIn];
}

void AutofillDialogCocoa::HideSignIn() {
  [sheet_delegate_ hideSignIn];
}

void AutofillDialogCocoa::UpdateProgressBar(double value) {}

void AutofillDialogCocoa::ModelChanged() {
  [sheet_delegate_ modelChanged];
}

void AutofillDialogCocoa::OnSignInResize(const gfx::Size& pref_size) {
  // TODO(groby): Implement Mac support for this.
}

TestableAutofillDialogView* AutofillDialogCocoa::GetTestableView() {
  return this;
}

void AutofillDialogCocoa::SubmitForTesting() {
  [sheet_delegate_ accept:nil];
}

void AutofillDialogCocoa::CancelForTesting() {
  [sheet_delegate_ cancel:nil];
}

string16 AutofillDialogCocoa::GetTextContentsOfInput(const DetailInput& input) {
  for (size_t i = SECTION_MIN; i <= SECTION_MAX; ++i) {
    DialogSection section = static_cast<DialogSection>(i);
    DetailOutputMap contents;
    [sheet_delegate_ getInputs:&contents forSection:section];
    DetailOutputMap::const_iterator it = contents.find(&input);
    if (it != contents.end())
      return it->second;
  }

  NOTREACHED();
  return string16();
}

void AutofillDialogCocoa::SetTextContentsOfInput(const DetailInput& input,
                                                 const string16& contents) {
  [sheet_delegate_ setTextContents:base::SysUTF16ToNSString(contents)
                            forInput:input];
}

void AutofillDialogCocoa::SetTextContentsOfSuggestionInput(
    DialogSection section,
    const base::string16& text) {
  [sheet_delegate_ setTextContents:base::SysUTF16ToNSString(text)
              ofSuggestionForSection:section];
}

void AutofillDialogCocoa::ActivateInput(const DetailInput& input) {
  [sheet_delegate_ activateFieldForInput:input];
}

gfx::Size AutofillDialogCocoa::GetSize() const {
  return gfx::Size(NSSizeToCGSize([[sheet_delegate_ window] frame].size));
}

void AutofillDialogCocoa::OnConstrainedWindowClosed(
    ConstrainedWindowMac* window) {
  constrained_window_.reset();
  // |this| belongs to |delegate_|, so no self-destruction here.
  delegate_->ViewClosed();
}

}  // autofill

#pragma mark Window Controller

@interface AutofillDialogWindowController ()

// Notification that the WebContent's view frame has changed.
- (void)onContentViewFrameDidChange:(NSNotification*)notification;

@end

@implementation AutofillDialogWindowController

- (id)initWithWebContents:(content::WebContents*)webContents
      autofillDialog:(autofill::AutofillDialogCocoa*)autofillDialog {
  DCHECK(webContents);

  base::scoped_nsobject<ConstrainedWindowCustomWindow> window(
      [[ConstrainedWindowCustomWindow alloc]
          initWithContentRect:ui::kWindowSizeDeterminedLater]);

  if ((self = [super initWithWindow:window])) {
    webContents_ = webContents;
    autofillDialog_ = autofillDialog;

    mainContainer_.reset([[AutofillMainContainer alloc]
                             initWithDelegate:autofillDialog->delegate()]);
    [mainContainer_ setTarget:self];

    signInContainer_.reset(
        [[AutofillSignInContainer alloc]
            initWithDelegate:autofillDialog->delegate()]);
    [[signInContainer_ view] setHidden:YES];

    NSRect clientRect = [[mainContainer_ view] frame];
    clientRect.origin = NSMakePoint(chrome_style::kClientBottomPadding,
                                    chrome_style::kHorizontalPadding);
    [[mainContainer_ view] setFrame:clientRect];
    [[signInContainer_ view] setFrame:clientRect];

    NSRect headerRect = clientRect;
    headerRect.size.height = kAccountChooserHeight;
    headerRect.origin.y = NSMaxY(clientRect);
    accountChooser_.reset([[AutofillAccountChooser alloc]
                              initWithFrame:headerRect
                                 delegate:autofillDialog->delegate()]);

    // This needs a flipped content view because otherwise the size
    // animation looks odd. However, replacing the contentView for constrained
    // windows does not work - it does custom rendering.
    base::scoped_nsobject<NSView> flippedContentView(
        [[FlippedView alloc] initWithFrame:NSZeroRect]);
    [flippedContentView setSubviews:
        @[accountChooser_, [mainContainer_ view], [signInContainer_ view]]];
    [flippedContentView setAutoresizingMask:
        (NSViewWidthSizable | NSViewHeightSizable)];
    [[[self window] contentView] addSubview:flippedContentView];
    [mainContainer_ setAnchorView:[[accountChooser_ subviews] objectAtIndex:1]];

    NSRect contentRect = clientRect;
    contentRect.origin = NSZeroPoint;
    contentRect.size.width += 2 * chrome_style::kHorizontalPadding;
    contentRect.size.height += NSHeight(headerRect) +
                               chrome_style::kClientBottomPadding +
                               chrome_style::kTitleTopPadding;
    [self performLayout];

    // Resizing the browser causes the ConstrainedWindow to move.
    // Observe that to allow resizes based on browser size.
    NSView* contentView = [[self window] contentView];
    [contentView setPostsFrameChangedNotifications:YES];
    [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(onContentViewFrameDidChange:)
               name:NSWindowDidMoveNotification
             object:[self window]];
  }
  return self;
}

- (void)dealloc {
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [super dealloc];
}

- (void)onContentViewFrameDidChange:(NSNotification*)notification {
  [self performLayout];
}

- (void)requestRelayout {
  [self performLayout];
}

- (NSSize)preferredSize {
  NSSize contentSize;
  // TODO(groby): Currently, keep size identical to main container.
  // Change to allow autoresize of web contents.
  contentSize = [mainContainer_ preferredSize];

  NSSize headerSize = NSMakeSize(contentSize.width, kAccountChooserHeight);
  NSSize size = NSMakeSize(
      std::max(contentSize.width, headerSize.width),
      contentSize.height + headerSize.height + kDetailTopPadding);
  size.width += 2 * chrome_style::kHorizontalPadding;
  size.height += chrome_style::kClientBottomPadding +
                 chrome_style::kTitleTopPadding;

  // Show as much of the main view as is possible without going past the
  // bottom of the browser window.
  NSRect dialogFrameRect = [[self window] frame];
  NSRect browserFrameRect =
      [webContents_->GetView()->GetTopLevelNativeWindow() frame];
  dialogFrameRect.size.height =
      NSMaxY(dialogFrameRect) - NSMinY(browserFrameRect);
  dialogFrameRect = [[self window] contentRectForFrameRect:dialogFrameRect];
  size.height = std::min(NSHeight(dialogFrameRect), size.height);

  return size;
}

- (void)performLayout {
  NSRect contentRect = NSZeroRect;
  contentRect.size = [self preferredSize];
  NSRect clientRect = NSInsetRect(
      contentRect, chrome_style::kHorizontalPadding, 0);
  clientRect.origin.y = chrome_style::kClientBottomPadding;
  clientRect.size.height -= chrome_style::kTitleTopPadding +
                            chrome_style::kClientBottomPadding;

  NSRect headerRect, mainRect, dummyRect;
  NSDivideRect(clientRect, &headerRect, &mainRect,
               kAccountChooserHeight, NSMinYEdge);
  NSDivideRect(mainRect, &dummyRect, &mainRect,
               kDetailTopPadding, NSMinYEdge);

  [accountChooser_ setFrame:headerRect];
  if ([[signInContainer_ view] isHidden]) {
    [[mainContainer_ view] setFrame:mainRect];
    [mainContainer_ performLayout];
  } else {
    [[signInContainer_ view] setFrame:mainRect];
  }

  NSRect frameRect = [[self window] frameRectForContentRect:contentRect];
  [[self window] setFrame:frameRect display:YES];
}

- (IBAction)accept:(id)sender {
  if ([mainContainer_ validate])
    autofillDialog_->delegate()->OnAccept();
}

- (IBAction)cancel:(id)sender {
  autofillDialog_->delegate()->OnCancel();
  autofillDialog_->PerformClose();
}

- (void)hide {
  autofillDialog_->delegate()->OnCancel();
  autofillDialog_->PerformClose();
}

- (void)updateNotificationArea {
  [mainContainer_ updateNotificationArea];
}

- (void)updateAccountChooser {
  [accountChooser_ update];
  [mainContainer_ updateLegalDocuments];
}

- (void)updateSection:(autofill::DialogSection)section {
  [[mainContainer_ sectionForId:section] update];
}

- (void)fillSection:(autofill::DialogSection)section
           forInput:(const autofill::DetailInput&)input {
  [[mainContainer_ sectionForId:section] fillForInput:input];
}

- (content::NavigationController*)showSignIn {
  [signInContainer_ loadSignInPage];
  [[mainContainer_ view] setHidden:YES];
  [[signInContainer_ view] setHidden:NO];
  [self performLayout];

  return [signInContainer_ navigationController];
}

- (void)getInputs:(autofill::DetailOutputMap*)output
       forSection:(autofill::DialogSection)section {
  [[mainContainer_ sectionForId:section] getInputs:output];
}

- (BOOL)saveDetailsLocally {
  return [mainContainer_ saveDetailsLocally];
}

- (void)hideSignIn {
  [[signInContainer_ view] setHidden:YES];
  [[mainContainer_ view] setHidden:NO];
  [self performLayout];
}

- (void)modelChanged {
  [mainContainer_ modelChanged];
}

@end


@implementation AutofillDialogWindowController (TestableAutofillDialogView)

- (void)setTextContents:(NSString*)text
               forInput:(const autofill::DetailInput&)input {
  for (size_t i = autofill::SECTION_MIN; i <= autofill::SECTION_MAX; ++i) {
    autofill::DialogSection section = static_cast<autofill::DialogSection>(i);
    // TODO(groby): Need to find the section for an input directly - wasteful.
    [[mainContainer_ sectionForId:section] setFieldValue:text forInput:input];
  }
}

- (void)setTextContents:(NSString*)text
 ofSuggestionForSection:(autofill::DialogSection)section {
  [[mainContainer_ sectionForId:section] setSuggestionFieldValue:text];
}

- (void)activateFieldForInput:(const autofill::DetailInput&)input {
  for (size_t i = autofill::SECTION_MIN; i <= autofill::SECTION_MAX; ++i) {
    autofill::DialogSection section = static_cast<autofill::DialogSection>(i);
    [[mainContainer_ sectionForId:section] activateFieldForInput:input];
  }
}

@end
