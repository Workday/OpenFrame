// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/prefs/testing_pref_service.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/autofill/autofill_popup_controller_impl.h"
#include "chrome/browser/ui/autofill/autofill_popup_view.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "chrome/test/base/testing_profile.h"
#include "components/autofill/content/browser/autofill_driver_impl.h"
#include "components/autofill/core/browser/autofill_external_delegate.h"
#include "components/autofill/core/browser/autofill_manager.h"
#include "components/autofill/core/browser/test_autofill_external_delegate.h"
#include "components/autofill/core/browser/test_autofill_manager_delegate.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/WebKit/public/web/WebAutofillClient.h"
#include "ui/gfx/display.h"
#include "ui/gfx/rect.h"

using ::testing::_;
using ::testing::AtLeast;
using ::testing::NiceMock;
using base::WeakPtr;
using WebKit::WebAutofillClient;

namespace autofill {
namespace {

class MockAutofillExternalDelegate : public AutofillExternalDelegate {
 public:
  MockAutofillExternalDelegate(content::WebContents* web_contents,
                               AutofillManager* autofill_manager,
                               AutofillDriver* autofill_driver)
      : AutofillExternalDelegate(web_contents, autofill_manager,
                                 autofill_driver) {}
  virtual ~MockAutofillExternalDelegate() {}

  virtual void DidSelectSuggestion(int identifier) OVERRIDE {}
  virtual void RemoveSuggestion(const string16& value, int identifier) OVERRIDE
      {}
  virtual void ClearPreviewedForm() OVERRIDE {}
  base::WeakPtr<AutofillExternalDelegate> GetWeakPtr() {
    return AutofillExternalDelegate::GetWeakPtr();
  }
};

class MockAutofillManagerDelegate
    : public autofill::TestAutofillManagerDelegate {
 public:
  MockAutofillManagerDelegate() {}
  virtual ~MockAutofillManagerDelegate() {}

  virtual PrefService* GetPrefs() OVERRIDE { return &prefs_; }

 private:
  TestingPrefServiceSimple prefs_;

  DISALLOW_COPY_AND_ASSIGN(MockAutofillManagerDelegate);
};

class TestAutofillPopupController : public AutofillPopupControllerImpl {
 public:
  explicit TestAutofillPopupController(
      base::WeakPtr<AutofillExternalDelegate> external_delegate,
      const gfx::RectF& element_bounds)
      : AutofillPopupControllerImpl(
            external_delegate, NULL, element_bounds,
            base::i18n::UNKNOWN_DIRECTION) {}
  virtual ~TestAutofillPopupController() {}

  void set_display(const gfx::Display display) {
    display_ = display;
  }
  virtual gfx::Display GetDisplayNearestPoint(const gfx::Point& point) const
      OVERRIDE {
    return display_;
  }

  // Making protected functions public for testing
  void SetPopupBounds(const gfx::Rect& bounds) {
    AutofillPopupControllerImpl::SetPopupBounds(bounds);
  }
  const std::vector<string16>& names() const {
    return AutofillPopupControllerImpl::names();
  }
  const std::vector<string16>& subtexts() const {
    return AutofillPopupControllerImpl::subtexts();
  }
  const std::vector<int>& identifiers() const {
    return AutofillPopupControllerImpl::identifiers();
  }
  int selected_line() const {
    return AutofillPopupControllerImpl::selected_line();
  }
  void SetSelectedLine(size_t selected_line) {
    AutofillPopupControllerImpl::SetSelectedLine(selected_line);
  }
  void SelectNextLine() {
    AutofillPopupControllerImpl::SelectNextLine();
  }
  void SelectPreviousLine() {
    AutofillPopupControllerImpl::SelectPreviousLine();
  }
  bool RemoveSelectedLine() {
    return AutofillPopupControllerImpl::RemoveSelectedLine();
  }
  void DoHide() {
    AutofillPopupControllerImpl::Hide();
  }
  const gfx::Rect& popup_bounds() const {
    return AutofillPopupControllerImpl::popup_bounds();
  }
  const gfx::RectF& element_bounds() const {
    return AutofillPopupControllerImpl::element_bounds();
  }
#if !defined(OS_ANDROID)
  const gfx::Font& GetNameFontForRow(size_t index) const {
    return AutofillPopupControllerImpl::GetNameFontForRow(index);
  }
  const gfx::Font& subtext_font() const {
    return AutofillPopupControllerImpl::subtext_font();
  }
  int RowWidthWithoutText(int row) const {
    return AutofillPopupControllerImpl::RowWidthWithoutText(row);
  }
#endif
  using AutofillPopupControllerImpl::SetValues;
  int GetDesiredPopupWidth() const {
    return AutofillPopupControllerImpl::GetDesiredPopupWidth();
  }
  int GetDesiredPopupHeight() const {
    return AutofillPopupControllerImpl::GetDesiredPopupHeight();
  }

