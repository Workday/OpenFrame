// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_THEMED_WINDOW_H_
#define CHROME_BROWSER_UI_COCOA_THEMED_WINDOW_H_

#import <Cocoa/Cocoa.h>

namespace ui {
class ThemeProvider;
}
using ui::ThemeProvider;

// Bit flags; mix-and-match as necessary.
enum {
  THEMED_NORMAL    = 0,
  THEMED_INCOGNITO = 1 << 0,
  THEMED_POPUP     = 1 << 1,
  THEMED_DEVTOOLS  = 1 << 2
};
typedef NSUInteger ThemedWindowStyle;

// Indicates how the theme image should be aligned.
enum ThemePatternAlignment {
  // Aligns the top of the theme image with the top of the frame. Use this
  // for IDR_THEME_THEME_FRAME.*
  THEME_PATTERN_ALIGN_WITH_FRAME,
  // Aligns the top of the theme image with the top of the tab
  // strip. Use this for IDR_THEME_TAB_BACKGROUND and IDR_THEME_TOOLBAR.
  THEME_PATTERN_ALIGN_WITH_TAB_STRIP
};

// Implemented by windows that support theming.

@interface NSWindow (ThemeProvider)
- (ThemeProvider*)themeProvider;
- (ThemedWindowStyle)themedWindowStyle;
- (NSPoint)themePatternPhaseForAlignment:(ThemePatternAlignment)alignment;
@end

#endif  // CHROME_BROWSER_UI_COCOA_THEMED_WINDOW_H_
