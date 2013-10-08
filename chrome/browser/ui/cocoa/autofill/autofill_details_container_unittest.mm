// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/autofill/autofill_details_container.h"

#include "base/mac/scoped_nsobject.h"
#include "base/strings/utf_string_conversions.h"
#import "chrome/browser/ui/cocoa/autofill/autofill_section_container.h"
#include "chrome/browser/ui/autofill/mock_autofill_dialog_view_delegate.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/platform_test.h"
#import "ui/base/test/ui_cocoa_test_helper.h"

namespace {

class AutofillDetailsContainerTest : public ui::CocoaTest {
 public:
  AutofillDetailsContainerTest() {
    container_.reset([[AutofillDetailsContainer alloc] initWithDelegate:
                         &delegate_]);
    [[test_window() contentView] addSubview:[container_ view]];
  }

 protected:
  base::scoped_nsobject<AutofillDetailsContainer> container_;
  testing::NiceMock<autofill::MockAutofillDialogViewDelegate> delegate_;
};

}  // namespace

TEST_VIEW(AutofillDetailsContainerTest, [container_ view])

TEST_F(AutofillDetailsContainerTest, BasicProperties) {
  EXPECT_TRUE([[container_ view] isKindOfClass:[NSScrollView class]]);
  EXPECT_GT([[[container_ view] subviews] count], 0U);

  [[container_ view] setFrameSize:[container_ preferredSize]];
  EXPECT_GT(NSHeight([[container_ view] frame]), 0);
  EXPECT_GT(NSWidth([[container_ view] frame]), 0);
}

TEST_F(AutofillDetailsContainerTest, ValidateAllSections) {
  using namespace autofill;
  using namespace testing;

  DetailOutputMap output;
  ValidityData validity;

  EXPECT_CALL(delegate_, InputsAreValid(_, _, VALIDATE_FINAL))
      .Times(4)
      .WillOnce(Return(validity))
      .WillOnce(Return(validity))
      .WillOnce(Return(validity))
      .WillOnce(Return(validity));

  EXPECT_TRUE([container_ validate]);

  ValidityData invalid;
  invalid[ADDRESS_HOME_ZIP] = ASCIIToUTF16("Some error message");

  EXPECT_CALL(delegate_, InputsAreValid(_, _, VALIDATE_FINAL))
      .Times(4)
      .WillOnce(Return(validity))
      .WillOnce(Return(validity))
      .WillOnce(Return(invalid))
      .WillOnce(Return(validity));

  EXPECT_FALSE([container_ validate]);
}
