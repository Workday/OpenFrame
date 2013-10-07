// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/renderer_host/chrome_render_widget_host_view_mac_delegate.h"

#include <cmath>

#import "base/mac/sdk_forward_declarations.h"
#include "base/prefs/pref_service.h"
#include "base/strings/sys_string_conversions.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/spellchecker/spellcheck_platform_mac.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_finder.h"
#import "chrome/browser/ui/cocoa/browser_window_controller.h"
#import "chrome/browser/ui/cocoa/history_overlay_controller.h"
#import "chrome/browser/ui/cocoa/view_id_util.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/spellcheck_messages.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_view_host_observer.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"

using content::RenderViewHost;

@interface ChromeRenderWidgetHostViewMacDelegate ()
- (BOOL)maybeHandleHistorySwiping:(NSEvent*)theEvent;
- (void)spellCheckEnabled:(BOOL)enabled checked:(BOOL)checked;
@end

namespace ChromeRenderWidgetHostViewMacDelegateInternal {

// Filters the message sent to RenderViewHost to know if spellchecking is
// enabled or not for the currently focused element.
class SpellCheckRenderViewObserver : public content::RenderViewHostObserver {
 public:
  SpellCheckRenderViewObserver(
      RenderViewHost* host,
      ChromeRenderWidgetHostViewMacDelegate* view_delegate)
      : content::RenderViewHostObserver(host),
        view_delegate_(view_delegate) {
  }

  virtual ~SpellCheckRenderViewObserver() {
  }

 private:
  // content::RenderViewHostObserver implementation.
  virtual void RenderViewHostDestroyed(RenderViewHost* rvh) OVERRIDE {
    // The parent implementation destroys the observer, scoping the lifetime of
    // the observer to the RenderViewHost. Since this class is acting as a
    // bridge to the view for the delegate below, and is owned by that delegate,
    // undo the scoping by not calling through to the parent implementation.
  }

  virtual bool OnMessageReceived(const IPC::Message& message) OVERRIDE {
    bool handled = true;
    IPC_BEGIN_MESSAGE_MAP(SpellCheckRenderViewObserver, message)
      IPC_MESSAGE_HANDLER(SpellCheckHostMsg_ToggleSpellCheck,
                          OnToggleSpellCheck)
      IPC_MESSAGE_UNHANDLED(handled = false)
    IPC_END_MESSAGE_MAP()
    return handled;
  }

  void OnToggleSpellCheck(bool enabled, bool checked) {
    [view_delegate_ spellCheckEnabled:enabled checked:checked];
  }

  ChromeRenderWidgetHostViewMacDelegate* view_delegate_;
};

}  // namespace ChromeRenderWidgetHostViewMacDelegateInternal

@implementation ChromeRenderWidgetHostViewMacDelegate

- (id)initWithRenderWidgetHost:(content::RenderWidgetHost*)renderWidgetHost {
  self = [super init];
  if (self) {
    renderWidgetHost_ = renderWidgetHost;
    NSView* nativeView = renderWidgetHost_->GetView()->GetNativeView();
    view_id_util::SetID(nativeView, VIEW_ID_TAB_CONTAINER);

    if (renderWidgetHost_->IsRenderView()) {
      spellingObserver_.reset(
          new ChromeRenderWidgetHostViewMacDelegateInternal::
              SpellCheckRenderViewObserver(
                  RenderViewHost::From(renderWidgetHost_), self));
    }
  }
  return self;
}

- (void)viewGone:(NSView*)view {
  view_id_util::UnsetID(view);
  [self autorelease];
}

- (BOOL)handleEvent:(NSEvent*)event {
  if ([event type] == NSScrollWheel)
    return [self maybeHandleHistorySwiping:event];

  return NO;
}

- (void)gotUnhandledWheelEvent {
  gotUnhandledWheelEvent_ = YES;
}

- (void)scrollOffsetPinnedToLeft:(BOOL)left toRight:(BOOL)right {
  isPinnedLeft_ = left;
  isPinnedRight_ = right;
}

- (void)setHasHorizontalScrollbar:(BOOL)hasHorizontalScrollbar {
  hasHorizontalScrollbar_ = hasHorizontalScrollbar;
}

