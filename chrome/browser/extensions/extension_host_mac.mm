// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_host_mac.h"

#import "chrome/browser/ui/cocoa/chrome_event_processing_window.h"
#import "chrome/browser/ui/cocoa/extensions/extension_popup_controller.h"
#import "chrome/browser/ui/cocoa/info_bubble_window.h"
#include "content/public/browser/native_web_keyboard_event.h"

using content::NativeWebKeyboardEvent;

namespace extensions {

ExtensionHostMac::~ExtensionHostMac() {
  // If there is a popup open for this host's extension, close it.
  ExtensionPopupController* popup = [ExtensionPopupController popup];
  if ([[popup window] isVisible] &&
      [popup extensionHost]->extension() == this->extension()) {
    InfoBubbleWindow* window = (InfoBubbleWindow*)[popup window];
    [window setAllowedAnimations:info_bubble::kAnimateNone];
    [popup close];
  }
}

void ExtensionHostMac::UnhandledKeyboardEvent(
    content::WebContents* source,
    const NativeWebKeyboardEvent& event) {
  if (event.skip_in_browser || event.type == NativeWebKeyboardEvent::Char ||
      extension_host_type() != VIEW_TYPE_EXTENSION_POPUP) {
    return;
  }

  ChromeEventProcessingWindow* event_window =
      static_cast<ChromeEventProcessingWindow*>([view()->native_view() window]);
  DCHECK([event_window isKindOfClass:[ChromeEventProcessingWindow class]]);
  [event_window redispatchKeyEvent:event.os_event];
}

}  // namespace extensions
