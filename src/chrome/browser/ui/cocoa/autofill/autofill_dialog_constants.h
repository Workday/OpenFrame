// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_AUTOFILL_AUTOFILL_DIALOG_CONSTANTS__H_
#define CHROME_BROWSER_UI_COCOA_AUTOFILL_AUTOFILL_DIALOG_CONSTANTS__H_

// Constants governing layout of autofill dialog.
namespace {

// Horizontal padding between text and other elements (in pixels).
const CGFloat kAroundTextPadding = 4;

// Sizing of notification arrow.
const int kArrowHeight = 7;
const int kArrowWidth = 2 * kArrowHeight;

// Spacing between buttons.
const CGFloat kButtonGap = 6;

// The space between the edges of a notification bar and the text within (in
// pixels).
const int kNotificationPadding = 14;

// Vertical spacing between legal text and details section.
const int kVerticalSpacing = 8;

// Padding between top bar and details section.
const int kDetailTopPadding = 20;

// Padding between the bottom of the details section and the button strip.
const int kDetailBottomPadding = 30;

}  // namespace

#endif  // CHROME_BROWSER_UI_COCOA_AUTOFILL_AUTOFILL_DIALOG_CONSTANTS__H_
