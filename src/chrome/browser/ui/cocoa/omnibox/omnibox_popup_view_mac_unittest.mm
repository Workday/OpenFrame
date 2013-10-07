// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/omnibox/omnibox_popup_view_mac.h"

#include "base/memory/scoped_ptr.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/cocoa/cocoa_profile_test.h"
#import "chrome/browser/ui/cocoa/omnibox/omnibox_view_mac.h"
#include "ui/base/text/text_elider.h"

namespace {

const float kLargeWidth = 10000;

// Returns the length of the run starting at |location| for which
// |attributeName| remains the same.
NSUInteger RunLengthForAttribute(NSAttributedString* string,
                                 NSUInteger location,
                                 NSString* attributeName) {
  const NSRange full_range = NSMakeRange(0, [string length]);
  NSRange range;
  [string attribute:attributeName
            atIndex:location longestEffectiveRange:&range inRange:full_range];

  // In order to signal when the run doesn't start exactly at location, return
  // a weirdo length.  This causes the incorrect expectation to manifest at the
  // calling location, which is more useful than an EXPECT_EQ() would be here.
  if (range.location != location) {
    return -1;
  }

  return range.length;
}

// Return true if the run starting at |location| has |color| for attribute
// NSForegroundColorAttributeName.
bool RunHasColor(NSAttributedString* string,
                 NSUInteger location,
                 NSColor* color) {
  const NSRange full_range = NSMakeRange(0, [string length]);
  NSRange range;
  NSColor* run_color = [string attribute:NSForegroundColorAttributeName
                                 atIndex:location
                   longestEffectiveRange:&range
                                 inRange:full_range];

  // According to one "Ali Ozer", you can compare objects within the same color
  // space using -isEqual:.  Converting color spaces seems too heavyweight for
  // these tests.
  // http://lists.apple.com/archives/cocoa-dev/2005/May/msg00186.html
  return [run_color isEqual:color] ? true : false;
}

// Return true if the run starting at |location| has the font trait(s) in |mask|
// font in NSFontAttributeName.
bool RunHasFontTrait(NSAttributedString* string,
                     NSUInteger location,
                     NSFontTraitMask mask) {
  const NSRange full_range = NSMakeRange(0, [string length]);
  NSRange range;
  NSFont* run_font = [string attribute:NSFontAttributeName
                               atIndex:location
                 longestEffectiveRange:&range
                               inRange:full_range];
  NSFontManager* fontManager = [NSFontManager sharedFontManager];
  if (run_font && (mask == ([fontManager traitsOfFont:run_font] & mask))) {
    return true;
  }
  return false;
}

// AutocompleteMatch doesn't really have the right constructor for our
// needs.  Fake one for us to use.
AutocompleteMatch MakeMatch(const string16& contents,
                            const string16& description) {
  AutocompleteMatch m(NULL, 1, true, AutocompleteMatchType::URL_WHAT_YOU_TYPED);
  m.contents = contents;
  m.description = description;
  return m;
}

class MockOmniboxPopupViewMac : public OmniboxPopupViewMac {
 public:
  MockOmniboxPopupViewMac(OmniboxView* omnibox_view,
                          OmniboxEditModel* edit_model,
                          NSTextField* field)
      : OmniboxPopupViewMac(omnibox_view, edit_model, field) {
  }

  void SetResultCount(size_t count) {
    ACMatches matches;
    for (size_t i = 0; i < count; ++i)
      matches.push_back(AutocompleteMatch());
    result_.Reset();
    result_.AppendMatches(matches);
  }

 protected:
  virtual const AutocompleteResult& GetResult() const OVERRIDE {
    return result_;
  }

 private:
  AutocompleteResult result_;
};

class OmniboxPopupViewMacTest : public CocoaProfileTest {
 public:
  OmniboxPopupViewMacTest() {
    color_ = [NSColor blackColor];
    dim_color_ = [NSColor darkGrayColor];
    font_ = gfx::Font(
        base::SysNSStringToUTF8([[NSFont userFontOfSize:12] fontName]), 12);
  }

 protected:
  NSColor* color_;  // weak
  NSColor* dim_color_;  // weak
  gfx::Font font_;

