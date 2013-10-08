// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/input_method/candidate_window_view.h"

#include <string>

#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/chromeos/input_method/candidate_view.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/widget/widget.h"

namespace chromeos {
namespace input_method {

namespace {
const char* kSampleCandidate[] = {
  "Sample Candidate 1",
  "Sample Candidate 2",
  "Sample Candidate 3"
};
const char* kSampleAnnotation[] = {
  "Sample Annotation 1",
  "Sample Annotation 2",
  "Sample Annotation 3"
};
const char* kSampleDescriptionTitle[] = {
  "Sample Description Title 1",
  "Sample Description Title 2",
  "Sample Description Title 3",
};
const char* kSampleDescriptionBody[] = {
  "Sample Description Body 1",
  "Sample Description Body 2",
  "Sample Description Body 3",
};

void InitIBusLookupTable(size_t page_size,
                         IBusLookupTable* table) {
  table->set_cursor_position(0);
  table->set_page_size(page_size);
  table->mutable_candidates()->clear();
  table->set_orientation(IBusLookupTable::VERTICAL);
}

void InitIBusLookupTableWithCandidatesFilled(size_t page_size,
                                             IBusLookupTable* table) {
  InitIBusLookupTable(page_size, table);
  for (size_t i = 0; i < page_size; ++i) {
    IBusLookupTable::Entry entry;
    entry.value = base::StringPrintf("value %lld",
                                     static_cast<unsigned long long>(i));
    entry.label = base::StringPrintf("%lld",
                                     static_cast<unsigned long long>(i));
    table->mutable_candidates()->push_back(entry);
  }
}

}  // namespace

class CandidateWindowViewTest : public views::ViewsTestBase {
 protected:
  void ExpectLabels(const std::string& shortcut,
                    const std::string& candidate,
                    const std::string& annotation,
                    const CandidateView* row) {
    EXPECT_EQ(shortcut, UTF16ToUTF8(row->shortcut_label_->text()));
    EXPECT_EQ(candidate, UTF16ToUTF8(row->candidate_label_->text()));
    EXPECT_EQ(annotation, UTF16ToUTF8(row->annotation_label_->text()));
  }
};

TEST_F(CandidateWindowViewTest, UpdateCandidatesTest_CursorVisibility) {
  views::Widget* widget = new views::Widget;
  views::Widget::InitParams params =
      CreateParams(views::Widget::InitParams::TYPE_WINDOW);
  widget->Init(params);

  CandidateWindowView candidate_window_view(widget);
  candidate_window_view.Init();

  // Visible (by default) cursor.
  IBusLookupTable table;
  const int table_size = 9;
  InitIBusLookupTableWithCandidatesFilled(table_size, &table);
  candidate_window_view.UpdateCandidates(table);
  EXPECT_EQ(0, candidate_window_view.selected_candidate_index_in_page_);

  // Invisible cursor.
  table.set_is_cursor_visible(false);
  candidate_window_view.UpdateCandidates(table);
  EXPECT_EQ(-1, candidate_window_view.selected_candidate_index_in_page_);

  // Move the cursor to the end.
  table.set_cursor_position(table_size - 1);
  candidate_window_view.UpdateCandidates(table);
  EXPECT_EQ(-1, candidate_window_view.selected_candidate_index_in_page_);

  // Change the cursor to visible.  The cursor must be at the end.
  table.set_is_cursor_visible(true);
  candidate_window_view.UpdateCandidates(table);
  EXPECT_EQ(table_size - 1,
            candidate_window_view.selected_candidate_index_in_page_);
}

TEST_F(CandidateWindowViewTest, SelectCandidateAtTest) {
  views::Widget* widget = new views::Widget;
  views::Widget::InitParams params =
      CreateParams(views::Widget::InitParams::TYPE_WINDOW);
  widget->Init(params);

  CandidateWindowView candidate_window_view(widget);
  candidate_window_view.Init();

  // Set 9 candidates.
  IBusLookupTable table_large;
  const int table_large_size = 9;
  InitIBusLookupTableWithCandidatesFilled(table_large_size, &table_large);
  table_large.set_cursor_position(table_large_size - 1);
  candidate_window_view.UpdateCandidates(table_large);
  // Select the last candidate.
  candidate_window_view.SelectCandidateAt(table_large_size - 1);

  // Reduce the number of candidates to 3.
  IBusLookupTable table_small;
  const int table_small_size = 3;
  InitIBusLookupTableWithCandidatesFilled(table_small_size, &table_small);
  table_small.set_cursor_position(table_small_size - 1);
  // Make sure the test doesn't crash if the candidate table reduced its size.
  // (crbug.com/174163)
  candidate_window_view.UpdateCandidates(table_small);
  candidate_window_view.SelectCandidateAt(table_small_size - 1);
}

TEST_F(CandidateWindowViewTest, ShortcutSettingTest) {
  const char* kEmptyLabel = "";
  const char* kCustomizedLabel[] = { "a", "s", "d" };
  const char* kExpectedHorizontalCustomizedLabel[] = { "a.", "s.", "d." };

  views::Widget* widget = new views::Widget;
  views::Widget::InitParams params =
      CreateParams(views::Widget::InitParams::TYPE_WINDOW);
  widget->Init(params);

  CandidateWindowView candidate_window_view(widget);
  candidate_window_view.Init();

  {
    SCOPED_TRACE("candidate_views allocation test");
    const size_t kMaxPageSize = 16;
    for (size_t i = 1; i < kMaxPageSize; ++i) {
      IBusLookupTable table;
      InitIBusLookupTable(i, &table);
      candidate_window_view.UpdateCandidates(table);
      EXPECT_EQ(i, candidate_window_view.candidate_views_.size());
    }
  }
  {
    SCOPED_TRACE("Empty string for each labels expects empty labels(vertical)");
    const size_t kPageSize = 3;
    IBusLookupTable table;
    InitIBusLookupTable(kPageSize, &table);

    table.set_orientation(IBusLookupTable::VERTICAL);
    for (size_t i = 0; i < kPageSize; ++i) {
      IBusLookupTable::Entry entry;
      entry.value = kSampleCandidate[i];
      entry.annotation = kSampleAnnotation[i];
      entry.description_title = kSampleDescriptionTitle[i];
      entry.description_body = kSampleDescriptionBody[i];
      entry.label = kEmptyLabel;
      table.mutable_candidates()->push_back(entry);
    }

    candidate_window_view.UpdateCandidates(table);

    ASSERT_EQ(kPageSize, candidate_window_view.candidate_views_.size());
    for (size_t i = 0; i < kPageSize; ++i) {
      ExpectLabels(kEmptyLabel, kSampleCandidate[i], kSampleAnnotation[i],
                   candidate_window_view.candidate_views_[i]);
    }
  }
  {
    SCOPED_TRACE(
        "Empty string for each labels expect empty labels(horizontal)");
    const size_t kPageSize = 3;
    IBusLookupTable table;
    InitIBusLookupTable(kPageSize, &table);

    table.set_orientation(IBusLookupTable::HORIZONTAL);
    for (size_t i = 0; i < kPageSize; ++i) {
      IBusLookupTable::Entry entry;
      entry.value = kSampleCandidate[i];
      entry.annotation = kSampleAnnotation[i];
      entry.description_title = kSampleDescriptionTitle[i];
      entry.description_body = kSampleDescriptionBody[i];
      entry.label = kEmptyLabel;
      table.mutable_candidates()->push_back(entry);
    }

    candidate_window_view.UpdateCandidates(table);

    ASSERT_EQ(kPageSize, candidate_window_view.candidate_views_.size());
    // Confirm actual labels not containing ".".
    for (size_t i = 0; i < kPageSize; ++i) {
      ExpectLabels(kEmptyLabel, kSampleCandidate[i], kSampleAnnotation[i],
                   candidate_window_view.candidate_views_[i]);
    }
  }
  {
    SCOPED_TRACE("Vertical customized label case");
    const size_t kPageSize = 3;
    IBusLookupTable table;
    InitIBusLookupTable(kPageSize, &table);

    table.set_orientation(IBusLookupTable::VERTICAL);
    for (size_t i = 0; i < kPageSize; ++i) {
      IBusLookupTable::Entry entry;
      entry.value = kSampleCandidate[i];
      entry.annotation = kSampleAnnotation[i];
      entry.description_title = kSampleDescriptionTitle[i];
      entry.description_body = kSampleDescriptionBody[i];
      entry.label = kCustomizedLabel[i];
      table.mutable_candidates()->push_back(entry);
    }

    candidate_window_view.UpdateCandidates(table);

    ASSERT_EQ(kPageSize, candidate_window_view.candidate_views_.size());
    // Confirm actual labels not containing ".".
    for (size_t i = 0; i < kPageSize; ++i) {
      ExpectLabels(kCustomizedLabel[i],
                   kSampleCandidate[i],
                   kSampleAnnotation[i],
                   candidate_window_view.candidate_views_[i]);
    }
  }
  {
    SCOPED_TRACE("Horizontal customized label case");
    const size_t kPageSize = 3;
    IBusLookupTable table;
    InitIBusLookupTable(kPageSize, &table);

    table.set_orientation(IBusLookupTable::HORIZONTAL);
    for (size_t i = 0; i < kPageSize; ++i) {
      IBusLookupTable::Entry entry;
      entry.value = kSampleCandidate[i];
      entry.annotation = kSampleAnnotation[i];
      entry.description_title = kSampleDescriptionTitle[i];
      entry.description_body = kSampleDescriptionBody[i];
      entry.label = kCustomizedLabel[i];
      table.mutable_candidates()->push_back(entry);
    }

    candidate_window_view.UpdateCandidates(table);

    ASSERT_EQ(kPageSize, candidate_window_view.candidate_views_.size());
    // Confirm actual labels not containing ".".
    for (size_t i = 0; i < kPageSize; ++i) {
      ExpectLabels(kExpectedHorizontalCustomizedLabel[i],
                   kSampleCandidate[i],
                   kSampleAnnotation[i],
                   candidate_window_view.candidate_views_[i]);
    }
  }

  // We should call CloseNow method, otherwise this test will leak memory.
  widget->CloseNow();
}

TEST_F(CandidateWindowViewTest, DoNotChangeRowHeightWithLabelSwitchTest) {
  const size_t kPageSize = 10;
  IBusLookupTable table;
  IBusLookupTable no_shortcut_table;

  const char kSampleCandidate1[] = "Sample String 1";
  const char kSampleCandidate2[] = "\xE3\x81\x82";  // multi byte string.
  const char kSampleCandidate3[] = ".....";

  const char kSampleShortcut1[] = "1";
  const char kSampleShortcut2[] = "b";
  const char kSampleShortcut3[] = "C";

  const char kSampleAnnotation1[] = "Sample Annotation 1";
  const char kSampleAnnotation2[] = "\xE3\x81\x82";  // multi byte string.
  const char kSampleAnnotation3[] = "......";

  // For testing, we have to prepare empty widget.
  // We should NOT manually free widget by default, otherwise double free will
  // be occurred. So, we should instantiate widget class with "new" operation.
  views::Widget* widget = new views::Widget;
  views::Widget::InitParams params =
      CreateParams(views::Widget::InitParams::TYPE_WINDOW);
  widget->Init(params);

  CandidateWindowView candidate_window_view(widget);
  candidate_window_view.Init();

  // Create LookupTable object.
  InitIBusLookupTable(kPageSize, &table);

  table.set_cursor_position(0);
  table.set_page_size(3);
  table.mutable_candidates()->clear();
  table.set_orientation(IBusLookupTable::VERTICAL);
  no_shortcut_table.CopyFrom(table);

  IBusLookupTable::Entry entry;
  entry.value = kSampleCandidate1;
  entry.annotation = kSampleAnnotation1;
  table.mutable_candidates()->push_back(entry);
  entry.label = kSampleShortcut1;
  no_shortcut_table.mutable_candidates()->push_back(entry);

  entry.value = kSampleCandidate2;
  entry.annotation = kSampleAnnotation2;
  table.mutable_candidates()->push_back(entry);
  entry.label = kSampleShortcut2;
  no_shortcut_table.mutable_candidates()->push_back(entry);

  entry.value = kSampleCandidate3;
  entry.annotation = kSampleAnnotation3;
  table.mutable_candidates()->push_back(entry);
  entry.label = kSampleShortcut3;
  no_shortcut_table.mutable_candidates()->push_back(entry);

  int before_height = 0;

  // Test for shortcut mode to no-shortcut mode.
  // Initialize with a shortcut mode lookup table.
  candidate_window_view.MaybeInitializeCandidateViews(table);
  ASSERT_EQ(3UL, candidate_window_view.candidate_views_.size());
  // Check the selected index is invalidated.
  EXPECT_EQ(-1, candidate_window_view.selected_candidate_index_in_page_);
  before_height =
      candidate_window_view.candidate_views_[0]->GetContentsBounds().height();
  // Checks all entry have same row height.
  for (size_t i = 1; i < candidate_window_view.candidate_views_.size(); ++i) {
    const CandidateView* view = candidate_window_view.candidate_views_[i];
    EXPECT_EQ(before_height, view->GetContentsBounds().height());
  }

  // Initialize with a no shortcut mode lookup table.
  candidate_window_view.MaybeInitializeCandidateViews(no_shortcut_table);
  ASSERT_EQ(3UL, candidate_window_view.candidate_views_.size());
  // Check the selected index is invalidated.
  EXPECT_EQ(-1, candidate_window_view.selected_candidate_index_in_page_);
  EXPECT_EQ(before_height,
            candidate_window_view.candidate_views_[0]->GetContentsBounds()
                .height());
  // Checks all entry have same row height.
  for (size_t i = 1; i < candidate_window_view.candidate_views_.size(); ++i) {
    const CandidateView* view = candidate_window_view.candidate_views_[i];
    EXPECT_EQ(before_height, view->GetContentsBounds().height());
  }

  // Test for no-shortcut mode to shortcut mode.
  // Initialize with a no shortcut mode lookup table.
  candidate_window_view.MaybeInitializeCandidateViews(no_shortcut_table);
  ASSERT_EQ(3UL, candidate_window_view.candidate_views_.size());
  // Check the selected index is invalidated.
  EXPECT_EQ(-1, candidate_window_view.selected_candidate_index_in_page_);
  before_height =
      candidate_window_view.candidate_views_[0]->GetContentsBounds().height();
  // Checks all entry have same row height.
  for (size_t i = 1; i < candidate_window_view.candidate_views_.size(); ++i) {
    const CandidateView* view = candidate_window_view.candidate_views_[i];
    EXPECT_EQ(before_height, view->GetContentsBounds().height());
  }

  // Initialize with a shortcut mode lookup table.
  candidate_window_view.MaybeInitializeCandidateViews(table);
  ASSERT_EQ(3UL, candidate_window_view.candidate_views_.size());
  // Check the selected index is invalidated.
  EXPECT_EQ(-1, candidate_window_view.selected_candidate_index_in_page_);
  EXPECT_EQ(before_height,
            candidate_window_view.candidate_views_[0]->GetContentsBounds()
                .height());
  // Checks all entry have same row height.
  for (size_t i = 1; i < candidate_window_view.candidate_views_.size(); ++i) {
    const CandidateView* view = candidate_window_view.candidate_views_[i];
    EXPECT_EQ(before_height, view->GetContentsBounds().height());
  }

  // We should call CloseNow method, otherwise this test will leak memory.
  widget->CloseNow();
}
}  // namespace input_method
}  // namespace chromeos
