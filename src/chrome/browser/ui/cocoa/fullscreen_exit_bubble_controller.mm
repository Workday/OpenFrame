// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <Cocoa/Cocoa.h>

#include "base/logging.h"  // for NOTREACHED()
#include "base/mac/bundle_locations.h"
#include "base/mac/mac_util.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#import "chrome/browser/ui/cocoa/browser_window_controller.h"
#import "chrome/browser/ui/cocoa/fullscreen_exit_bubble_controller.h"
#import "chrome/browser/ui/cocoa/hyperlink_text_view.h"
#import "chrome/browser/ui/cocoa/info_bubble_view.h"
#import "chrome/browser/ui/cocoa/info_bubble_window.h"
#include "chrome/browser/ui/fullscreen/fullscreen_controller.h"
#include "chrome/browser/ui/fullscreen/fullscreen_exit_bubble_type.h"
#include "grit/generated_resources.h"
#include "grit/ui_strings.h"
#import "third_party/GTM/AppKit/GTMNSAnimation+Duration.h"
#include "third_party/GTM/AppKit/GTMUILocalizerAndLayoutTweaker.h"
#import "third_party/GTM/AppKit/GTMUILocalizerAndLayoutTweaker.h"
#include "ui/base/accelerators/platform_accelerator_cocoa.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/l10n/l10n_util_mac.h"


namespace {
const float kInitialDelay = 3.8;
const float kHideDuration = 0.7;
} // namespace

@interface OneClickHyperlinkTextView : HyperlinkTextView
@end
@implementation OneClickHyperlinkTextView
- (BOOL)acceptsFirstMouse:(NSEvent*)event {
  return YES;
}
@end

@interface FullscreenExitBubbleController (PrivateMethods)
// Sets |exitLabel_| based on |exitLabelPlaceholder_|,
// sets |exitLabelPlaceholder_| to nil.
- (void)initializeLabel;

- (NSString*)getLabelText;

- (void)hideSoon;

// Returns the Accelerator for the Toggle Fullscreen menu item.
+ (scoped_ptr<ui::PlatformAcceleratorCocoa>)acceleratorForToggleFullscreen;

// Returns a string representation fit for display of
// +acceleratorForToggleFullscreen.
+ (NSString*)keyCommandString;

+ (NSString*)keyCombinationForAccelerator:
    (const ui::PlatformAcceleratorCocoa&)item;
@end

@implementation FullscreenExitBubbleController

- (id)initWithOwner:(BrowserWindowController*)owner
            browser:(Browser*)browser
                url:(const GURL&)url
         bubbleType:(FullscreenExitBubbleType)bubbleType {
  NSString* nibPath =
      [base::mac::FrameworkBundle() pathForResource:@"FullscreenExitBubble"
                                             ofType:@"nib"];
  if ((self = [super initWithWindowNibPath:nibPath owner:self])) {
    browser_ = browser;
    owner_ = owner;
    url_ = url;
    bubbleType_ = bubbleType;
    // Mouse lock expects mouse events to reach the main window immediately.
    // Make the bubble transparent for mouse events if mouse lock is enabled.
    if (bubbleType_ == FEB_TYPE_FULLSCREEN_MOUSELOCK_EXIT_INSTRUCTION ||
        bubbleType_ == FEB_TYPE_MOUSELOCK_EXIT_INSTRUCTION)
      [[self window] setIgnoresMouseEvents:YES];
  }
  return self;
}

- (void)allow:(id)sender {
  // The mouselock code expects that mouse events reach the main window
  // immediately, but the cursor is still over the bubble, which eats the
  // mouse events. Make the bubble transparent for mouse events.
  if (bubbleType_ == FEB_TYPE_FULLSCREEN_MOUSELOCK_BUTTONS ||
      bubbleType_ == FEB_TYPE_MOUSELOCK_BUTTONS)
    [[self window] setIgnoresMouseEvents:YES];

  DCHECK(fullscreen_bubble::ShowButtonsForType(bubbleType_));
  browser_->fullscreen_controller()->OnAcceptFullscreenPermission();
}

- (void)deny:(id)sender {
  DCHECK(fullscreen_bubble::ShowButtonsForType(bubbleType_));
  browser_->fullscreen_controller()->OnDenyFullscreenPermission();
}

- (void)showButtons:(BOOL)show {
  [allowButton_ setHidden:!show];
  [denyButton_ setHidden:!show];
  [exitLabel_ setHidden:show];
}