 private:
  DISALLOW_COPY_AND_ASSIGN(OmniboxPopupViewMacTest);
};

// Simple inputs with no matches should result in styled output who's text
// matches the input string, with the passed-in color, and nothing bolded.
TEST_F(OmniboxPopupViewMacTest, DecorateMatchedStringNoMatch) {
  NSString* const string = @"This is a test";
  AutocompleteMatch::ACMatchClassifications classifications;

  NSAttributedString* decorated =
      OmniboxPopupViewMac::DecorateMatchedString(
          base::SysNSStringToUTF16(string), classifications,
          color_, dim_color_, font_);

  // Result has same characters as the input.
  EXPECT_EQ([decorated length], [string length]);
  EXPECT_TRUE([[decorated string] isEqualToString:string]);

  // Our passed-in color for the entire string.
  EXPECT_EQ(RunLengthForAttribute(decorated, 0U,
                                  NSForegroundColorAttributeName),
            [string length]);
  EXPECT_TRUE(RunHasColor(decorated, 0U, color_));

  // An unbolded font for the entire string.
  EXPECT_EQ(RunLengthForAttribute(decorated, 0U, NSFontAttributeName),
            [string length]);
  EXPECT_FALSE(RunHasFontTrait(decorated, 0U, NSBoldFontMask));
}

// Identical to DecorateMatchedStringNoMatch, except test that URL style gets a
// different color than we passed in.
TEST_F(OmniboxPopupViewMacTest, DecorateMatchedStringURLNoMatch) {
  NSString* const string = @"This is a test";
  AutocompleteMatch::ACMatchClassifications classifications;

  classifications.push_back(
      ACMatchClassification(0, ACMatchClassification::URL));

  NSAttributedString* decorated =
      OmniboxPopupViewMac::DecorateMatchedString(
          base::SysNSStringToUTF16(string), classifications,
          color_, dim_color_, font_);

  // Result has same characters as the input.
  EXPECT_EQ([decorated length], [string length]);
  EXPECT_TRUE([[decorated string] isEqualToString:string]);

  // One color for the entire string, and it's not the one we passed in.
  EXPECT_EQ(RunLengthForAttribute(decorated, 0U,
                                  NSForegroundColorAttributeName),
            [string length]);
  EXPECT_FALSE(RunHasColor(decorated, 0U, color_));

  // An unbolded font for the entire string.
  EXPECT_EQ(RunLengthForAttribute(decorated, 0U,
                                  NSFontAttributeName), [string length]);
  EXPECT_FALSE(RunHasFontTrait(decorated, 0U, NSBoldFontMask));
}

// Test that DIM works as expected.
TEST_F(OmniboxPopupViewMacTest, DecorateMatchedStringDimNoMatch) {
  NSString* const string = @"This is a test";
  // Dim "is".
  const NSUInteger run_length_1 = 5, run_length_2 = 2, run_length_3 = 7;
  // Make sure nobody messed up the inputs.
  EXPECT_EQ(run_length_1 + run_length_2 + run_length_3, [string length]);

  // Push each run onto classifications.
  AutocompleteMatch::ACMatchClassifications classifications;
  classifications.push_back(
      ACMatchClassification(0, ACMatchClassification::NONE));
  classifications.push_back(
      ACMatchClassification(run_length_1, ACMatchClassification::DIM));
  classifications.push_back(
      ACMatchClassification(run_length_1 + run_length_2,
                            ACMatchClassification::NONE));

  NSAttributedString* decorated =
      OmniboxPopupViewMac::DecorateMatchedString(
          base::SysNSStringToUTF16(string), classifications,
          color_, dim_color_, font_);

  // Result has same characters as the input.
  EXPECT_EQ([decorated length], [string length]);
  EXPECT_TRUE([[decorated string] isEqualToString:string]);

  // Should have three font runs, normal, dim, normal.
  EXPECT_EQ(RunLengthForAttribute(decorated, 0U,
                                  NSForegroundColorAttributeName),
            run_length_1);
  EXPECT_TRUE(RunHasColor(decorated, 0U, color_));

  EXPECT_EQ(RunLengthForAttribute(decorated, run_length_1,
                                  NSForegroundColorAttributeName),
            run_length_2);
  EXPECT_TRUE(RunHasColor(decorated, run_length_1, dim_color_));

  EXPECT_EQ(RunLengthForAttribute(decorated, run_length_1 + run_length_2,
                                  NSForegroundColorAttributeName),
            run_length_3);
  EXPECT_TRUE(RunHasColor(decorated, run_length_1 + run_length_2, color_));

  // An unbolded font for the entire string.
  EXPECT_EQ(RunLengthForAttribute(decorated, 0U,
                                  NSFontAttributeName), [string length]);
  EXPECT_FALSE(RunHasFontTrait(decorated, 0U, NSBoldFontMask));
}

// Test that the matched run gets bold-faced, but keeps the same color.
TEST_F(OmniboxPopupViewMacTest, DecorateMatchedStringMatch) {
  NSString* const string = @"This is a test";
  // Match "is".
  const NSUInteger run_length_1 = 5, run_length_2 = 2, run_length_3 = 7;
  // Make sure nobody messed up the inputs.
  EXPECT_EQ(run_length_1 + run_length_2 + run_length_3, [string length]);

  // Push each run onto classifications.
  AutocompleteMatch::ACMatchClassifications classifications;
  classifications.push_back(
      ACMatchClassification(0, ACMatchClassification::NONE));
  classifications.push_back(
      ACMatchClassification(run_length_1, ACMatchClassification::MATCH));
  classifications.push_back(
      ACMatchClassification(run_length_1 + run_length_2,
                            ACMatchClassification::NONE));

  NSAttributedString* decorated =
      OmniboxPopupViewMac::DecorateMatchedString(
          base::SysNSStringToUTF16(string), classifications,
          color_, dim_color_, font_);

  // Result has same characters as the input.
  EXPECT_EQ([decorated length], [string length]);
  EXPECT_TRUE([[decorated string] isEqualToString:string]);

  // Our passed-in color for the entire string.
  EXPECT_EQ(RunLengthForAttribute(decorated, 0U,
                                  NSForegroundColorAttributeName),
            [string length]);
  EXPECT_TRUE(RunHasColor(decorated, 0U, color_));

  // Should have three font runs, not bold, bold, then not bold again.
  EXPECT_EQ(RunLengthForAttribute(decorated, 0U,
                                  NSFontAttributeName), run_length_1);
  EXPECT_FALSE(RunHasFontTrait(decorated, 0U, NSBoldFontMask));

  EXPECT_EQ(RunLengthForAttribute(decorated, run_length_1,
                                  NSFontAttributeName), run_length_2);
  EXPECT_TRUE(RunHasFontTrait(decorated, run_length_1, NSBoldFontMask));

  EXPECT_EQ(RunLengthForAttribute(decorated, run_length_1 + run_length_2,
                                  NSFontAttributeName), run_length_3);
  EXPECT_FALSE(RunHasFontTrait(decorated, run_length_1 + run_length_2,
                               NSBoldFontMask));
}

// Just like DecorateMatchedStringURLMatch, this time with URL style.
TEST_F(OmniboxPopupViewMacTest, DecorateMatchedStringURLMatch) {
  NSString* const string = @"http://hello.world/";
  // Match "hello".
  const NSUInteger run_length_1 = 7, run_length_2 = 5, run_length_3 = 7;
  // Make sure nobody messed up the inputs.
  EXPECT_EQ(run_length_1 + run_length_2 + run_length_3, [string length]);

  // Push each run onto classifications.
  AutocompleteMatch::ACMatchClassifications classifications;
  classifications.push_back(
      ACMatchClassification(0, ACMatchClassification::URL));
  const int kURLMatch = ACMatchClassification::URL|ACMatchClassification::MATCH;
  classifications.push_back(ACMatchClassification(run_length_1, kURLMatch));
  classifications.push_back(
      ACMatchClassification(run_length_1 + run_length_2,
                            ACMatchClassification::URL));

  NSAttributedString* decorated =
      OmniboxPopupViewMac::DecorateMatchedString(
          base::SysNSStringToUTF16(string), classifications,
          color_, dim_color_, font_);

  // Result has same characters as the input.
  EXPECT_EQ([decorated length], [string length]);
  EXPECT_TRUE([[decorated string] isEqualToString:string]);

  // One color for the entire string, and it's not the one we passed in.
  EXPECT_EQ(RunLengthForAttribute(decorated, 0U,
                                  NSForegroundColorAttributeName),
            [string length]);
  EXPECT_FALSE(RunHasColor(decorated, 0U, color_));

  // Should have three font runs, not bold, bold, then not bold again.
  EXPECT_EQ(RunLengthForAttribute(decorated, 0U,
                                  NSFontAttributeName), run_length_1);
  EXPECT_FALSE(RunHasFontTrait(decorated, 0U, NSBoldFontMask));

  EXPECT_EQ(RunLengthForAttribute(decorated, run_length_1,
                                  NSFontAttributeName), run_length_2);
  EXPECT_TRUE(RunHasFontTrait(decorated, run_length_1, NSBoldFontMask));

  EXPECT_EQ(RunLengthForAttribute(decorated, run_length_1 + run_length_2,
                                  NSFontAttributeName), run_length_3);
  EXPECT_FALSE(RunHasFontTrait(decorated, run_length_1 + run_length_2,
                               NSBoldFontMask));
}

TEST_F(OmniboxPopupViewMacTest, UpdatePopupAppearance) {
  base::scoped_nsobject<NSTextField> field(
      [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 100, 20)]);
  [[test_window() contentView] addSubview:field];

  OmniboxViewMac view(NULL, NULL, profile(), NULL, NULL);
  MockOmniboxPopupViewMac popup_view(&view, view.model(), field);

  popup_view.UpdatePopupAppearance();
  EXPECT_FALSE(popup_view.IsOpen());
  EXPECT_EQ(0, [popup_view.matrix() numberOfRows]);

  popup_view.SetResultCount(3);
  popup_view.UpdatePopupAppearance();
  EXPECT_TRUE(popup_view.IsOpen());
  EXPECT_EQ(3, [popup_view.matrix() numberOfRows]);

  int old_height = popup_view.GetTargetBounds().height();
  popup_view.SetResultCount(5);
  popup_view.UpdatePopupAppearance();
  EXPECT_GT(popup_view.GetTargetBounds().height(), old_height);
  EXPECT_EQ(5, [popup_view.matrix() numberOfRows]);

  popup_view.SetResultCount(0);
  popup_view.UpdatePopupAppearance();
  EXPECT_FALSE(popup_view.IsOpen());
  EXPECT_EQ(0, [popup_view.matrix() numberOfRows]);
}

}  // namespace
