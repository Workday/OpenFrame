// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/browser_window_controller.h"

#import "base/mac/mac_util.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/infobars/confirm_infobar_delegate.h"
#include "chrome/browser/infobars/infobar_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_tabstrip.cc"
#include "chrome/browser/ui/browser_window.h"
#import "chrome/browser/ui/cocoa/browser/avatar_button_controller.h"
#include "chrome/browser/ui/cocoa/browser_window_cocoa.h"
#import "chrome/browser/ui/cocoa/browser_window_controller_private.h"
#import "chrome/browser/ui/cocoa/fast_resize_view.h"
#import "chrome/browser/ui/cocoa/history_overlay_controller.h"
#import "chrome/browser/ui/cocoa/infobars/infobar_container_controller.h"
#import "chrome/browser/ui/cocoa/nsview_additions.h"
#import "chrome/browser/ui/cocoa/tab_contents/overlayable_contents_controller.h"
#include "chrome/browser/ui/extensions/application_launch.h"
#include "chrome/browser/ui/find_bar/find_bar_controller.h"
#include "chrome/browser/ui/find_bar/find_bar.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#import "testing/gtest_mac.h"

namespace {

#if !defined(MAC_OS_X_VERSION_10_7) || \
    MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_X_VERSION_10_7
enum {
  NSWindowDocumentVersionsButton = 6,
  NSWindowFullScreenButton
};
#endif  // MAC_OS_X_VERSION_10_7

void CreateProfileCallback(const base::Closure& quit_closure,
                           Profile* profile,
                           Profile::CreateStatus status) {
  EXPECT_TRUE(profile);
  EXPECT_NE(Profile::CREATE_STATUS_LOCAL_FAIL, status);
  EXPECT_NE(Profile::CREATE_STATUS_REMOTE_FAIL, status);
  // This will be called multiple times. Wait until the profile is initialized
  // fully to quit the loop.
  if (status == Profile::CREATE_STATUS_INITIALIZED)
    quit_closure.Run();
}

enum ViewID {
  VIEW_ID_TOOLBAR,
  VIEW_ID_BOOKMARK_BAR,
  VIEW_ID_INFO_BAR,
  VIEW_ID_FIND_BAR,
  VIEW_ID_DOWNLOAD_SHELF,
  VIEW_ID_TAB_CONTENT_AREA,
  VIEW_ID_FULLSCREEN_FLOATING_BAR,
  VIEW_ID_COUNT,
};

// A very simple info bar implementation used to show an infobar on the browser
// window.
class DummyInfoBar : public ConfirmInfoBarDelegate {
 public:
  explicit DummyInfoBar(InfoBarService* service)
      : ConfirmInfoBarDelegate(service) {
  }

  virtual ~DummyInfoBar() {
  }

  virtual string16 GetMessageText() const OVERRIDE {
    return string16();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(DummyInfoBar);
};

}  // namespace

class BrowserWindowControllerTest : public InProcessBrowserTest {
 public:
  BrowserWindowControllerTest() : InProcessBrowserTest() {
  }

  virtual void SetUpOnMainThread() OVERRIDE {
    [[controller() bookmarkBarController] setStateAnimationsEnabled:NO];
    [[controller() bookmarkBarController] setInnerContentAnimationsEnabled:NO];
  }

  BrowserWindowController* controller() const {
    return [BrowserWindowController browserWindowControllerForWindow:
        browser()->window()->GetNativeWindow()];
  }

  void ShowInfoBar() {
    content::WebContents* web_contents =
        browser()->tab_strip_model()->GetActiveWebContents();
    InfoBarService* service =
        InfoBarService::FromWebContents(web_contents);
    info_bar_delegate_.reset(new DummyInfoBar(service));
    [[controller() infoBarContainerController]
        addInfoBar:info_bar_delegate_->CreateInfoBar(service)
           animate:NO];
  }