// Checks if |theEvent| should trigger history swiping, and if so, does
// history swiping. Returns YES if the event was consumed or NO if it should
// be passed on to the renderer.
- (BOOL)maybeHandleHistorySwiping:(NSEvent*)theEvent {
  BOOL canUseLionApis = [theEvent respondsToSelector:@selector(phase)];
  if (!canUseLionApis)
    return NO;

  // Scroll events always go to the web first, and can only trigger history
  // swiping if they come back unhandled.
  if ([theEvent phase] == NSEventPhaseBegan) {
    totalScrollDelta_ = NSZeroSize;
    gotUnhandledWheelEvent_ = NO;
  }

  if (!renderWidgetHost_ || !renderWidgetHost_->IsRenderView())
    return NO;
  if (DevToolsWindow::IsDevToolsWindow(
          RenderViewHost::From(renderWidgetHost_))) {
    return NO;
  }

  if (gotUnhandledWheelEvent_ &&
      [NSEvent isSwipeTrackingFromScrollEventsEnabled] &&
      [theEvent phase] == NSEventPhaseChanged) {
    Browser* browser = chrome::FindBrowserWithWindow([theEvent window]);
    totalScrollDelta_.width += [theEvent scrollingDeltaX];
    totalScrollDelta_.height += [theEvent scrollingDeltaY];

    bool isHorizontalGesture =
      std::abs(totalScrollDelta_.width) > std::abs(totalScrollDelta_.height);

    bool isRightScroll = [theEvent scrollingDeltaX] < 0;
    bool goForward = isRightScroll;
    bool canGoBack = false, canGoForward = false;
    if (browser) {
      canGoBack = chrome::CanGoBack(browser);
      canGoForward = chrome::CanGoForward(browser);
    }

    // If "forward" is inactive and the user rubber-bands to the right,
    // "isPinnedLeft" will be false.  When the user then rubber-bands to the
    // left in the same gesture, that should trigger history immediately if
    // there's no scrollbar, hence the check for hasHorizontalScrollbar_.
    bool shouldGoBack = isPinnedLeft_ || !hasHorizontalScrollbar_;
    bool shouldGoForward = isPinnedRight_ || !hasHorizontalScrollbar_;
    if (isHorizontalGesture &&
        // For normal pages, canGoBack/canGoForward are checked in the renderer
        // (ChromeClientImpl::shouldRubberBand()), when it decides if it should
        // rubberband or send back an event unhandled. The check here is
        // required for pages with an onmousewheel handler that doesn't call
        // preventDefault().
        ((shouldGoBack && canGoBack && !isRightScroll) ||
         (shouldGoForward && canGoForward && isRightScroll))) {

      // Released by the tracking handler once the gesture is complete.
      HistoryOverlayController* historyOverlay =
          [[HistoryOverlayController alloc]
              initForMode:goForward ? kHistoryOverlayModeForward :
                                      kHistoryOverlayModeBack];

      // The way this API works: gestureAmount is between -1 and 1 (float).  If
      // the user does the gesture for more than about 25% (i.e. < -0.25 or >
      // 0.25) and then lets go, it is accepted, we get a NSEventPhaseEnded,
      // and after that the block is called with amounts animating towards 1
      // (or -1, depending on the direction).  If the user lets go below that
      // threshold, we get NSEventPhaseCancelled, and the amount animates
      // toward 0.  When gestureAmount has reaches its final value, i.e. the
      // track animation is done, the handler is called with |isComplete| set
      // to |YES|.
      // When starting a backwards navigation gesture (swipe from left to right,
      // gestureAmount will go from 0 to 1), if the user swipes from left to
      // right and then quickly back to the left, this call can send
      // NSEventPhaseEnded and then animate to gestureAmount of -1. For a
      // picture viewer, that makes sense, but for back/forward navigation users
      // find it confusing. There are two ways to prevent this:
      // 1. Set Options to NSEventSwipeTrackingLockDirection. This way,
      //    gestureAmount will always stay > 0.
      // 2. Pass min:0 max:1 (instead of min:-1 max:1). This way, gestureAmount
      //    will become less than 0, but on the quick swipe back to the left,
      //    NSEventPhaseCancelled is sent instead.
      // The current UI looks nicer with (1) so that swiping the opposite
      // direction after the initial swipe doesn't cause the shield to move
      // in the wrong direction.
      [theEvent trackSwipeEventWithOptions:NSEventSwipeTrackingLockDirection
                  dampenAmountThresholdMin:-1
                                       max:1
                              usingHandler:^(CGFloat gestureAmount,
                                             NSEventPhase phase,
                                             BOOL isComplete,
                                             BOOL *stop) {
          if (phase == NSEventPhaseBegan) {
            [historyOverlay showPanelForView:
                renderWidgetHost_->GetView()->GetNativeView()];
            return;
          }

          BOOL ended = phase == NSEventPhaseEnded;

          // Dismiss the panel before navigation for immediate visual feedback.
          [historyOverlay setProgress:gestureAmount];
          if (ended)
            [historyOverlay dismiss];

          // |gestureAmount| obeys -[NSEvent isDirectionInvertedFromDevice]
          // automatically.
          Browser* browser = chrome::FindBrowserWithWindow(
              historyOverlay.view.window);
          if (ended && browser) {
            if (goForward)
              chrome::GoForward(browser, CURRENT_TAB);
            else
              chrome::GoBack(browser, CURRENT_TAB);
          }

          if (isComplete)
            [historyOverlay release];
        }];
      return YES;
    }
  }
  return NO;
}

