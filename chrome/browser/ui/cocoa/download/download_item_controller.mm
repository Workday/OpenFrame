// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/download/download_item_controller.h"

#include "base/mac/bundle_locations.h"
#include "base/mac/mac_util.h"
#include "base/metrics/histogram.h"
#include "base/strings/string16.h"
#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/download/chrome_download_manager_delegate.h"
#include "chrome/browser/download/download_item_model.h"
#include "chrome/browser/download/download_shelf_context_menu.h"
#include "chrome/browser/download/download_util.h"
#import "chrome/browser/themes/theme_properties.h"
#import "chrome/browser/themes/theme_service.h"
#import "chrome/browser/ui/cocoa/download/download_item_button.h"
#import "chrome/browser/ui/cocoa/download/download_item_cell.h"
#include "chrome/browser/ui/cocoa/download/download_item_mac.h"
#import "chrome/browser/ui/cocoa/download/download_shelf_controller.h"
#import "chrome/browser/ui/cocoa/themed_window.h"
#import "chrome/browser/ui/cocoa/ui_localizer.h"
#include "content/public/browser/download_item.h"
#include "content/public/browser/page_navigator.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "third_party/GTM/AppKit/GTMUILocalizerAndLayoutTweaker.h"
#include "ui/base/l10n/l10n_util_mac.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/text/text_elider.h"
#include "ui/gfx/font.h"
#include "ui/gfx/image/image.h"

using content::DownloadItem;

namespace {

// NOTE: Mac currently doesn't use this like Windows does.  Mac uses this to
// control the min size on the dangerous download text.  TVL sent a query off to
// UX to fully spec all the the behaviors of download items and truncations
// rules so all platforms can get inline in the future.
const int kTextWidth = 140;            // Pixels

// The maximum width in pixels for the file name tooltip.
const int kToolTipMaxWidth = 900;


// Helper to widen a view.
void WidenView(NSView* view, CGFloat widthChange) {
  // If it is an NSBox, the autoresize of the contentView is the issue.
  NSView* contentView = view;
  if ([view isKindOfClass:[NSBox class]]) {
    contentView = [(NSBox*)view contentView];
  }
  BOOL autoresizesSubviews = [contentView autoresizesSubviews];
  if (autoresizesSubviews) {
    [contentView setAutoresizesSubviews:NO];
  }

  NSRect frame = [view frame];
  frame.size.width += widthChange;
  [view setFrame:frame];

  if (autoresizesSubviews) {
    [contentView setAutoresizesSubviews:YES];
  }
}

}  // namespace

class DownloadShelfContextMenuMac : public DownloadShelfContextMenu {
 public:
  DownloadShelfContextMenuMac(DownloadItem* downloadItem,
                              content::PageNavigator* navigator)
      : DownloadShelfContextMenu(downloadItem, navigator) { }

  // DownloadShelfContextMenu::GetMenuModel is protected.
  using DownloadShelfContextMenu::GetMenuModel;
};

@interface DownloadItemController (Private)
- (void)themeDidChangeNotification:(NSNotification*)aNotification;
- (void)updateTheme:(ui::ThemeProvider*)themeProvider;
- (void)setState:(DownoadItemState)state;
@end

// Implementation of DownloadItemController

@implementation DownloadItemController

- (id)initWithDownload:(DownloadItem*)downloadItem
                 shelf:(DownloadShelfController*)shelf
             navigator:(content::PageNavigator*)navigator {
  if ((self = [super initWithNibName:@"DownloadItem"
                              bundle:base::mac::FrameworkBundle()])) {
    // Must be called before [self view], so that bridge_ is set in awakeFromNib
    bridge_.reset(new DownloadItemMac(downloadItem, self));
    menuBridge_.reset(new DownloadShelfContextMenuMac(downloadItem, navigator));

    NSNotificationCenter* defaultCenter = [NSNotificationCenter defaultCenter];
    [defaultCenter addObserver:self
                      selector:@selector(themeDidChangeNotification:)
                          name:kBrowserThemeDidChangeNotification
                        object:nil];

    shelf_ = shelf;
    state_ = kNormal;
    creationTime_ = base::Time::Now();
    font_.reset(new gfx::Font());
  }
  return self;
}

- (void)dealloc {
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [progressView_ setController:nil];
  [[self view] removeFromSuperview];
  [super dealloc];
}

- (void)awakeFromNib {
  [progressView_ setController:self];

  [self setStateFromDownload:bridge_->download_model()];

  GTMUILocalizerAndLayoutTweaker* localizerAndLayoutTweaker =
      [[[GTMUILocalizerAndLayoutTweaker alloc] init] autorelease];
  [localizerAndLayoutTweaker applyLocalizer:localizer_ tweakingUI:[self view]];

  // The strings are based on the download item's name, sizing tweaks have to be
  // manually done.
  DCHECK(buttonTweaker_ != nil);
  CGFloat widthChange = [buttonTweaker_ changedWidth];
  // If it's a dangerous download, size the two lines so the text/filename
  // is always visible.
  if ([self isDangerousMode]) {
    widthChange +=
        [GTMUILocalizerAndLayoutTweaker
          sizeToFitFixedHeightTextField:dangerousDownloadLabel_
                               minWidth:kTextWidth];
  }
  // Grow the parent views
  WidenView([self view], widthChange);
  WidenView(dangerousDownloadView_, widthChange);
  // Slide the two buttons over.
  NSPoint frameOrigin = [buttonTweaker_ frame].origin;
  frameOrigin.x += widthChange;
  [buttonTweaker_ setFrameOrigin:frameOrigin];

  bridge_->LoadIcon();
  [self updateToolTip];
}