  NSView* GetViewWithID(ViewID view_id) const {
    switch (view_id) {
      case VIEW_ID_FULLSCREEN_FLOATING_BAR:
        return [controller() floatingBarBackingView];
      case VIEW_ID_TOOLBAR:
        return [[controller() toolbarController] view];
      case VIEW_ID_BOOKMARK_BAR:
        return [[controller() bookmarkBarController] view];
      case VIEW_ID_INFO_BAR:
        return [[controller() infoBarContainerController] view];
      case VIEW_ID_FIND_BAR:
        return [[controller() findBarCocoaController] view];
      case VIEW_ID_DOWNLOAD_SHELF:
        return [[controller() downloadShelf] view];
      case VIEW_ID_TAB_CONTENT_AREA:
        return [controller() tabContentArea];
      default:
        NOTREACHED();
        return nil;
    }
  }

  void VerifyZOrder(const std::vector<ViewID>& view_list) const {
    for (size_t i = 0; i < view_list.size() - 1; ++i) {
      NSView* bottom_view = GetViewWithID(view_list[i]);
      NSView* top_view = GetViewWithID(view_list[i + 1]);
      EXPECT_NSEQ([bottom_view superview], [top_view superview]);
      EXPECT_TRUE([bottom_view cr_isBelowView:top_view]);
    }

    // Views not in |view_list| must either be nil or not parented.
    for (size_t i = 0; i < VIEW_ID_COUNT; ++i) {
      if (std::find(view_list.begin(), view_list.end(), i) == view_list.end()) {
        NSView* view = GetViewWithID(static_cast<ViewID>(i));
        EXPECT_TRUE(!view || ![view superview]);
      }
    }
  }

  CGFloat GetViewHeight(ViewID viewID) const {
    CGFloat height = NSHeight([GetViewWithID(viewID) frame]);
    if (viewID == VIEW_ID_INFO_BAR) {
      height -= [[controller() infoBarContainerController]
          overlappingTipHeight];
    }
    return height;
  }

 private:
  scoped_ptr<InfoBarDelegate> info_bar_delegate_;

