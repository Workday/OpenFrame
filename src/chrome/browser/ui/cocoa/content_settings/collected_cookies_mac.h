// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <Cocoa/Cocoa.h>

#include "base/mac/scoped_nsobject.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/browsing_data/cookies_tree_model.h"
#include "chrome/browser/ui/cocoa/constrained_window/constrained_window_mac.h"
#import "chrome/browser/ui/cocoa/content_settings/cookie_tree_node.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"

@class CollectedCookiesWindowController;
@class CookieDetailsViewController;
@class VerticalGradientView;

namespace content {
class WebContents;
}

// The constrained window delegate reponsible for managing the collected
// cookies dialog.
class CollectedCookiesMac : public ConstrainedWindowMacDelegate,
                            public content::NotificationObserver {
 public:
  CollectedCookiesMac(content::WebContents* web_contents);
  virtual ~CollectedCookiesMac();

  void PerformClose();

  // ConstrainedWindowMacDelegate implementation.
  virtual void OnConstrainedWindowClosed(
      ConstrainedWindowMac* window) OVERRIDE;

  CollectedCookiesWindowController* sheet_controller() const {
    return sheet_controller_.get();
  }

 private:
  // NotificationObserver implementation.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  content::NotificationRegistrar registrar_;

  scoped_ptr<ConstrainedWindowMac> window_;

  base::scoped_nsobject<CollectedCookiesWindowController> sheet_controller_;

  DISALLOW_COPY_AND_ASSIGN(CollectedCookiesMac);
};

// Controller for the collected cookies dialog. This class stores an internal
// copy of the CookiesTreeModel but with Cocoa-converted values (NSStrings and
// NSImages instead of std::strings and ImageSkias). Doing this allows us to use
// bindings for the interface. Changes are pushed to this internal model via a
// very thin bridge (see cookies_window_controller.h).
@interface CollectedCookiesWindowController : NSWindowController
                                              <NSOutlineViewDelegate,
                                               NSTabViewDelegate,
                                               NSWindowDelegate> {
 @private
  // Platform-independent model.
  scoped_ptr<CookiesTreeModel> allowedTreeModel_;
  scoped_ptr<CookiesTreeModel> blockedTreeModel_;

  // Cached array of icons.
  base::scoped_nsobject<NSMutableArray> icons_;

  // Our Cocoa copy of the model.
  base::scoped_nsobject<CocoaCookieTreeNode> cocoaAllowedTreeModel_;
  base::scoped_nsobject<CocoaCookieTreeNode> cocoaBlockedTreeModel_;

  BOOL allowedCookiesButtonsEnabled_;
  BOOL blockedCookiesButtonsEnabled_;

  IBOutlet NSTreeController* allowedTreeController_;
  IBOutlet NSTreeController* blockedTreeController_;
  IBOutlet NSOutlineView* allowedOutlineView_;
  IBOutlet NSOutlineView* blockedOutlineView_;
  IBOutlet VerticalGradientView* infoBar_;
  IBOutlet NSImageView* infoBarIcon_;
  IBOutlet NSTextField* infoBarText_;
  IBOutlet NSTabView* tabView_;
  IBOutlet NSScrollView* blockedScrollView_;
  IBOutlet NSTextField* blockedCookiesText_;
  IBOutlet NSView* cookieDetailsViewPlaceholder_;

  base::scoped_nsobject<NSViewAnimation> animation_;

  base::scoped_nsobject<CookieDetailsViewController> detailsViewController_;

  content::WebContents* webContents_;  // weak

  CollectedCookiesMac* collectedCookiesMac_;  // weak

  BOOL infoBarVisible_;

  BOOL contentSettingsChanged_;
}

@property(readonly, nonatomic) IBOutlet NSTreeController* allowedTreeController;
@property(readonly, nonatomic) IBOutlet NSTreeController* blockedTreeController;
@property(readonly, nonatomic) IBOutlet NSOutlineView* allowedOutlineView;
@property(readonly, nonatomic) IBOutlet NSOutlineView* blockedOutlineView;
@property(readonly, nonatomic) IBOutlet VerticalGradientView* infoBar;
@property(readonly, nonatomic) IBOutlet NSImageView* infoBarIcon;
@property(readonly, nonatomic) IBOutlet NSTextField* infoBarText;
@property(readonly, nonatomic) IBOutlet NSTabView* tabView;
@property(readonly, nonatomic) IBOutlet NSScrollView* blockedScrollView;
@property(readonly, nonatomic) IBOutlet NSTextField* blockedCookiesText;
@property(readonly, nonatomic) IBOutlet NSView* cookieDetailsViewPlaceholder;

@property(assign, nonatomic) BOOL allowedCookiesButtonsEnabled;
@property(assign, nonatomic) BOOL blockedCookiesButtonsEnabled;

// Designated initializer. The WebContents cannot be NULL.
- (id)initWithWebContents:(content::WebContents*)webContents
      collectedCookiesMac:(CollectedCookiesMac*)collectedCookiesMac;

// Closes the sheet and ends the modal loop. This will also clean up the memory.
- (IBAction)closeSheet:(id)sender;

- (IBAction)allowOrigin:(id)sender;
- (IBAction)allowForSessionFromOrigin:(id)sender;
- (IBAction)blockOrigin:(id)sender;

// Returns the |cocoaAllowedTreeModel_| and |cocoaBlockedTreeModel_|.
- (CocoaCookieTreeNode*)cocoaAllowedTreeModel;
- (CocoaCookieTreeNode*)cocoaBlockedTreeModel;
- (void)setCocoaAllowedTreeModel:(CocoaCookieTreeNode*)model;
- (void)setCocoaBlockedTreeModel:(CocoaCookieTreeNode*)model;

// Returns the |allowedTreeModel_| and |blockedTreeModel_|.
- (CookiesTreeModel*)allowedTreeModel;
- (CookiesTreeModel*)blockedTreeModel;

- (void)loadTreeModelFromWebContents;
@end
