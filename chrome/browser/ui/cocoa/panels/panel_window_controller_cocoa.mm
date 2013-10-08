// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/cocoa/panels/panel_window_controller_cocoa.h"

#import <Cocoa/Cocoa.h>

#include "base/auto_reset.h"
#include "base/logging.h"
#include "base/mac/bundle_locations.h"
#include "base/mac/mac_util.h"
#include "base/mac/scoped_nsautorelease_pool.h"
#include "base/strings/sys_string_conversions.h"
#include "chrome/app/chrome_command_ids.h"  // IDC_*
#include "chrome/browser/chrome_browser_application_mac.h"
#include "chrome/browser/profiles/profile.h"
#import "chrome/browser/ui/cocoa/browser_command_executor.h"
#import "chrome/browser/ui/cocoa/browser_window_utils.h"
#import "chrome/browser/ui/cocoa/panels/mouse_drag_controller.h"
#import "chrome/browser/ui/cocoa/panels/panel_cocoa.h"
#import "chrome/browser/ui/cocoa/panels/panel_titlebar_view_cocoa.h"
#import "chrome/browser/ui/cocoa/panels/panel_utils_cocoa.h"
#import "chrome/browser/ui/cocoa/tab_contents/favicon_util_mac.h"
#import "chrome/browser/ui/cocoa/tab_contents/tab_contents_controller.h"
#import "chrome/browser/ui/cocoa/tabs/throbber_view.h"
#include "chrome/browser/ui/panels/panel_bounds_animation.h"
#include "chrome/browser/ui/panels/panel_collection.h"
#include "chrome/browser/ui/panels/panel_constants.h"
#include "chrome/browser/ui/panels/panel_manager.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/toolbar/encoding_menu_controller.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "grit/ui_resources.h"
#include "third_party/WebKit/public/web/WebCursorInfo.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/image/image.h"
#include "webkit/common/cursors/webcursor.h"

using content::WebContents;

const int kMinimumWindowSize = 1;
const double kBoundsAnimationSpeedPixelsPerSecond = 1000;
const double kBoundsAnimationMaxDurationSeconds = 0.18;

// Resize edge thickness, in screen pixels.
const double kWidthOfMouseResizeArea = 4.0;

@interface PanelWindowControllerCocoa (PanelsCanBecomeKey)
// Internal helper method for extracting the total number of panel windows
// from the panel manager. Used to decide if panel can become the key window.
- (int)numPanels;
@end

@implementation PanelWindowCocoaImpl
// The panels cannot be reduced to 3-px windows on the edge of the screen
// active area (above Dock). Default constraining logic makes at least a height
// of the titlebar visible, so the user could still grab it. We do 'restore'
// differently, and minimize panels to 3 px. Hence the need to override the
// constraining logic.
- (NSRect)constrainFrameRect:(NSRect)frameRect toScreen:(NSScreen *)screen {
  return frameRect;
}

// Prevent panel window from becoming key - for example when it is minimized.
// Panel windows use a higher priority NSWindowLevel to ensure they are always
// visible, causing the OS to prefer panel windows when selecting a window
// to make the key window. To counter this preference, we override
// -[NSWindow:canBecomeKeyWindow] to restrict when the panel can become the
// key window to a limited set of scenarios, such as when cycling through
// windows, when panels are the only remaining windows, when an event
// triggers window activation, etc. The panel may also be prevented from
// becoming the key window, regardless of the above scenarios, such as when
// a panel is minimized.
- (BOOL)canBecomeKeyWindow {
  // Give precedence to controller preventing activation of the window.
  PanelWindowControllerCocoa* controller =
      base::mac::ObjCCast<PanelWindowControllerCocoa>([self windowController]);
  if (![controller canBecomeKeyWindow])
    return NO;

  BrowserCrApplication* app = base::mac::ObjCCast<BrowserCrApplication>(
      [BrowserCrApplication sharedApplication]);

  // A Panel window can become the key window only in limited scenarios.
  // This prevents the system from always preferring a Panel window due
  // to its higher priority NSWindowLevel when selecting a window to make key.
  return ([app isHandlingSendEvent]  && [[app currentEvent] window] == self) ||
      [controller activationRequestedByPanel] ||
      [app isCyclingWindows] ||
      [app previousKeyWindow] == self ||
      [[app windows] count] == static_cast<NSUInteger>([controller numPanels]);
}

- (void)performMiniaturize:(id)sender {
  [[self windowController] minimizeButtonClicked:0];
}

// Ignore key events if window cannot become key window to fix problem
// where keyboard input is still going into a minimized panel even though
// the app has been deactivated in -[PanelWindowControllerCocoa deactivate:].
- (void)sendEvent:(NSEvent*)anEvent {
  NSEventType eventType = [anEvent type];
  if ((eventType == NSKeyDown || eventType == NSKeyUp) &&
      ![self canBecomeKeyWindow])
    return;
  [super sendEvent:anEvent];
}
@end