  DISALLOW_COPY_AND_ASSIGN(BrowserWindowControllerTest);
};

// Tests that adding the first profile moves the Lion fullscreen button over
// correctly.
// DISABLED_ because it regularly times out: http://crbug.com/159002.
IN_PROC_BROWSER_TEST_F(BrowserWindowControllerTest,
                       DISABLED_ProfileAvatarFullscreenButton) {
  if (base::mac::IsOSSnowLeopard())
    return;

  // Initialize the locals.
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  ASSERT_TRUE(profile_manager);

  NSWindow* window = browser()->window()->GetNativeWindow();
  ASSERT_TRUE(window);

  // With only one profile, the fullscreen button should be visible, but the
  // avatar button should not.
  EXPECT_EQ(1u, profile_manager->GetNumberOfProfiles());

  NSButton* fullscreen_button =
      [window standardWindowButton:NSWindowFullScreenButton];
  EXPECT_TRUE(fullscreen_button);
  EXPECT_FALSE([fullscreen_button isHidden]);

  AvatarButtonController* avatar_controller =
      [controller() avatarButtonController];
  NSView* avatar = [avatar_controller view];
  EXPECT_TRUE(avatar);
  EXPECT_TRUE([avatar isHidden]);

  // Create a profile asynchronously and run the loop until its creation
  // is complete.
  base::RunLoop run_loop;

  ProfileManager::CreateCallback create_callback =
      base::Bind(&CreateProfileCallback, run_loop.QuitClosure());
  profile_manager->CreateProfileAsync(
      profile_manager->user_data_dir().Append("test"),
      create_callback,
      ASCIIToUTF16("avatar_test"),
      string16(),
      std::string());

  run_loop.Run();

  // There should now be two profiles, and the avatar button and fullscreen
  // button are both visible.
  EXPECT_EQ(2u, profile_manager->GetNumberOfProfiles());
  EXPECT_FALSE([avatar isHidden]);
  EXPECT_FALSE([fullscreen_button isHidden]);
  EXPECT_EQ([avatar window], [fullscreen_button window]);

  // Make sure the visual order of the buttons is correct and that they don't
  // overlap.
  NSRect avatar_frame = [avatar frame];
  NSRect fullscreen_frame = [fullscreen_button frame];

  EXPECT_LT(NSMinX(fullscreen_frame), NSMinX(avatar_frame));
  EXPECT_LT(NSMaxX(fullscreen_frame), NSMinX(avatar_frame));
}

// Verify that in non-Instant normal mode that the find bar and download shelf
// are above the content area. Everything else should be below it.
IN_PROC_BROWSER_TEST_F(BrowserWindowControllerTest, ZOrderNormal) {
  browser()->GetFindBarController();  // add find bar

  std::vector<ViewID> view_list;
  view_list.push_back(VIEW_ID_BOOKMARK_BAR);
  view_list.push_back(VIEW_ID_TOOLBAR);
  view_list.push_back(VIEW_ID_INFO_BAR);
  view_list.push_back(VIEW_ID_TAB_CONTENT_AREA);
  view_list.push_back(VIEW_ID_FIND_BAR);
  view_list.push_back(VIEW_ID_DOWNLOAD_SHELF);
  VerifyZOrder(view_list);
}

// Verify that in non-Instant presentation mode that the info bar is below the
// content are and everything else is above it.
// DISABLED due to flaky failures on trybots. http://crbug.com/178778
IN_PROC_BROWSER_TEST_F(BrowserWindowControllerTest,
                       DISABLED_ZOrderPresentationMode) {
  chrome::ToggleFullscreenMode(browser());
  browser()->GetFindBarController();  // add find bar

  std::vector<ViewID> view_list;
  view_list.push_back(VIEW_ID_INFO_BAR);
  view_list.push_back(VIEW_ID_TAB_CONTENT_AREA);
  view_list.push_back(VIEW_ID_FULLSCREEN_FLOATING_BAR);
  view_list.push_back(VIEW_ID_BOOKMARK_BAR);
  view_list.push_back(VIEW_ID_TOOLBAR);
  view_list.push_back(VIEW_ID_FIND_BAR);
  view_list.push_back(VIEW_ID_DOWNLOAD_SHELF);
  VerifyZOrder(view_list);
}

// Verify that if the fullscreen floating bar view is below the tab content area
// then calling |updateSubviewZOrder:| will correctly move back above.
IN_PROC_BROWSER_TEST_F(BrowserWindowControllerTest,
                       DISABLED_FloatingBarBelowContentView) {
  // TODO(kbr): re-enable: http://crbug.com/222296
  if (base::mac::IsOSMountainLionOrLater())
    return;

  chrome::ToggleFullscreenMode(browser());

  NSView* fullscreen_floating_bar =
      GetViewWithID(VIEW_ID_FULLSCREEN_FLOATING_BAR);
  [fullscreen_floating_bar removeFromSuperview];
  [[[controller() window] contentView] addSubview:fullscreen_floating_bar
                                       positioned:NSWindowBelow
                                       relativeTo:nil];
  [controller() updateSubviewZOrder:[controller() inPresentationMode]];

  std::vector<ViewID> view_list;
  view_list.push_back(VIEW_ID_INFO_BAR);
  view_list.push_back(VIEW_ID_TAB_CONTENT_AREA);
  view_list.push_back(VIEW_ID_FULLSCREEN_FLOATING_BAR);
  view_list.push_back(VIEW_ID_BOOKMARK_BAR);
  view_list.push_back(VIEW_ID_TOOLBAR);
  view_list.push_back(VIEW_ID_DOWNLOAD_SHELF);
  VerifyZOrder(view_list);
}

IN_PROC_BROWSER_TEST_F(BrowserWindowControllerTest, SheetPosition) {
  ASSERT_TRUE([controller() isKindOfClass:[BrowserWindowController class]]);
  EXPECT_TRUE([controller() isTabbedWindow]);
  EXPECT_TRUE([controller() hasTabStrip]);
  EXPECT_FALSE([controller() hasTitleBar]);
  EXPECT_TRUE([controller() hasToolbar]);
  EXPECT_FALSE([controller() isBookmarkBarVisible]);

  NSRect defaultAlertFrame = NSMakeRect(0, 0, 300, 200);
  NSWindow* window = browser()->window()->GetNativeWindow();
  NSRect alertFrame = [controller() window:window
                         willPositionSheet:nil
                                 usingRect:defaultAlertFrame];
  NSRect toolbarFrame = [[[controller() toolbarController] view] frame];
  EXPECT_EQ(NSMinY(alertFrame), NSMinY(toolbarFrame));

  // Open sheet with normal browser window, persistent bookmark bar.
  browser()->window()->ToggleBookmarkBar();
  EXPECT_TRUE([controller() isBookmarkBarVisible]);
  alertFrame = [controller() window:window
                  willPositionSheet:nil
                          usingRect:defaultAlertFrame];
  NSRect bookmarkBarFrame = [[[controller() bookmarkBarController] view] frame];
  EXPECT_EQ(NSMinY(alertFrame), NSMinY(bookmarkBarFrame));

  // Make sure the profile does not have the bookmark visible so that
  // we'll create the shortcut window without the bookmark bar.
  browser()->window()->ToggleBookmarkBar();
  // Open application mode window.
  gfx::Rect initial_bounds(0, 0, 400, 400);
  chrome::OpenAppShortcutWindow(
      browser()->profile(), GURL("about:blank"), initial_bounds);
  Browser* popup_browser = BrowserList::GetInstance(
      chrome::GetActiveDesktop())->GetLastActive();
  NSWindow* popupWindow = popup_browser->window()->GetNativeWindow();
  BrowserWindowController* popupController =
      [BrowserWindowController browserWindowControllerForWindow:popupWindow];
  ASSERT_TRUE([popupController isKindOfClass:[BrowserWindowController class]]);
  EXPECT_FALSE([popupController isTabbedWindow]);
  EXPECT_FALSE([popupController hasTabStrip]);
  EXPECT_TRUE([popupController hasTitleBar]);
  EXPECT_FALSE([popupController isBookmarkBarVisible]);
  EXPECT_FALSE([popupController hasToolbar]);

  // Open sheet in an application window.
  [popupController showWindow:nil];
  alertFrame = [popupController window:popupWindow
                     willPositionSheet:nil
                             usingRect:defaultAlertFrame];
  EXPECT_EQ(NSMinY(alertFrame),
            NSHeight([[popupWindow contentView] frame]) -
            defaultAlertFrame.size.height);

  // Close the application window.
  popup_browser->tab_strip_model()->CloseSelectedTabs();
  [popupController close];
}

// Verify that the info bar tip is hidden when the toolbar is not visible.
IN_PROC_BROWSER_TEST_F(BrowserWindowControllerTest,
                       InfoBarTipHiddenForWindowWithoutToolbar) {
  ShowInfoBar();
  EXPECT_FALSE(
      [[controller() infoBarContainerController] shouldSuppressTopInfoBarTip]);

  gfx::Rect initial_bounds(0, 0, 400, 400);
  chrome::OpenAppShortcutWindow(
      browser()->profile(), GURL("about:blank"), initial_bounds);
  Browser* popup_browser = BrowserList::GetInstance(
      chrome::HOST_DESKTOP_TYPE_NATIVE)->GetLastActive();
  NSWindow* popupWindow = popup_browser->window()->GetNativeWindow();
  BrowserWindowController* popupController =
      [BrowserWindowController browserWindowControllerForWindow:popupWindow];
  EXPECT_FALSE([popupController hasToolbar]);

  // Show infobar for controller.
  content::WebContents* web_contents =
      popup_browser->tab_strip_model()->GetActiveWebContents();
  InfoBarService* service = InfoBarService::FromWebContents(web_contents);
  scoped_ptr<InfoBarDelegate> info_bar_delegate(new DummyInfoBar(service));
  [[popupController infoBarContainerController]
      addInfoBar:info_bar_delegate->CreateInfoBar(service)
         animate:NO];

  EXPECT_TRUE(
      [[popupController infoBarContainerController]
          shouldSuppressTopInfoBarTip]);
}

// Verify that AllowOverlappingViews is set while the history overlay is
// visible.
IN_PROC_BROWSER_TEST_F(BrowserWindowControllerTest,
                       AllowOverlappingViewsHistoryOverlay) {
  content::WebContentsView* web_contents_view =
      browser()->tab_strip_model()->GetActiveWebContents()->GetView();
  EXPECT_FALSE(web_contents_view->GetAllowOverlappingViews());

  base::scoped_nsobject<HistoryOverlayController> overlay(
      [[HistoryOverlayController alloc] initForMode:kHistoryOverlayModeBack]);
  [overlay showPanelForView:web_contents_view->GetNativeView()];
  EXPECT_TRUE(web_contents_view->GetAllowOverlappingViews());

  overlay.reset();
  EXPECT_FALSE(web_contents_view->GetAllowOverlappingViews());
}
