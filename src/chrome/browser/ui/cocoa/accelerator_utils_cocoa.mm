// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/cocoa/accelerators_cocoa.h"
#import "chrome/browser/ui/cocoa/browser_window_utils.h"
#include "content/public/browser/native_web_keyboard_event.h"
#include "ui/base/accelerators/accelerator.h"
#import "ui/base/accelerators/platform_accelerator_cocoa.h"
#import "ui/base/keycodes/keyboard_code_conversion_mac.h"

namespace chrome {

bool IsChromeAccelerator(const ui::Accelerator& accelerator, Profile* profile) {
  // The |accelerator| passed in contains a Windows key code but no platform
  // accelerator info. The Accelerator list is the opposite: It has accelerators
  // that have key_code() == VKEY_UNKNOWN but they contain a platform
  // accelerator. We find common ground by converting the passed in Windows key
  // code to a character and use that when comparing against the Accelerator
  // list.
  unichar character;
  unichar characterIgnoringModifiers;
  ui::MacKeyCodeForWindowsKeyCode(accelerator.key_code(),
                                  0,
                                  &character,
                                  &characterIgnoringModifiers);
  NSString* characters =
      [[[NSString alloc] initWithCharacters:&character length:1] autorelease];

  NSUInteger modifiers =
      (accelerator.IsCtrlDown() ? NSControlKeyMask : 0) |
      (accelerator.IsCmdDown() ? NSCommandKeyMask : 0) |
      (accelerator.IsAltDown() ? NSAlternateKeyMask : 0) |
      (accelerator.IsShiftDown() ? NSShiftKeyMask : 0);

  NSEvent* event = [NSEvent keyEventWithType:NSKeyDown
                                    location:NSZeroPoint
                               modifierFlags:modifiers
                                   timestamp:0
                                windowNumber:0
                                     context:nil
                                  characters:characters
                 charactersIgnoringModifiers:characters
                                   isARepeat:NO
                                     keyCode:accelerator.key_code()];

  content::NativeWebKeyboardEvent keyboard_event(event);
  int id = [BrowserWindowUtils getCommandId:keyboard_event];
  return id != -1;
}

}  // namespace chrome