- (BOOL)validateUserInterfaceItem:(id<NSValidatedUserInterfaceItem>)item
                      isValidItem:(BOOL*)valid {
  SEL action = [item action];

  // For now, this action is always enabled for render view;
  // this is sub-optimal.
  // TODO(suzhe): Plumb the "can*" methods up from WebCore.
  if (action == @selector(checkSpelling:)) {
    *valid = renderWidgetHost_->IsRenderView();
    return YES;
  }

  // TODO(groby): Clarify who sends this and if toggleContinuousSpellChecking:
  // is still necessary.
  if (action == @selector(toggleContinuousSpellChecking:)) {
    if ([(id)item respondsToSelector:@selector(setState:)]) {
      content::RenderProcessHost* host = renderWidgetHost_->GetProcess();
      Profile* profile = Profile::FromBrowserContext(host->GetBrowserContext());
      DCHECK(profile);
      spellcheckChecked_ =
          profile->GetPrefs()->GetBoolean(prefs::kEnableContinuousSpellcheck);
      NSCellStateValue checkedState =
          spellcheckChecked_ ? NSOnState : NSOffState;
      [(id)item setState:checkedState];
    }
    *valid = spellcheckEnabled_;
    return YES;
  }

  return NO;
}

// Spellchecking methods
// The next five methods are implemented here since this class is the first
// responder for anything in the browser.

// This message is sent whenever the user specifies that a word should be
// changed from the spellChecker.
- (void)changeSpelling:(id)sender {
  // Grab the currently selected word from the spell panel, as this is the word
  // that we want to replace the selected word in the text with.
  NSString* newWord = [[sender selectedCell] stringValue];
  if (newWord != nil) {
    renderWidgetHost_->Replace(base::SysNSStringToUTF16(newWord));
  }
}

// This message is sent by NSSpellChecker whenever the next word should be
// advanced to, either after a correction or clicking the "Find Next" button.
// This isn't documented anywhere useful, like in NSSpellProtocol.h with the
// other spelling panel methods. This is probably because Apple assumes that the
// the spelling panel will be used with an NSText, which will automatically
// catch this and advance to the next word for you. Thanks Apple.
// This is also called from the Edit -> Spelling -> Check Spelling menu item.
- (void)checkSpelling:(id)sender {
  renderWidgetHost_->Send(new SpellCheckMsg_AdvanceToNextMisspelling(
      renderWidgetHost_->GetRoutingID()));
}

// This message is sent by the spelling panel whenever a word is ignored.
- (void)ignoreSpelling:(id)sender {
  // Ideally, we would ask the current RenderView for its tag, but that would
  // mean making a blocking IPC call from the browser. Instead,
  // spellcheck_mac::CheckSpelling remembers the last tag and
  // spellcheck_mac::IgnoreWord assumes that is the correct tag.
  NSString* wordToIgnore = [sender stringValue];
  if (wordToIgnore != nil)
    spellcheck_mac::IgnoreWord(base::SysNSStringToUTF16(wordToIgnore));
}

- (void)showGuessPanel:(id)sender {
  renderWidgetHost_->Send(new SpellCheckMsg_ToggleSpellPanel(
      renderWidgetHost_->GetRoutingID(),
      spellcheck_mac::SpellingPanelVisible()));
}

- (void)toggleContinuousSpellChecking:(id)sender {
  content::RenderProcessHost* host = renderWidgetHost_->GetProcess();
  Profile* profile = Profile::FromBrowserContext(host->GetBrowserContext());
  DCHECK(profile);
  PrefService* pref = profile->GetPrefs();
  pref->SetBoolean(prefs::kEnableContinuousSpellcheck,
                   !pref->GetBoolean(prefs::kEnableContinuousSpellcheck));
}

- (void)spellCheckEnabled:(BOOL)enabled checked:(BOOL)checked {
  spellcheckEnabled_ = enabled;
  spellcheckChecked_ = checked;
}

// END Spellchecking methods

@end