// We want this to be a child of a browser window.  addChildWindow:
// (called from this function) will bring the window on-screen;
// unfortunately, [NSWindowController showWindow:] will also bring it
// on-screen (but will cause unexpected changes to the window's
// position).  We cannot have an addChildWindow: and a subsequent
// showWindow:. Thus, we have our own version.
- (void)showWindow {
  // Completes nib load.
  InfoBubbleWindow* info_bubble = static_cast<InfoBubbleWindow*>([self window]);
  [info_bubble setCanBecomeKeyWindow:NO];
  if (!fullscreen_bubble::ShowButtonsForType(bubbleType_)) {
    [self showButtons:NO];
    [self hideSoon];
  }
  [tweaker_ tweakUI:info_bubble];
  [[owner_ window] addChildWindow:info_bubble ordered:NSWindowAbove];
  [owner_ layoutSubviews];

  [info_bubble orderFront:self];
}

- (void)awakeFromNib {
  DCHECK([[self window] isKindOfClass:[InfoBubbleWindow class]]);
  [messageLabel_ setStringValue:[self getLabelText]];
  [self initializeLabel];
}

- (void)positionInWindowAtTop:(CGFloat)maxY width:(CGFloat)maxWidth {
  NSRect windowFrame = [self window].frame;
  NSRect ownerWindowFrame = [owner_ window].frame;
  NSPoint origin;
  origin.x = ownerWindowFrame.origin.x +
      (int)(NSWidth(ownerWindowFrame)/2 - NSWidth(windowFrame)/2);
  origin.y = ownerWindowFrame.origin.y + maxY - NSHeight(windowFrame);
  [[self window] setFrameOrigin:origin];
}

// Called when someone clicks on the embedded link.
- (BOOL) textView:(NSTextView*)textView
    clickedOnLink:(id)link
          atIndex:(NSUInteger)charIndex {
  browser_->fullscreen_controller()->
      ExitTabOrBrowserFullscreenToPreviousState();
  return YES;
}

- (void)hideTimerFired:(NSTimer*)timer {
  // This might fire racily for buttoned bubbles, even though the timer is
  // cancelled for them. Explicitly check for this case.
  if (fullscreen_bubble::ShowButtonsForType(bubbleType_))
    return;

  [NSAnimationContext beginGrouping];
  [[NSAnimationContext currentContext]
      gtm_setDuration:kHideDuration
            eventMask:NSLeftMouseUpMask|NSLeftMouseDownMask];
  [[[self window] animator] setAlphaValue:0.0];
  [NSAnimationContext endGrouping];
}

- (void)animationDidEnd:(NSAnimation*)animation {
  if (animation == hideAnimation_.get()) {
    hideAnimation_.reset();
  }
}

- (void)closeImmediately {
  // Without this, quitting fullscreen with esc will let the bubble reappear
  // once the "exit fullscreen" animation is done on lion.
  InfoBubbleWindow* infoBubble = static_cast<InfoBubbleWindow*>([self window]);
  [[infoBubble parentWindow] removeChildWindow:infoBubble];
  [hideAnimation_.get() stopAnimation];
  [hideTimer_ invalidate];
  [infoBubble setAllowedAnimations:info_bubble::kAnimateNone];
  [self close];
}

- (void)dealloc {
  [hideAnimation_.get() stopAnimation];
  [hideTimer_ invalidate];
  [super dealloc];
}

@end

@implementation FullscreenExitBubbleController (PrivateMethods)