  WeakPtr<AutofillPopupControllerImpl> GetWeakPtr() {
    return AutofillPopupControllerImpl::GetWeakPtr();
  }

  MOCK_METHOD1(InvalidateRow, void(size_t));
  MOCK_METHOD0(UpdateBoundsAndRedrawPopup, void());
  MOCK_METHOD0(Hide, void());

 private:
  virtual void ShowView() OVERRIDE {}

  gfx::Display display_;
};

}  // namespace

class AutofillPopupControllerUnitTest : public ChromeRenderViewHostTestHarness {
 public:
  AutofillPopupControllerUnitTest()
      : manager_delegate_(new MockAutofillManagerDelegate()),
        autofill_popup_controller_(NULL) {}
  virtual ~AutofillPopupControllerUnitTest() {}

  virtual void SetUp() OVERRIDE {
    ChromeRenderViewHostTestHarness::SetUp();

    AutofillDriverImpl::CreateForWebContentsAndDelegate(
        web_contents(),
        manager_delegate_.get(),
        "en-US",
        AutofillManager::ENABLE_AUTOFILL_DOWNLOAD_MANAGER);
    AutofillDriverImpl* driver =
        AutofillDriverImpl::FromWebContents(web_contents());
    external_delegate_.reset(
        new NiceMock<MockAutofillExternalDelegate>(
            web_contents(),
            driver->autofill_manager(),
            driver));

    autofill_popup_controller_ =
        new testing::NiceMock<TestAutofillPopupController>(
            external_delegate_->GetWeakPtr(), gfx::Rect());
  }

  virtual void TearDown() OVERRIDE {
    // This will make sure the controller and the view (if any) are both
    // cleaned up.
    if (autofill_popup_controller_)
      autofill_popup_controller_->DoHide();

    external_delegate_.reset();
    ChromeRenderViewHostTestHarness::TearDown();
  }

  TestAutofillPopupController* popup_controller() {
    return autofill_popup_controller_;
  }

  MockAutofillExternalDelegate* delegate() {
    return external_delegate_.get();
  }

