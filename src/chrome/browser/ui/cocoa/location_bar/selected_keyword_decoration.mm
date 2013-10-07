// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/location_bar/selected_keyword_decoration.h"

#include "base/strings/sys_string_conversions.h"
#import "chrome/browser/ui/cocoa/omnibox/omnibox_view_mac.h"
#include "chrome/browser/ui/omnibox/location_bar_util.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "ui/base/l10n/l10n_util_mac.h"

SelectedKeywordDecoration::SelectedKeywordDecoration() {
  search_image_.reset([OmniboxViewMac::ImageForResource(
      IDR_KEYWORD_SEARCH_MAGNIFIER) retain]);

  // Matches the color of the highlighted line in the popup.
  NSColor* background_color = [NSColor selectedControlColor];

  // Match focus ring's inner color.
  NSColor* border_color =
      [[NSColor keyboardFocusIndicatorColor] colorWithAlphaComponent:0.5];
  SetColors(border_color, background_color, [NSColor blackColor]);
}

SelectedKeywordDecoration::~SelectedKeywordDecoration() {}

CGFloat SelectedKeywordDecoration::GetWidthForSpace(CGFloat width) {
  const CGFloat full_width =
      GetWidthForImageAndLabel(search_image_, full_string_);
  if (full_width <= width) {
    BubbleDecoration::SetImage(search_image_);
    SetLabel(full_string_);
    return full_width;
  }

  BubbleDecoration::SetImage(nil);
  const CGFloat no_image_width = GetWidthForImageAndLabel(nil, full_string_);
  if (no_image_width <= width || !partial_string_) {
    SetLabel(full_string_);
    return no_image_width;
  }

  SetLabel(partial_string_);
  return GetWidthForImageAndLabel(nil, partial_string_);
}

void SelectedKeywordDecoration::SetKeyword(const string16& short_name,
                                           bool is_extension_keyword) {
  const string16 min_name(location_bar_util::CalculateMinString(short_name));
  NSString* full_string = is_extension_keyword ?
      base::SysUTF16ToNSString(short_name) :
      l10n_util::GetNSStringF(IDS_OMNIBOX_KEYWORD_TEXT, short_name);

  // The text will be like "Search <name>:".  "<name>" is a parameter
  // derived from |short_name|.
  full_string_.reset([full_string copy]);

  if (min_name.empty()) {
    partial_string_.reset();
  } else {
    NSString* partial_string = is_extension_keyword ?
        base::SysUTF16ToNSString(min_name) :
        l10n_util::GetNSStringF(IDS_OMNIBOX_KEYWORD_TEXT, min_name);
    partial_string_.reset([partial_string copy]);
  }
}

void SelectedKeywordDecoration::SetImage(NSImage* image) {
  if (image != search_image_)
    search_image_.reset([image retain]);
  BubbleDecoration::SetImage(image);
}