// Transparent view covering the whole panel in order to intercept mouse
// messages for custom user resizing. We need custom resizing because panels
// use their own constrained layout.
@interface PanelResizeByMouseOverlay : NSView <MouseDragControllerClient> {
 @private
   Panel* panel_;
   base::scoped_nsobject<MouseDragController> dragController_;
   base::scoped_nsobject<NSCursor> dragCursor_;
   base::scoped_nsobject<NSCursor> eastWestCursor_;
   base::scoped_nsobject<NSCursor> northSouthCursor_;
   base::scoped_nsobject<NSCursor> northEastSouthWestCursor_;
   base::scoped_nsobject<NSCursor> northWestSouthEastCursor_;
   NSRect leftCursorRect_;
   NSRect rightCursorRect_;
   NSRect topCursorRect_;
   NSRect bottomCursorRect_;
   NSRect topLeftCursorRect_;
   NSRect topRightCursorRect_;
   NSRect bottomLeftCursorRect_;
   NSRect bottomRightCursorRect_;
}
@end

namespace {
NSCursor* LoadWebKitCursor(WebKit::WebCursorInfo::Type type) {
    return WebCursor(WebCursor::CursorInfo(type)).GetNativeCursor();
}
}

@implementation PanelResizeByMouseOverlay
- (PanelResizeByMouseOverlay*)initWithFrame:(NSRect)frame panel:(Panel*)panel {
  if ((self = [super initWithFrame:frame])) {
    panel_ = panel;
    dragController_.reset([[MouseDragController alloc] initWithClient:self]);

    eastWestCursor_.reset(
        [LoadWebKitCursor(WebKit::WebCursorInfo::TypeEastWestResize) retain]);
    northSouthCursor_.reset(
        [LoadWebKitCursor(WebKit::WebCursorInfo::TypeNorthSouthResize) retain]);
    northEastSouthWestCursor_.reset(
        [LoadWebKitCursor(WebKit::WebCursorInfo::TypeNorthEastSouthWestResize)
        retain]);
    northWestSouthEastCursor_.reset(
        [LoadWebKitCursor(WebKit::WebCursorInfo::TypeNorthWestSouthEastResize)
        retain]);
  }
  return self;
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event {
  return YES;
}

// |pointInWindow| is in window coordinates.
- (panel::ResizingSides)edgeHitTest:(NSPoint)pointInWindow {
  panel::Resizability resizability = panel_->CanResizeByMouse();
  DCHECK_NE(panel::NOT_RESIZABLE, resizability);

  NSPoint point = [self convertPoint:pointInWindow fromView:nil];
  BOOL flipped = [self isFlipped];

  if ((resizability & panel::RESIZABLE_TOP_LEFT) &&
      NSMouseInRect(point, topLeftCursorRect_, flipped)) {
    return panel::RESIZE_TOP_LEFT;
  }
  if ((resizability & panel::RESIZABLE_TOP_RIGHT) &&
      NSMouseInRect(point, topRightCursorRect_, flipped)) {
    return panel::RESIZE_TOP_RIGHT;
  }
  if ((resizability & panel::RESIZABLE_BOTTOM_LEFT) &&
      NSMouseInRect(point, bottomLeftCursorRect_, flipped)) {
    return panel::RESIZE_BOTTOM_LEFT;
  }
  if ((resizability & panel::RESIZABLE_BOTTOM_RIGHT) &&
      NSMouseInRect(point, bottomRightCursorRect_, flipped)) {
    return panel::RESIZE_BOTTOM_RIGHT;
  }

  if ((resizability & panel::RESIZABLE_LEFT) &&
      NSMouseInRect(point, leftCursorRect_, flipped)) {
    return panel::RESIZE_LEFT;
  }
  if ((resizability & panel::RESIZABLE_RIGHT) &&
      NSMouseInRect(point, rightCursorRect_, flipped)) {
    return panel::RESIZE_RIGHT;
  }
  if ((resizability & panel::RESIZABLE_TOP) &&
      NSMouseInRect(point, topCursorRect_, flipped)) {
    return panel::RESIZE_TOP;
  }
  if ((resizability & panel::RESIZABLE_BOTTOM) &&
      NSMouseInRect(point, bottomCursorRect_, flipped)) {
    return panel::RESIZE_BOTTOM;
  }

  return panel::RESIZE_NONE;
}

// NSWindow uses this method to figure out if this view is under the mouse
// and hence the one to handle the incoming mouse event.
// Since this view covers the whole panel, it is asked first.
// See if this is the mouse event we are interested in (in the resize areas)
// and return 'nil' to let NSWindow find another candidate otherwise.
// |point| is in coordinate system of the parent view.
- (NSView*)hitTest:(NSPoint)point {
  // If panel is not resizable, let the mouse events fall through.
  if (panel::NOT_RESIZABLE == panel_->CanResizeByMouse())
    return nil;

  NSPoint pointInWindow = [[self superview] convertPoint:point toView:nil];
  return [self edgeHitTest:pointInWindow] == panel::RESIZE_NONE ? nil : self;
}

// Delegate these to MouseDragController, it will call back on
// MouseDragControllerClient protocol.
- (void)mouseDown:(NSEvent*)event {
  [dragController_ mouseDown:event];
}

- (void)mouseDragged:(NSEvent*)event {
  [dragController_ mouseDragged:event];
}

- (void)mouseUp:(NSEvent*)event {
  [dragController_ mouseUp:event];
}

// MouseDragControllerClient protocol.

- (void)prepareForDrag {
  // If the panel is not resizable, hitTest should have failed and no mouse
  // events should have come here.
  DCHECK_NE(panel::NOT_RESIZABLE, panel_->CanResizeByMouse());

  // Make sure the cursor stays the same during whole resize operation.
  // The cursor rects normally do not guarantee the same cursor, since the
  // mouse may temporarily leave the cursor rect area (or even the window) so
  // the cursor will flicker. Disable cursor rects and grab the current cursor
  // so we can set it on mouseDragged: events to avoid flicker.
  [[self window] disableCursorRects];
  dragCursor_.reset([[NSCursor currentCursor] retain]);
}

- (void)cleanupAfterDrag {
  [[self window] enableCursorRects];
  dragCursor_.reset();
}

- (void)dragStarted:(NSPoint)initialMouseLocation {
  NSPoint initialMouseLocationScreen =
      [[self window] convertBaseToScreen:initialMouseLocation];

  panel_->manager()->StartResizingByMouse(
      panel_,
      cocoa_utils::ConvertPointFromCocoaCoordinates(initialMouseLocationScreen),
      [self edgeHitTest:initialMouseLocation]);
}

- (void)dragProgress:(NSPoint)mouseLocation {
  NSPoint mouseLocationScreen =
      [[self window] convertBaseToScreen:mouseLocation];
  panel_->manager()->ResizeByMouse(
      cocoa_utils::ConvertPointFromCocoaCoordinates(mouseLocationScreen));

  // Set the resize cursor on every mouse drag event in case the mouse
  // wandered outside the window and was switched to another one.
  // This does not produce flicker, seems the real cursor is updated after
  // mouseDrag is processed.
  [dragCursor_ set];
}

- (void)dragEnded:(BOOL)cancelled {
  panel_->manager()->EndResizingByMouse(cancelled);
}

- (void)resetCursorRects {
  panel::Resizability resizability = panel_->CanResizeByMouse();
  if (panel::NOT_RESIZABLE == resizability)
    return;

  NSRect bounds = [self bounds];

  // Left vertical edge.
  if (resizability & panel::RESIZABLE_LEFT) {
    leftCursorRect_ = NSMakeRect(
        NSMinX(bounds),
        NSMinY(bounds) + kWidthOfMouseResizeArea,
        kWidthOfMouseResizeArea,
        NSHeight(bounds) - 2 * kWidthOfMouseResizeArea);
    [self addCursorRect:leftCursorRect_ cursor:eastWestCursor_];
  }

  // Right vertical edge.
  if (resizability & panel::RESIZABLE_RIGHT) {
    rightCursorRect_ = leftCursorRect_;
    rightCursorRect_.origin.x = NSMaxX(bounds) - kWidthOfMouseResizeArea;
    [self addCursorRect:rightCursorRect_ cursor:eastWestCursor_];
  }

  // Top horizontal edge.
  if (resizability & panel::RESIZABLE_TOP) {
    topCursorRect_ = NSMakeRect(NSMinX(bounds) + kWidthOfMouseResizeArea,
                                NSMaxY(bounds) - kWidthOfMouseResizeArea,
                                NSWidth(bounds) - 2 * kWidthOfMouseResizeArea,
                                kWidthOfMouseResizeArea);
    [self addCursorRect:topCursorRect_ cursor:northSouthCursor_];
  }

  // Top left corner.
  if (resizability & panel::RESIZABLE_TOP_LEFT) {
    topLeftCursorRect_ = NSMakeRect(NSMinX(bounds),
                                    NSMaxY(bounds) - kWidthOfMouseResizeArea,
                                    kWidthOfMouseResizeArea,
                                    NSMaxY(bounds));
    [self addCursorRect:topLeftCursorRect_ cursor:northWestSouthEastCursor_];
  }

  // Top right corner.
  if (resizability & panel::RESIZABLE_TOP_RIGHT) {
    topRightCursorRect_ = topLeftCursorRect_;
    topRightCursorRect_.origin.x = NSMaxX(bounds) - kWidthOfMouseResizeArea;
    [self addCursorRect:topRightCursorRect_ cursor:northEastSouthWestCursor_];
  }

  // Bottom horizontal edge.
  if (resizability & panel::RESIZABLE_BOTTOM) {
    bottomCursorRect_ = topCursorRect_;
    bottomCursorRect_.origin.y = NSMinY(bounds);
    [self addCursorRect:bottomCursorRect_ cursor:northSouthCursor_];
  }

  // Bottom right corner.
  if (resizability & panel::RESIZABLE_BOTTOM_RIGHT) {
    bottomRightCursorRect_ = topRightCursorRect_;
    bottomRightCursorRect_.origin.y = NSMinY(bounds);
    [self addCursorRect:bottomRightCursorRect_
                 cursor:northWestSouthEastCursor_];
  }

  // Bottom left corner.
  if (resizability & panel::RESIZABLE_BOTTOM_LEFT) {
    bottomLeftCursorRect_ = bottomRightCursorRect_;
    bottomLeftCursorRect_.origin.x = NSMinX(bounds);
    [self addCursorRect:bottomLeftCursorRect_ cursor:northEastSouthWestCursor_];
  }
}
@end

// ChromeEventProcessingWindow expects its controller to implement the
// BrowserCommandExecutor protocol.
@interface PanelWindowControllerCocoa (InternalAPI) <BrowserCommandExecutor>

// BrowserCommandExecutor methods.
- (void)executeCommand:(int)command;

@end

@implementation PanelWindowControllerCocoa (InternalAPI)

// This gets called whenever a browser-specific keyboard shortcut is performed
// in the Panel window. We simply swallow all those events.
- (void)executeCommand:(int)command {}

@end

@implementation PanelWindowControllerCocoa

- (id)initWithPanel:(PanelCocoa*)window {
  NSString* nibpath =
      [base::mac::FrameworkBundle() pathForResource:@"Panel" ofType:@"nib"];
  if ((self = [super initWithWindowNibPath:nibpath owner:self])) {
    windowShim_.reset(window);
    animateOnBoundsChange_ = YES;
    canBecomeKeyWindow_ = YES;
    activationRequestedByPanel_ = NO;
  }
  return self;
}

- (void)awakeFromNib {
  NSWindow* window = [self window];

  DCHECK(window);
  DCHECK(titlebar_view_);
  DCHECK_EQ(self, [window delegate]);

  [self updateWindowLevel];

  [self updateWindowCollectionBehavior];

  [titlebar_view_ attach];

  // Set initial size of the window to match the size of the panel to give
  // the renderer the proper size to work with earlier, avoiding a resize
  // after the window is revealed.
  gfx::Rect panelBounds = windowShim_->panel()->GetBounds();
  NSRect frame = [window frame];
  frame.size.width = panelBounds.width();
  frame.size.height = panelBounds.height();
  [window setFrame:frame display:NO];

  // Add a transparent overlay on top of the whole window to process mouse
  // events - for example, user-resizing.
  NSView* superview = [[window contentView] superview];
  NSRect bounds = [superview bounds];
  overlayView_.reset(
      [[PanelResizeByMouseOverlay alloc] initWithFrame:bounds
                                                 panel:windowShim_->panel()]);
  // Set autoresizing behavior: glued to edges.
  [overlayView_ setAutoresizingMask:(NSViewHeightSizable | NSViewWidthSizable)];
  [superview addSubview:overlayView_ positioned:NSWindowAbove relativeTo:nil];
}

- (void)updateWebContentsViewFrame {
  content::WebContents* webContents = windowShim_->panel()->GetWebContents();
  if (!webContents)
    return;

  // Compute the size of the web contents view. Don't assume it's similar to the
  // size of the contentView, because the contentView is managed by the Cocoa
  // to be (window - standard titlebar), while we have taller custom titlebar
  // instead. In coordinate system of window's contentView.
  NSRect contentFrame = [self contentRectForFrameRect:[[self window] frame]];
  contentFrame.origin = NSZeroPoint;

  NSView* contentView = webContents->GetView()->GetNativeView();
  if (!NSEqualRects([contentView frame], contentFrame))
    [contentView setFrame:contentFrame];
}

- (void)disableWebContentsViewAutosizing {
  [[[self window] contentView] setAutoresizesSubviews:NO];
}

- (void)enableWebContentsViewAutosizing {
  [self updateWebContentsViewFrame];
  [[[self window] contentView] setAutoresizesSubviews:YES];
}

- (void)revealAnimatedWithFrame:(const NSRect&)frame {
  NSWindow* window = [self window];  // This ensures loading the nib.

  // Disable subview resizing while resizing the window to avoid renderer
  // resizes during intermediate stages of animation.
  [self disableWebContentsViewAutosizing];

  // We grow the window from the bottom up to produce a 'reveal' animation.
  NSRect startFrame = NSMakeRect(NSMinX(frame), NSMinY(frame),
                                 NSWidth(frame), kMinimumWindowSize);
  [window setFrame:startFrame display:NO animate:NO];
  // Shows the window without making it key, on top of its layer, even if
  // Chromium is not an active app.
  [window orderFrontRegardless];
  // TODO(dcheng): Temporary hack to work around the fact that
  // orderFrontRegardless causes us to become the first responder. The usual
  // Chrome assumption is that becoming the first responder = you have focus, so
  // we always deactivate the controls here. If we're created as an active
  // panel, we'll get a NSWindowDidBecomeKeyNotification and reactivate the web
  // view properly. See crbug.com/97831 for more details.
  WebContents* web_contents = windowShim_->panel()->GetWebContents();
  // RWHV may be NULL in unit tests.
  if (web_contents && web_contents->GetRenderWidgetHostView())
    web_contents->GetRenderWidgetHostView()->SetActive(false);

  // This will re-enable the content resizing after it finishes.
  [self setPanelFrame:frame animate:YES];
}

- (void)updateTitleBar {
  NSString* newTitle = base::SysUTF16ToNSString(
      windowShim_->panel()->GetWindowTitle());
  pendingWindowTitle_.reset(
      [BrowserWindowUtils scheduleReplaceOldTitle:pendingWindowTitle_.get()
                                     withNewTitle:newTitle
                                        forWindow:[self window]]);
  [titlebar_view_ setTitle:newTitle];
  [self updateIcon];
}

- (void)updateIcon {
  NSView* icon = nil;
  NSRect iconFrame = [[titlebar_view_ icon] frame];
  if (throbberShouldSpin_) {
    // If the throbber is spinning now, no need to replace it.
    if ([[titlebar_view_ icon] isKindOfClass:[ThrobberView class]])
      return;

    NSImage* iconImage =
        ResourceBundle::GetSharedInstance().GetNativeImageNamed(
            IDR_THROBBER).ToNSImage();
    icon = [ThrobberView filmstripThrobberViewWithFrame:iconFrame
                                                  image:iconImage];
  } else {
    const gfx::Image& page_icon = windowShim_->panel()->GetCurrentPageIcon();
    ResourceBundle& rb = ResourceBundle::GetSharedInstance();
    NSImage* iconImage = page_icon.IsEmpty() ?
        rb.GetNativeImageNamed(IDR_DEFAULT_FAVICON).ToNSImage() :
        page_icon.ToNSImage();
    NSImageView* iconView =
        [[[NSImageView alloc] initWithFrame:iconFrame] autorelease];
    [iconView setImage:iconImage];
    icon = iconView;
  }

  [titlebar_view_ setIcon:icon];
}

- (void)updateThrobber:(BOOL)shouldSpin {
  if (throbberShouldSpin_ == shouldSpin)
    return;
  throbberShouldSpin_ = shouldSpin;

  // If the titlebar view has not been attached, bail out.
  if (!titlebar_view_)
    return;

  [self updateIcon];
}

- (void)updateTitleBarMinimizeRestoreButtonVisibility {
  Panel* panel = windowShim_->panel();
  [titlebar_view_ setMinimizeButtonVisibility:panel->CanShowMinimizeButton()];
  [titlebar_view_ setRestoreButtonVisibility:panel->CanShowRestoreButton()];
}

- (void)webContentsInserted:(WebContents*)contents {
  NSView* view = contents->GetView()->GetNativeView();
  [[[self window] contentView] addSubview:view];
  [view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];

  [self enableWebContentsViewAutosizing];
}

- (void)webContentsDetached:(WebContents*)contents {
  [contents->GetView()->GetNativeView() removeFromSuperview];
}

- (PanelTitlebarViewCocoa*)titlebarView {
  return titlebar_view_;
}

// Called to validate menu and toolbar items when this window is key. All the
// items we care about have been set with the |-commandDispatch:|
// action and a target of FirstResponder in IB.
// Delegate to the NSApp delegate if Panel does not care about the command or
// shortcut, to make sure the global items in Chrome main app menu still work.
- (BOOL)validateUserInterfaceItem:(id<NSValidatedUserInterfaceItem>)item {
  if ([item action] == @selector(commandDispatch:)) {
    NSInteger tag = [item tag];
    CommandUpdater* command_updater = windowShim_->panel()->command_updater();
    if (command_updater->SupportsCommand(tag))
      return command_updater->IsCommandEnabled(tag);
    else
      return [[NSApp delegate] validateUserInterfaceItem:item];
  }
  return NO;
}

// Called when the user picks a menu or toolbar item when this window is key.
// Calls through to the panel object to execute the command or delegates up.
- (void)commandDispatch:(id)sender {
  DCHECK(sender);
  NSInteger tag = [sender tag];
  CommandUpdater* command_updater = windowShim_->panel()->command_updater();
  if (command_updater->SupportsCommand(tag))
    windowShim_->panel()->ExecuteCommandIfEnabled(tag);
  else
    [[NSApp delegate] commandDispatch:sender];
}

// Handler for the custom Close button.
- (void)closePanel {
  windowShim_->panel()->Close();
}

// Handler for the custom Minimize button.
- (void)minimizeButtonClicked:(int)modifierFlags {
  Panel* panel = windowShim_->panel();
  panel->OnMinimizeButtonClicked((modifierFlags & NSShiftKeyMask) ?
                                 panel::APPLY_TO_ALL : panel::NO_MODIFIER);
}

// Handler for the custom Restore button.
- (void)restoreButtonClicked:(int)modifierFlags {
  Panel* panel = windowShim_->panel();
  panel->OnRestoreButtonClicked((modifierFlags & NSShiftKeyMask) ?
                                panel::APPLY_TO_ALL : panel::NO_MODIFIER);
}

// Called when the user wants to close the panel or from the shutdown process.
// The Panel object is in control of whether or not we're allowed to close. It
// may defer closing due to several states, such as onbeforeUnload handlers
// needing to be fired. If closing is deferred, the Panel will handle the
// processing required to get us to the closing state and (by watching for
// the web content going away) will again call to close the window when it's
// finally ready.
- (BOOL)windowShouldClose:(id)sender {
  Panel* panel = windowShim_->panel();
  // Give beforeunload handlers the chance to cancel the close before we hide
  // the window below.
  if (!panel->ShouldCloseWindow())
    return NO;

  if (panel->GetWebContents()) {
    // Terminate any playing animations.
    [self terminateBoundsAnimation];
    animateOnBoundsChange_ = NO;
    // Make panel close the web content, allowing the renderer to shut down
    // and call us back again.
    panel->OnWindowClosing();
    return NO;
  }

  // No web content; it's ok to close the window.
  return YES;
}

// When windowShouldClose returns YES (or if controller receives direct 'close'
// signal), window will be unconditionally closed. Clean up.
- (void)windowWillClose:(NSNotification*)notification {
  DCHECK(!windowShim_->panel()->GetWebContents());
  // Avoid callbacks from a nonblocking animation in progress, if any.
  [self terminateBoundsAnimation];
  windowShim_->DidCloseNativeWindow();
  // Call |-autorelease| after a zero-length delay to avoid deadlock from
  // code in the current run loop that waits on PANEL_CLOSED notification.
  // The notification is sent when this object is freed, but this object
  // cannot be freed until the current run loop completes.
  [self performSelector:@selector(autorelease)
             withObject:nil
             afterDelay:0];
}

- (void)startDrag:(NSPoint)mouseLocation {
  // Convert from Cocoa's screen coordinates to platform-indepedent screen
  // coordinates because PanelManager method takes platform-indepedent screen
  // coordinates.
  windowShim_->panel()->manager()->StartDragging(
      windowShim_->panel(),
      cocoa_utils::ConvertPointFromCocoaCoordinates(mouseLocation));
}

- (void)endDrag:(BOOL)cancelled {
  windowShim_->panel()->manager()->EndDragging(cancelled);
}

- (void)drag:(NSPoint)mouseLocation {
  // Convert from Cocoa's screen coordinates to platform-indepedent screen
  // coordinates because PanelManager method takes platform-indepedent screen
  // coordinates.
  windowShim_->panel()->manager()->Drag(
      cocoa_utils::ConvertPointFromCocoaCoordinates(mouseLocation));
}

- (void)setPanelFrame:(NSRect)frame
              animate:(BOOL)animate {
  BOOL jumpToDestination = (!animateOnBoundsChange_ || !animate);

  // If no animation is in progress, apply bounds change instantly.
  if (jumpToDestination && ![self isAnimatingBounds]) {
    [[self window] setFrame:frame display:YES animate:NO];
    return;
  }

  NSDictionary *windowResize = [NSDictionary dictionaryWithObjectsAndKeys:
      [self window], NSViewAnimationTargetKey,
      [NSValue valueWithRect:frame], NSViewAnimationEndFrameKey, nil];
  NSArray *animations = [NSArray arrayWithObjects:windowResize, nil];

  // If an animation is in progress, update the animation with new target
  // bounds. Also, set the destination frame bounds to the new value.
  if (jumpToDestination && [self isAnimatingBounds]) {
    [boundsAnimation_ setViewAnimations:animations];
    [[self window] setFrame:frame display:YES animate:NO];
    return;
  }

  // Will be enabled back in animationDidEnd callback.
  [self disableWebContentsViewAutosizing];

  // Terminate previous animation, if it is still playing.
  [self terminateBoundsAnimation];

  boundsAnimation_ =
      [[NSViewAnimation alloc] initWithViewAnimations:animations];
  [boundsAnimation_ setDelegate:self];

  NSRect currentFrame = [[self window] frame];
  // Compute duration. We use constant speed of animation, however if the change
  // is too large, we clip the duration (effectively increasing speed) to
  // limit total duration of animation. This makes 'small' transitions fast.
  // 'distance' is the max travel between 4 potentially traveling corners.
  double distanceX = std::max(abs(NSMinX(currentFrame) - NSMinX(frame)),
                              abs(NSMaxX(currentFrame) - NSMaxX(frame)));
  double distanceY = std::max(abs(NSMinY(currentFrame) - NSMinY(frame)),
                              abs(NSMaxY(currentFrame) - NSMaxY(frame)));
  double distance = std::max(distanceX, distanceY);
  double duration = std::min(distance / kBoundsAnimationSpeedPixelsPerSecond,
                             kBoundsAnimationMaxDurationSeconds);
  // Detect animation that happens when expansion state is set to MINIMIZED
  // and there is relatively big portion of the panel to hide from view.
  // Initialize animation differently in this case, using fast-pause-slow
  // method, see below for more details.
  if (distanceY > 0 &&
      windowShim_->panel()->expansion_state() == Panel::MINIMIZED) {
    animationStopToShowTitlebarOnly_ = 1.0 -
        (windowShim_->panel()->TitleOnlyHeight() - NSHeight(frame)) / distanceY;
    if (animationStopToShowTitlebarOnly_ > 0.7) {  // Relatively big movement.
      playingMinimizeAnimation_ = YES;
      duration = 1.5;
    }
  }
  [boundsAnimation_ setDuration: PanelManager::AdjustTimeInterval(duration)];
  [boundsAnimation_ setFrameRate:0.0];
  [boundsAnimation_ setAnimationBlockingMode: NSAnimationNonblocking];
  [boundsAnimation_ startAnimation];
}

- (float)animation:(NSAnimation*)animation
  valueForProgress:(NSAnimationProgress)progress {
  return PanelBoundsAnimation::ComputeAnimationValue(
      progress, playingMinimizeAnimation_, animationStopToShowTitlebarOnly_);
}

- (void)cleanupAfterAnimation {
  playingMinimizeAnimation_ = NO;
  if (!windowShim_->panel()->IsMinimized())
    [self enableWebContentsViewAutosizing];
}

- (void)animationDidEnd:(NSAnimation*)animation {
  [self cleanupAfterAnimation];

  // Only invoke this callback from animationDidEnd, since animationDidStop can
  // be called when we interrupt/restart animation which is in progress.
  // We only need this notification when animation indeed finished moving
  // the panel bounds.
  Panel* panel = windowShim_->panel();
  panel->manager()->OnPanelAnimationEnded(panel);
}

- (void)animationDidStop:(NSAnimation*)animation {
  [self cleanupAfterAnimation];
}

- (void)terminateBoundsAnimation {
  if (!boundsAnimation_)
    return;
  [boundsAnimation_ stopAnimation];
  [boundsAnimation_ setDelegate:nil];
  [boundsAnimation_ release];
  boundsAnimation_ = nil;
}

- (BOOL)isAnimatingBounds {
  return boundsAnimation_ && [boundsAnimation_ isAnimating];
}

- (void)onTitlebarMouseClicked:(int)modifierFlags {
  Panel* panel = windowShim_->panel();
  panel->OnTitlebarClicked((modifierFlags & NSShiftKeyMask) ?
                           panel::APPLY_TO_ALL : panel::NO_MODIFIER);
}

- (int)titlebarHeightInScreenCoordinates {
  NSView* titlebar = [self titlebarView];
  return NSHeight([titlebar convertRect:[titlebar bounds] toView:nil]);
}

// TODO(dcheng): These two selectors are almost copy-and-paste from
// BrowserWindowController. Figure out the appropriate way of code sharing,
// whether it's refactoring more things into BrowserWindowUtils or making a
// common base controller for browser windows.
- (void)windowDidBecomeKey:(NSNotification*)notification {
  // We need to activate the controls (in the "WebView"). To do this, get the
  // selected WebContents's RenderWidgetHostView and tell it to activate.
  if (WebContents* contents = windowShim_->panel()->GetWebContents()) {
    if (content::RenderWidgetHostView* rwhv =
        contents->GetRenderWidgetHostView())
      rwhv->SetActive(true);
  }

  windowShim_->panel()->OnActiveStateChanged(true);
}

- (void)windowDidResignKey:(NSNotification*)notification {
  // If our app is still active and we're still the key window, ignore this
  // message, since it just means that a menu extra (on the "system status bar")
  // was activated; we'll get another |-windowDidResignKey| if we ever really
  // lose key window status.
  if ([NSApp isActive] && ([NSApp keyWindow] == [self window]))
    return;

  [self onWindowDidResignKey];
}

- (void)windowDidResize:(NSNotification*)notification {
  // Remove the web contents view from the view hierarchy when the panel is not
  // taller than the titlebar. Put it back when the panel grows taller than
  // the titlebar. Note that RenderWidgetHostViewMac works for the case that
  // the web contents view does not exist in the view hierarchy (i.e. the tab
  // is not the main one), but it does not work well, like causing occasional
  // crashes (http://crbug.com/265932), if the web contents view is made hidden.
  //
  // The reason for doing this is to ensure that our titlebar view, that is
  // somewhat taller than the standard titlebar, does not overlap with the web
  // contents view because the the web contents view assumes that its view will
  // never overlap with another view in order to paint the web contents view
  // directly. If we do not do this, some part of the web contents view will
  // become visible and overlap the bottom area of the titlebar.
  content::WebContents* webContents = windowShim_->panel()->GetWebContents();
  if (!webContents)
    return;
  NSView* contentView = webContents->GetView()->GetNativeView();
  if (NSHeight([self contentRectForFrameRect:[[self window] frame]]) <=
      panel::kTitlebarHeight) {
    // No need to retain the view before it is removed from its superview
    // because WebContentsView keeps a reference to this view.
    if ([contentView superview])
      [contentView removeFromSuperview];
  } else {
    if (![contentView superview])
      [[[self window] contentView] addSubview:contentView];
  }
}

- (void)activate {
  // Activate the window. -|windowDidBecomeKey:| will be called when
  // window becomes active.
  base::AutoReset<BOOL> pin(&activationRequestedByPanel_, true);
  [BrowserWindowUtils activateWindowForController:self];
}

- (void)deactivate {
  if (![[self window] isMainWindow])
    return;

  // Cocoa does not support deactivating a window, so we deactivate the app.
  [NSApp deactivate];

  // Deactivating the app does not trigger windowDidResignKey. Do it manually.
  [self onWindowDidResignKey];
}

- (void)onWindowDidResignKey {
  // We need to deactivate the controls (in the "WebView"). To do this, get the
  // selected WebContents's RenderWidgetHostView and tell it to deactivate.
  if (WebContents* contents = windowShim_->panel()->GetWebContents()) {
    if (content::RenderWidgetHostView* rwhv =
        contents->GetRenderWidgetHostView())
      rwhv->SetActive(false);
  }

  windowShim_->panel()->OnActiveStateChanged(false);
}

- (void)preventBecomingKeyWindow:(BOOL)prevent {
  canBecomeKeyWindow_ = !prevent;
}

- (void)fullScreenModeChanged:(bool)isFullScreen {
  [self updateWindowLevel];

  // If the panel is not always on top, its z-order should not be affected if
  // some other window enters fullscreen mode.
  if (!windowShim_->panel()->IsAlwaysOnTop())
    return;

  // The full-screen window is in normal level and changing the panel window
  // to same normal level will not move it below the full-screen window. Thus
  // we need to reorder the panel window.
  if (isFullScreen)
    [[self window] orderOut:nil];
  else
    [[self window] orderFrontRegardless];
}

- (BOOL)canBecomeKeyWindow {
  // Panel can only gain focus if it is expanded. Minimized panels do not
  // participate in Cmd-~ rotation.
  // TODO(dimich): If it will be ever desired to expand/focus the Panel on
  // keyboard navigation or via main menu, the care should be taken to avoid
  // cases when minimized Panel is getting keyboard input, invisibly.
  return canBecomeKeyWindow_;
}

- (int)numPanels {
  return windowShim_->panel()->manager()->num_panels();
}

- (BOOL)activationRequestedByPanel {
  return activationRequestedByPanel_;
}

- (void)updateWindowLevel {
  [self updateWindowLevel:windowShim_->panel()->IsMinimized()];
}

- (void)updateWindowLevel:(BOOL)panelIsMinimized {
  if (![self isWindowLoaded])
    return;
  Panel* panel = windowShim_->panel();
  if (!panel->IsAlwaysOnTop()) {
    [[self window] setLevel:NSNormalWindowLevel];
    return;
  }
  // If we simply use NSStatusWindowLevel (25) for all docked panel windows,
  // IME composition windows for things like CJK languages appear behind panels.
  // Pre 10.7, IME composition windows have a window level of 19, which is
  // lower than the dock at level 20. Since we want panels to appear on top of
  // the dock, it is impossible to enforce an ordering where IME > panel > dock,
  // since IME < dock.
  // On 10.7, IME composition windows and the dock both live at level 20, so we
  // use the same window level for panels. Since newly created windows appear at
  // the top of their window level, panels are typically on top of the dock, and
  // the IME composition window correctly draws over the panel.
  // An autohide dock causes problems though: since it's constantly being
  // revealed, it ends up drawing on top of other windows at the same level.
  // While this is OK for expanded panels, it makes minimized panels impossible
  // to activate. As a result, we still use NSStatusWindowLevel for minimized
  // panels, since it's impossible to compose IME text in them anyway.
  if (panelIsMinimized) {
    [[self window] setLevel:NSStatusWindowLevel];
    return;
  }
  [[self window] setLevel:NSDockWindowLevel];
}

- (void)updateWindowCollectionBehavior {
  if (![self isWindowLoaded])
    return;
  NSWindowCollectionBehavior collectionBehavior =
      NSWindowCollectionBehaviorParticipatesInCycle;
  if (windowShim_->panel()->IsAlwaysOnTop())
    collectionBehavior |= NSWindowCollectionBehaviorCanJoinAllSpaces;
  [[self window] setCollectionBehavior:collectionBehavior];
}

- (void)enableResizeByMouse:(BOOL)enable {
  if (![self isWindowLoaded])
    return;
  [[self window] invalidateCursorRectsForView:overlayView_];
}

- (void)showShadow:(BOOL)show {
  if (![self isWindowLoaded])
    return;
  [[self window] setHasShadow:show];
}

- (void)miniaturize {
  [[self window] miniaturize:nil];
}

- (BOOL)isMiniaturized {
  return [[self window] isMiniaturized];
}

// We have custom implementation of these because our titlebar height is custom
// and does not match the standard one.
- (NSRect)frameRectForContentRect:(NSRect)contentRect {
  // contentRect is in contentView coord system. We should add a titlebar on top
  // and then convert to the windows coord system.
  contentRect.size.height += panel::kTitlebarHeight;
  NSRect frameRect = [[[self window] contentView] convertRect:contentRect
                                                       toView:nil];
  return frameRect;
}

- (NSRect)contentRectForFrameRect:(NSRect)frameRect {
  NSRect contentRect = [[[self window] contentView] convertRect:frameRect
                                                       fromView:nil];
  contentRect.size.height -= panel::kTitlebarHeight;
  if (contentRect.size.height < 0)
    contentRect.size.height = 0;
  return contentRect;
}

@end