- (void)initializeLabel {
  // Replace the label placeholder NSTextField with the real label NSTextView.
  // The former doesn't show links in a nice way, but the latter can't be added
  // in IB without a containing scroll view, so create the NSTextView
  // programmatically.
  exitLabel_.reset([[OneClickHyperlinkTextView alloc]
      initWithFrame:[exitLabelPlaceholder_ frame]]);
  [exitLabel_.get() setAutoresizingMask:
      [exitLabelPlaceholder_ autoresizingMask]];
  [exitLabel_.get() setHidden:[exitLabelPlaceholder_ isHidden]];
  [[exitLabelPlaceholder_ superview]
      replaceSubview:exitLabelPlaceholder_ with:exitLabel_.get()];
  exitLabelPlaceholder_ = nil;  // Now released.
  [exitLabel_.get() setDelegate:self];

  NSString* exitLinkText;
  NSString* exitUnlinkedText;
  if (bubbleType_ == FEB_TYPE_FULLSCREEN_MOUSELOCK_EXIT_INSTRUCTION ||
      bubbleType_ == FEB_TYPE_MOUSELOCK_EXIT_INSTRUCTION) {
    exitLinkText = @"";
    exitUnlinkedText = [@" " stringByAppendingString:
        l10n_util::GetNSStringF(IDS_FULLSCREEN_PRESS_ESC_TO_EXIT,
                                l10n_util::GetStringUTF16(IDS_APP_ESC_KEY))];
  } else {
    exitLinkText = l10n_util::GetNSString(IDS_EXIT_FULLSCREEN_MODE);
    exitUnlinkedText = [@" " stringByAppendingString:
        l10n_util::GetNSStringF(IDS_EXIT_FULLSCREEN_MODE_ACCELERATOR,
                                l10n_util::GetStringUTF16(IDS_APP_ESC_KEY))];
  }

  NSFont* font = [NSFont systemFontOfSize:
      [NSFont systemFontSizeForControlSize:NSRegularControlSize]];
  [(HyperlinkTextView*)exitLabel_.get()
        setMessageAndLink:exitUnlinkedText
                 withLink:exitLinkText
                 atOffset:0
                     font:font
             messageColor:[NSColor blackColor]
                linkColor:[NSColor blueColor]];
  [exitLabel_.get() setAlignment:NSRightTextAlignment];

  NSRect labelFrame = [exitLabel_ frame];

  // NSTextView's sizeToFit: method seems to enjoy wrapping lines. Temporarily
  // set the size large to force it not to.
  NSRect windowFrame = [[self window] frame];
  [exitLabel_ setFrameSize:windowFrame.size];
  NSLayoutManager* layoutManager = [exitLabel_ layoutManager];
  NSTextContainer* textContainer = [exitLabel_ textContainer];
  [layoutManager ensureLayoutForTextContainer:textContainer];
  NSRect textFrame = [layoutManager usedRectForTextContainer:textContainer];

  textFrame.size.width = ceil(NSWidth(textFrame));
  labelFrame.origin.x += NSWidth(labelFrame) - NSWidth(textFrame);
  labelFrame.size = textFrame.size;
  [exitLabel_ setFrame:labelFrame];
}

- (NSString*)getLabelText {
  if (bubbleType_ == FEB_TYPE_NONE)
    return @"";
  return SysUTF16ToNSString(fullscreen_bubble::GetLabelTextForType(
          bubbleType_, url_, browser_->profile()->GetExtensionService()));
}

// This looks at the Main Menu and determines what the user has set as the
// key combination for quit. It then gets the modifiers and builds an object
// to hold the data.
+ (scoped_ptr<ui::PlatformAcceleratorCocoa>)acceleratorForToggleFullscreen {
  NSMenu* mainMenu = [NSApp mainMenu];
  // Get the application menu (i.e. Chromium).
  for (NSMenuItem* menu in [mainMenu itemArray]) {
    for (NSMenuItem* item in [[menu submenu] itemArray]) {
      // Find the toggle presentation mode item.
      if ([item tag] == IDC_PRESENTATION_MODE) {
        return scoped_ptr<ui::PlatformAcceleratorCocoa>(
          new ui::PlatformAcceleratorCocoa([item keyEquivalent],
                                           [item keyEquivalentModifierMask]));
      }
    }
  }
  // Default to Cmd+Shift+F.
  return scoped_ptr<ui::PlatformAcceleratorCocoa>(
      new ui::PlatformAcceleratorCocoa(@"f", NSCommandKeyMask|NSShiftKeyMask));
}

// This looks at the Main Menu and determines what the user has set as the
// key combination for quit. It then gets the modifiers and builds a string
// to display them.
+ (NSString*)keyCommandString {
  scoped_ptr<ui::PlatformAcceleratorCocoa> accelerator(
      [[self class] acceleratorForToggleFullscreen]);
  return [[self class] keyCombinationForAccelerator:*accelerator];
}

+ (NSString*)keyCombinationForAccelerator:
    (const ui::PlatformAcceleratorCocoa&)item {
  NSMutableString* string = [NSMutableString string];
  NSUInteger modifiers = item.modifier_mask();

  if (modifiers & NSCommandKeyMask)
    [string appendString:@"\u2318"];
  if (modifiers & NSControlKeyMask)
    [string appendString:@"\u2303"];
  if (modifiers & NSAlternateKeyMask)
    [string appendString:@"\u2325"];
  BOOL isUpperCase = [[NSCharacterSet uppercaseLetterCharacterSet]
      characterIsMember:[item.characters() characterAtIndex:0]];
  if (modifiers & NSShiftKeyMask || isUpperCase)
    [string appendString:@"\u21E7"];

  [string appendString:[item.characters() uppercaseString]];
  return string;
}

- (void)hideSoon {
  hideTimer_.reset(
      [[NSTimer scheduledTimerWithTimeInterval:kInitialDelay
                                        target:self
                                      selector:@selector(hideTimerFired:)
                                      userInfo:nil
                                       repeats:NO] retain]);
}

@end
