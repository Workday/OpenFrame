// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_BROWSER_AVATAR_BUTTON_CONTROLLER_H_
#define CHROME_BROWSER_UI_COCOA_BROWSER_AVATAR_BUTTON_CONTROLLER_H_

#import <AppKit/AppKit.h>

#import "base/mac/scoped_nsobject.h"
#include "base/memory/scoped_ptr.h"

@class AvatarLabelButton;
@class AvatarMenuBubbleController;
class Browser;

namespace AvatarButtonControllerInternal {
class Observer;
}

// This view controller manages the button/image that sits in the top of the
// window frame when using multi-profiles. It shows the current profile's
// avatar, or, when in Incognito, the spy dude. With multi-profiles, clicking
// will open the profile menu; in Incognito, clicking will do nothing.
@interface AvatarButtonController : NSViewController {
 @private
  Browser* browser_;

  // Notification bridge for profile info updates.
  scoped_ptr<AvatarButtonControllerInternal::Observer> observer_;

  // The menu controller, if the menu is open.
  __weak AvatarMenuBubbleController* menuController_;

  // The avatar button.
  base::scoped_nsobject<NSButton> button_;

  // The managed user avatar label button. Only used for managed user profiles.
  base::scoped_nsobject<AvatarLabelButton> labelButton_;
}

// The avatar button view.
@property(readonly, nonatomic) NSButton* buttonView;

// The managed user avatar label button view.
@property(readonly, nonatomic) NSButton* labelButtonView;

// Designated initializer.
- (id)initWithBrowser:(Browser*)browser;

// Sets the image to be used as the avatar. This will have a drop shadow applied
// and will be resized to the frame of the button.
- (void)setImage:(NSImage*)image;

// Shows the avatar bubble.
- (void)showAvatarBubble:(NSView*)anchor;

@end

@interface AvatarButtonController (ExposedForTesting)
- (AvatarMenuBubbleController*)menuController;
@end

#endif  // CHROME_BROWSER_UI_COCOA_BROWSER_AVATAR_BUTTON_CONTROLLER_H_