 protected:
  scoped_ptr<MockAutofillManagerDelegate> manager_delegate_;
  scoped_ptr<NiceMock<MockAutofillExternalDelegate> > external_delegate_;
  testing::NiceMock<TestAutofillPopupController>* autofill_popup_controller_;
};

TEST_F(AutofillPopupControllerUnitTest, SetBounds) {
  // Ensure the popup size can be set and causes a redraw.
  gfx::Rect popup_bounds(10, 10, 100, 100);

  EXPECT_CALL(*autofill_popup_controller_,
              UpdateBoundsAndRedrawPopup());

  popup_controller()->SetPopupBounds(popup_bounds);

  EXPECT_EQ(popup_bounds, popup_controller()->popup_bounds());
}

TEST_F(AutofillPopupControllerUnitTest, ChangeSelectedLine) {
  // Set up the popup.
  std::vector<string16> names(2, string16());
  std::vector<int> autofill_ids(2, 0);
  autofill_popup_controller_->Show(names, names, names, autofill_ids);

  EXPECT_LT(autofill_popup_controller_->selected_line(), 0);
  // Check that there are at least 2 values so that the first and last selection
  // are different.
  EXPECT_GE(2,
      static_cast<int>(autofill_popup_controller_->subtexts().size()));

  // Test wrapping before the front.
  autofill_popup_controller_->SelectPreviousLine();
  EXPECT_EQ(static_cast<int>(
      autofill_popup_controller_->subtexts().size() - 1),
      autofill_popup_controller_->selected_line());

  // Test wrapping after the end.
  autofill_popup_controller_->SelectNextLine();
  EXPECT_EQ(0, autofill_popup_controller_->selected_line());
}

TEST_F(AutofillPopupControllerUnitTest, RedrawSelectedLine) {
  // Set up the popup.
  std::vector<string16> names(2, string16());
  std::vector<int> autofill_ids(2, 0);
  autofill_popup_controller_->Show(names, names, names, autofill_ids);

  // Make sure that when a new line is selected, it is invalidated so it can
  // be updated to show it is selected.
  int selected_line = 0;
  EXPECT_CALL(*autofill_popup_controller_, InvalidateRow(selected_line));
  autofill_popup_controller_->SetSelectedLine(selected_line);

  // Ensure that the row isn't invalidated if it didn't change.
  EXPECT_CALL(*autofill_popup_controller_,
              InvalidateRow(selected_line)).Times(0);
  autofill_popup_controller_->SetSelectedLine(selected_line);

  // Change back to no selection.
  EXPECT_CALL(*autofill_popup_controller_, InvalidateRow(selected_line));
  autofill_popup_controller_->SetSelectedLine(-1);
}

TEST_F(AutofillPopupControllerUnitTest, RemoveLine) {
  // Set up the popup.
  std::vector<string16> names(3, string16());
  std::vector<int> autofill_ids;
  autofill_ids.push_back(1);
  autofill_ids.push_back(1);
  autofill_ids.push_back(WebAutofillClient::MenuItemIDAutofillOptions);
  autofill_popup_controller_->Show(names, names, names, autofill_ids);

  // Generate a popup, so it can be hidden later. It doesn't matter what the
  // external_delegate thinks is being shown in the process, since we are just
  // testing the popup here.
  autofill::GenerateTestAutofillPopup(external_delegate_.get());

  // No line is selected so the removal should fail.
  EXPECT_FALSE(autofill_popup_controller_->RemoveSelectedLine());

  // Try to remove the last entry and ensure it fails (it is an option).
  autofill_popup_controller_->SetSelectedLine(
      autofill_popup_controller_->subtexts().size() - 1);
  EXPECT_FALSE(autofill_popup_controller_->RemoveSelectedLine());
  EXPECT_LE(0, autofill_popup_controller_->selected_line());

  // Remove the first entry. The popup should be redrawn since its size has
  // changed.
  EXPECT_CALL(*autofill_popup_controller_, UpdateBoundsAndRedrawPopup());
  autofill_popup_controller_->SetSelectedLine(0);
  EXPECT_TRUE(autofill_popup_controller_->RemoveSelectedLine());

  // Remove the last entry. The popup should then be hidden since there are
  // no Autofill entries left.
  EXPECT_CALL(*autofill_popup_controller_, Hide());
  autofill_popup_controller_->SetSelectedLine(0);
  EXPECT_TRUE(autofill_popup_controller_->RemoveSelectedLine());
}

TEST_F(AutofillPopupControllerUnitTest, RemoveOnlyLine) {
  // Set up the popup.
  std::vector<string16> names(1, string16());
  std::vector<int> autofill_ids;
  autofill_ids.push_back(1);
  autofill_popup_controller_->Show(names, names, names, autofill_ids);

  // Generate a popup.
  autofill::GenerateTestAutofillPopup(external_delegate_.get());

  // Select the only line.
  autofill_popup_controller_->SetSelectedLine(0);

  // Remove the only line. There should be no row invalidation and the popup
  // should then be hidden since there are no Autofill entries left.
  EXPECT_CALL(*autofill_popup_controller_, Hide());
  EXPECT_CALL(*autofill_popup_controller_, InvalidateRow(_)).Times(0);
  EXPECT_TRUE(autofill_popup_controller_->RemoveSelectedLine());
}

TEST_F(AutofillPopupControllerUnitTest, SkipSeparator) {
  // Set up the popup.
  std::vector<string16> names(3, string16());
  std::vector<int> autofill_ids;
  autofill_ids.push_back(1);
  autofill_ids.push_back(WebAutofillClient::MenuItemIDSeparator);
  autofill_ids.push_back(WebAutofillClient::MenuItemIDAutofillOptions);
  autofill_popup_controller_->Show(names, names, names, autofill_ids);

  autofill_popup_controller_->SetSelectedLine(0);

  // Make sure next skips the unselectable separator.
  autofill_popup_controller_->SelectNextLine();
  EXPECT_EQ(2, autofill_popup_controller_->selected_line());

  // Make sure previous skips the unselectable separator.
  autofill_popup_controller_->SelectPreviousLine();
  EXPECT_EQ(0, autofill_popup_controller_->selected_line());
}

TEST_F(AutofillPopupControllerUnitTest, RowWidthWithoutText) {
  std::vector<string16> names(4);
  std::vector<string16> subtexts(4);
  std::vector<string16> icons(4);
  std::vector<int> ids(4);

  // Set up some visible display so the text values are kept.
  gfx::Display display(0, gfx::Rect(0, 0, 100, 100));
  autofill_popup_controller_->set_display(display);

  // Give elements 1 and 3 subtexts and elements 2 and 3 icons, to ensure
  // all combinations of subtexts and icons.
  subtexts[1] = ASCIIToUTF16("x");
  subtexts[3] = ASCIIToUTF16("x");
  icons[2] = ASCIIToUTF16("x");
  icons[3] = ASCIIToUTF16("x");
  autofill_popup_controller_->Show(names, subtexts, icons, ids);

  int base_size = AutofillPopupView::kEndPadding * 2;
  int subtext_increase = AutofillPopupView::kNamePadding;
  int icon_increase = AutofillPopupView::kIconPadding +
      AutofillPopupView::kAutofillIconWidth;

  EXPECT_EQ(base_size, autofill_popup_controller_->RowWidthWithoutText(0));
  EXPECT_EQ(base_size + subtext_increase,
            autofill_popup_controller_->RowWidthWithoutText(1));
  EXPECT_EQ(base_size + icon_increase,
            autofill_popup_controller_->RowWidthWithoutText(2));
  EXPECT_EQ(base_size + subtext_increase + icon_increase,
            autofill_popup_controller_->RowWidthWithoutText(3));
}

TEST_F(AutofillPopupControllerUnitTest, UpdateDataListValues) {
  std::vector<string16> items;
  items.push_back(string16());
  std::vector<int> ids;
  ids.push_back(1);

  autofill_popup_controller_->Show(items, items, items, ids);

  EXPECT_EQ(items, autofill_popup_controller_->names());
  EXPECT_EQ(ids, autofill_popup_controller_->identifiers());

  // Add one data list entry.
  std::vector<string16> data_list_values;
  data_list_values.push_back(ASCIIToUTF16("data list value 1"));

  autofill_popup_controller_->UpdateDataListValues(data_list_values,
                                                   data_list_values);

  // Update the expected values.
  items.insert(items.begin(), data_list_values[0]);
  items.insert(items.begin() + 1, string16());
  ids.insert(ids.begin(), WebAutofillClient::MenuItemIDDataListEntry);
  ids.insert(ids.begin() + 1, WebAutofillClient::MenuItemIDSeparator);

  EXPECT_EQ(items, autofill_popup_controller_->names());
  EXPECT_EQ(ids, autofill_popup_controller_->identifiers());

  // Add two data list entries (which should replace the current one).
  data_list_values.push_back(ASCIIToUTF16("data list value 2"));

  autofill_popup_controller_->UpdateDataListValues(data_list_values,
                                                   data_list_values);

  // Update the expected values.
  items.insert(items.begin() + 1, data_list_values[1]);
  ids.insert(ids.begin(), WebAutofillClient::MenuItemIDDataListEntry);

  EXPECT_EQ(items, autofill_popup_controller_->names());
  EXPECT_EQ(ids, autofill_popup_controller_->identifiers());

  // Clear all data list values.
  data_list_values.clear();

  autofill_popup_controller_->UpdateDataListValues(data_list_values,
                                                   data_list_values);

  items.clear();
  items.push_back(string16());
  ids.clear();
  ids.push_back(1);

  EXPECT_EQ(items, autofill_popup_controller_->names());
  EXPECT_EQ(ids, autofill_popup_controller_->identifiers());
}

TEST_F(AutofillPopupControllerUnitTest, PopupsWithOnlyDataLists) {
  // Create the popup with a single datalist element.
  std::vector<string16> items;
  items.push_back(string16());
  std::vector<int> ids;
  ids.push_back(WebAutofillClient::MenuItemIDDataListEntry);

  autofill_popup_controller_->Show(items, items, items, ids);

  EXPECT_EQ(items, autofill_popup_controller_->names());
  EXPECT_EQ(ids, autofill_popup_controller_->identifiers());

  // Replace the datalist element with a new one.
  std::vector<string16> data_list_values;
  data_list_values.push_back(ASCIIToUTF16("data list value 1"));

  autofill_popup_controller_->UpdateDataListValues(data_list_values,
                                                   data_list_values);

  EXPECT_EQ(data_list_values, autofill_popup_controller_->names());
  // The id value should stay the same.
  EXPECT_EQ(ids, autofill_popup_controller_->identifiers());

  // Clear datalist values and check that the popup becomes hidden.
  EXPECT_CALL(*autofill_popup_controller_, Hide());
  data_list_values.clear();
  autofill_popup_controller_->UpdateDataListValues(data_list_values,
                                                   data_list_values);
}

TEST_F(AutofillPopupControllerUnitTest, GetOrCreate) {
  AutofillDriverImpl* driver =
      AutofillDriverImpl::FromWebContents(web_contents());
  MockAutofillExternalDelegate delegate(
      web_contents(), driver->autofill_manager(), driver);

  WeakPtr<AutofillPopupControllerImpl> controller =
      AutofillPopupControllerImpl::GetOrCreate(
          WeakPtr<AutofillPopupControllerImpl>(), delegate.GetWeakPtr(), NULL,
          gfx::Rect(), base::i18n::UNKNOWN_DIRECTION);
  EXPECT_TRUE(controller.get());

  controller->Hide();

  controller = AutofillPopupControllerImpl::GetOrCreate(
      WeakPtr<AutofillPopupControllerImpl>(), delegate.GetWeakPtr(), NULL,
      gfx::Rect(), base::i18n::UNKNOWN_DIRECTION);
  EXPECT_TRUE(controller.get());

  WeakPtr<AutofillPopupControllerImpl> controller2 =
      AutofillPopupControllerImpl::GetOrCreate(controller,
                                               delegate.GetWeakPtr(),
                                               NULL,
                                               gfx::Rect(),
                                               base::i18n::UNKNOWN_DIRECTION);
  EXPECT_EQ(controller.get(), controller2.get());
  controller->Hide();

  testing::NiceMock<TestAutofillPopupController>* test_controller =
      new testing::NiceMock<TestAutofillPopupController>(delegate.GetWeakPtr(),
                                                         gfx::Rect());
  EXPECT_CALL(*test_controller, Hide());

  gfx::RectF bounds(0.f, 0.f, 1.f, 2.f);
  base::WeakPtr<AutofillPopupControllerImpl> controller3 =
      AutofillPopupControllerImpl::GetOrCreate(
          test_controller->GetWeakPtr(),
          delegate.GetWeakPtr(),
          NULL,
          bounds,
          base::i18n::UNKNOWN_DIRECTION);
  EXPECT_EQ(
      bounds,
      static_cast<AutofillPopupController*>(controller3.get())->
          element_bounds());
  controller3->Hide();

  // Hide the test_controller to delete it.
  test_controller->DoHide();
}

TEST_F(AutofillPopupControllerUnitTest, ProperlyResetController) {
  std::vector<string16> names(2);
  std::vector<int> ids(2);
  popup_controller()->SetValues(names, names, names, ids);
  popup_controller()->SetSelectedLine(0);

  // Now show a new popup with the same controller, but with fewer items.
  WeakPtr<AutofillPopupControllerImpl> controller =
      AutofillPopupControllerImpl::GetOrCreate(
          popup_controller()->GetWeakPtr(),
          delegate()->GetWeakPtr(),
          NULL,
          gfx::Rect(),
          base::i18n::UNKNOWN_DIRECTION);
  EXPECT_NE(0, controller->selected_line());
  EXPECT_TRUE(controller->names().empty());
}

#if !defined(OS_ANDROID)
TEST_F(AutofillPopupControllerUnitTest, ElideText) {
  std::vector<string16> names;
  names.push_back(ASCIIToUTF16("Text that will need to be trimmed"));
  names.push_back(ASCIIToUTF16("Untrimmed"));

  std::vector<string16> subtexts;
  subtexts.push_back(ASCIIToUTF16("Label that will be trimmed"));
  subtexts.push_back(ASCIIToUTF16("Untrimmed"));

  std::vector<string16> icons(2, string16());
  std::vector<int> autofill_ids(2, 0);

  // Show the popup once so we can easily generate the size it needs.
  autofill_popup_controller_->Show(names, subtexts, icons, autofill_ids);

  // Ensure the popup will be too small to display all of the first row.
  int popup_max_width =
      autofill_popup_controller_->GetNameFontForRow(0).GetStringWidth(
          names[0]) +
      autofill_popup_controller_->subtext_font().GetStringWidth(subtexts[0]) -
      25;
  gfx::Rect popup_bounds = gfx::Rect(0, 0, popup_max_width, 0);
  autofill_popup_controller_->set_display(gfx::Display(0, popup_bounds));

  autofill_popup_controller_->Show(names, subtexts, icons, autofill_ids);

  // The first element was long so it should have been trimmed.
  EXPECT_NE(names[0], autofill_popup_controller_->names()[0]);
  EXPECT_NE(subtexts[0], autofill_popup_controller_->subtexts()[0]);

  // The second element was shorter so it should be unchanged.
  EXPECT_EQ(names[1], autofill_popup_controller_->names()[1]);
  EXPECT_EQ(subtexts[1], autofill_popup_controller_->subtexts()[1]);
}
#endif

TEST_F(AutofillPopupControllerUnitTest, GrowPopupInSpace) {
  std::vector<string16> names(1);
  std::vector<int> autofill_ids(1, 1);

  // Call Show so that GetDesired...() will be able to provide valid values.
  autofill_popup_controller_->Show(names, names, names, autofill_ids);
  int desired_width = autofill_popup_controller_->GetDesiredPopupWidth();
  int desired_height = autofill_popup_controller_->GetDesiredPopupHeight();

  // Set up the visible screen space.
  gfx::Display display(0,
                       gfx::Rect(0, 0, desired_width * 2, desired_height * 2));

  // Store the possible element bounds and the popup bounds they should result
  // in.
  std::vector<gfx::RectF> element_bounds;
  std::vector<gfx::Rect> expected_popup_bounds;

  // The popup grows down and to the right.
  element_bounds.push_back(gfx::RectF(0, 0, 0, 0));
  expected_popup_bounds.push_back(
      gfx::Rect(0, 0, desired_width, desired_height));

  // The popup grows down and to the left.
  element_bounds.push_back(gfx::RectF(2 * desired_width, 0, 0, 0));
  expected_popup_bounds.push_back(
      gfx::Rect(desired_width, 0, desired_width, desired_height));

  // The popup grows up and to the right.
  element_bounds.push_back(gfx::RectF(0, 2 * desired_height, 0, 0));
  expected_popup_bounds.push_back(
      gfx::Rect(0, desired_height, desired_width, desired_height));

  // The popup grows up and to the left.
  element_bounds.push_back(
      gfx::RectF(2 * desired_width, 2 * desired_height, 0, 0));
  expected_popup_bounds.push_back(
      gfx::Rect(desired_width, desired_height, desired_width, desired_height));

  // The popup would be partial off the top and left side of the screen.
  element_bounds.push_back(
      gfx::RectF(-desired_width / 2, -desired_height / 2, 0, 0));
  expected_popup_bounds.push_back(
      gfx::Rect(0, 0, desired_width, desired_height));

  // The popup would be partially off the bottom and the right side of
  // the screen.
  element_bounds.push_back(
      gfx::RectF(desired_width * 1.5, desired_height * 1.5, 0, 0));
  expected_popup_bounds.push_back(
      gfx::Rect((desired_width + 1) / 2, (desired_height + 1) / 2,
                desired_width, desired_height));

  for (size_t i = 0; i < element_bounds.size(); ++i) {
    AutofillDriverImpl* driver =
        AutofillDriverImpl::FromWebContents(web_contents());
    NiceMock<MockAutofillExternalDelegate> external_delegate(
        web_contents(), driver->autofill_manager(), driver);
    TestAutofillPopupController* autofill_popup_controller =
        new TestAutofillPopupController(external_delegate.GetWeakPtr(),
                                        element_bounds[i]);

    autofill_popup_controller->set_display(display);
    autofill_popup_controller->Show(names, names, names, autofill_ids);

    EXPECT_EQ(expected_popup_bounds[i].ToString(),
              autofill_popup_controller->popup_bounds().ToString()) <<
        "Popup bounds failed to match for test " << i;

    // Hide the controller to delete it.
    autofill_popup_controller->DoHide();
  }
}

}  // namespace autofill