- (void)setStateFromDownload:(DownloadItemModel*)downloadModel {
  DCHECK_EQ([self download], downloadModel->download());

  // Handle dangerous downloads.
  if (downloadModel->IsDangerous()) {
    [self setState:kDangerous];

    ResourceBundle& rb = ResourceBundle::GetSharedInstance();
    NSString* dangerousWarning;
    NSString* confirmButtonTitle;
    NSImage* alertIcon;

    dangerousWarning =
        base::SysUTF16ToNSString(downloadModel->GetWarningText(
            *font_, kTextWidth));
    confirmButtonTitle =
        base::SysUTF16ToNSString(downloadModel->GetWarningConfirmButtonText());
    if (downloadModel->IsMalicious())
      alertIcon = rb.GetNativeImageNamed(IDR_SAFEBROWSING_WARNING).ToNSImage();
    else
      alertIcon = rb.GetNativeImageNamed(IDR_WARNING).ToNSImage();
    DCHECK(alertIcon);
    [image_ setImage:alertIcon];
    DCHECK(dangerousWarning);
    [dangerousDownloadLabel_ setStringValue:dangerousWarning];
    DCHECK(confirmButtonTitle);
    [dangerousDownloadConfirmButton_ setTitle:confirmButtonTitle];
    return;
  }

  // Set path to draggable download on completion.
  if (downloadModel->download()->GetState() == DownloadItem::COMPLETE)
    [progressView_ setDownload:downloadModel->download()->GetTargetFilePath()];

  [cell_ setStateFromDownload:downloadModel];
}

- (void)setIcon:(NSImage*)icon {
  [cell_ setImage:icon];
}

- (void)remove {
  // We are deleted after this!
  [shelf_ remove:self];
}

- (void)updateVisibility:(id)sender {
  if ([[self view] window])
    [self updateTheme:[[[self view] window] themeProvider]];

  NSView* view = [self view];
  NSRect containerFrame = [[view superview] frame];
  [view setHidden:(NSMaxX([view frame]) > NSWidth(containerFrame))];
}

- (void)downloadWasOpened {
  [shelf_ downloadWasOpened:self];
}

- (IBAction)handleButtonClick:(id)sender {
  NSEvent* event = [NSApp currentEvent];
  DownloadItem* download = [self download];
  if ([event modifierFlags] & NSCommandKeyMask) {
    // Let cmd-click show the file in Finder, like e.g. in Safari and Spotlight.
    download->ShowDownloadInShell();
  } else {
    download->OpenDownload();
  }
}

- (NSSize)preferredSize {
  if (state_ == kNormal)
    return [progressView_ frame].size;
  DCHECK_EQ(kDangerous, state_);
  return [dangerousDownloadView_ frame].size;
}

- (DownloadItem*)download {
  return bridge_->download_model()->download();
}

- (ui::MenuModel*)contextMenuModel {
  return menuBridge_->GetMenuModel();
}

- (void)updateToolTip {
  string16 tooltip_text =
      bridge_->download_model()->GetTooltipText(*font_, kToolTipMaxWidth);
  [progressView_ setToolTip:base::SysUTF16ToNSString(tooltip_text)];
}

- (void)clearDangerousMode {
  [self setState:kNormal];
  // The state change hide the dangerouse download view and is now showing the
  // download progress view.  This means the view is likely to be a different
  // size, so trigger a shelf layout to fix up spacing.
  [shelf_ layoutItems];
}

- (BOOL)isDangerousMode {
  return state_ == kDangerous;
}

- (void)setState:(DownoadItemState)state {
  if (state_ == state)
    return;
  state_ = state;
  if (state_ == kNormal) {
    [progressView_ setHidden:NO];
    [dangerousDownloadView_ setHidden:YES];
  } else {
    DCHECK_EQ(kDangerous, state_);
    [progressView_ setHidden:YES];
    [dangerousDownloadView_ setHidden:NO];
  }
  // NOTE: Do not relayout the shelf, as this could get called during initial
  // setup of the the item, so the localized text and sizing might not have
  // happened yet.
}

// Called after a theme change took place, possibly for a different profile.
- (void)themeDidChangeNotification:(NSNotification*)notification {
  [self updateTheme:[[[self view] window] themeProvider]];
}

// Adapt appearance to the current theme. Called after theme changes and before
// this is shown for the first time.
- (void)updateTheme:(ui::ThemeProvider*)themeProvider {
  if (!themeProvider)
    return;

  NSColor* color = themeProvider->GetNSColor(ThemeProperties::COLOR_TAB_TEXT);
  [dangerousDownloadLabel_ setTextColor:color];
}

- (IBAction)saveDownload:(id)sender {
  // The user has confirmed a dangerous download.  We record how quickly the
  // user did this to detect whether we're being clickjacked.
  UMA_HISTOGRAM_LONG_TIMES("clickjacking.save_download",
                           base::Time::Now() - creationTime_);
  // This will change the state and notify us.
  bridge_->download_model()->download()->ValidateDangerousDownload();
}

- (IBAction)discardDownload:(id)sender {
  UMA_HISTOGRAM_LONG_TIMES("clickjacking.discard_download",
                           base::Time::Now() - creationTime_);
  DownloadItem* download = bridge_->download_model()->download();
  download->Remove();
  // WARNING: we are deleted at this point.  Don't access 'this'.
}

@end
