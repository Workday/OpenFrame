// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ui/app_list/cocoa/test/apps_grid_controller_test_helper.h"

#include "base/mac/foundation_util.h"
#include "base/strings/stringprintf.h"
#include "ui/app_list/app_list_item.h"
#import "ui/app_list/cocoa/apps_grid_controller.h"
#import "ui/app_list/cocoa/apps_grid_view_item.h"
#import "ui/events/test/cocoa_test_event_utils.h"

namespace app_list {
namespace test {

const size_t AppsGridControllerTestHelper::kItemsPerPage = 16;

AppsGridControllerTestHelper::AppsGridControllerTestHelper() {
  Init();
  [AppsGridController setScrollAnimationDuration:0.0];
}

AppsGridControllerTestHelper::~AppsGridControllerTestHelper() {}

void AppsGridControllerTestHelper::SetUpWithGridController(
    AppsGridController* grid_controller) {
  ui::CocoaTest::SetUp();
  apps_grid_controller_ = grid_controller;
}

void AppsGridControllerTestHelper::SimulateClick(NSView* view) {
  std::pair<NSEvent*, NSEvent*> events(
      cocoa_test_event_utils::MouseClickInView(view, 1));
  [NSApp postEvent:events.first atStart:NO];
  [NSApp postEvent:events.second atStart:NO];
}

void AppsGridControllerTestHelper::SimulateKeyAction(SEL c) {
  [apps_grid_controller_ handleCommandBySelector:c];
}

void AppsGridControllerTestHelper::SimulateMouseEnterItemAt(size_t index) {
  [[apps_grid_controller_ itemAtIndex:index]
      mouseEntered:cocoa_test_event_utils::EnterEvent()];
}

void AppsGridControllerTestHelper::SimulateMouseExitItemAt(size_t index) {
  [[apps_grid_controller_ itemAtIndex:index]
      mouseExited:cocoa_test_event_utils::ExitEvent()];
}

std::string AppsGridControllerTestHelper::GetViewContent() const {
  std::string s;
  for (size_t page_index = 0; page_index < [apps_grid_controller_ pageCount];
       ++page_index) {
    s += '|';
    NSCollectionView* page_view =
        [apps_grid_controller_ collectionViewAtPageIndex:page_index];
    for (size_t i = 0; i < [[page_view content] count]; ++i) {
      AppsGridViewItem* item = base::mac::ObjCCastStrict<AppsGridViewItem>(
          [page_view itemAtIndex:i]);
      if (i != 0)
        s += ',';
      s += [item model]->id();
    }
    s += '|';
  }
  return s;
}

size_t AppsGridControllerTestHelper::GetPageIndexForItem(int item_id) const {
  const std::string search = base::StringPrintf("Item %d", item_id);
  size_t page_index = 0;
  NSUInteger found_at_page_index = NSNotFound;
  for (; page_index < [apps_grid_controller_ pageCount]; ++page_index) {
    NSCollectionView* page_view =
        [apps_grid_controller_ collectionViewAtPageIndex:page_index];
    for (NSUInteger i = 0; i < [[page_view content] count]; ++i) {
      AppsGridViewItem* item = base::mac::ObjCCastStrict<AppsGridViewItem>(
          [page_view itemAtIndex:i]);
      if ([item model]->id() == search) {
        if (found_at_page_index != NSNotFound)
          return NSNotFound;  // Duplicate.
        found_at_page_index = page_index;
      }
    }
  }
  return found_at_page_index;
}

void AppsGridControllerTestHelper::DelayForCollectionView() {
  message_loop_.PostDelayedTask(FROM_HERE,
                                base::MessageLoop::QuitWhenIdleClosure(),
                                base::TimeDelta::FromMilliseconds(100));
  message_loop_.Run();
}

void AppsGridControllerTestHelper::SinkEvents() {
  message_loop_.PostTask(FROM_HERE, base::MessageLoop::QuitWhenIdleClosure());
  message_loop_.Run();
}

NSButton* AppsGridControllerTestHelper::GetItemViewAt(size_t index) {
  if (index == NSNotFound)
    return nil;

  return [[apps_grid_controller_ itemAtIndex:index] button];
}

NSCollectionView* AppsGridControllerTestHelper::GetPageAt(size_t index) {
  return [apps_grid_controller_ collectionViewAtPageIndex:index];
}

NSView* AppsGridControllerTestHelper::GetSelectedView() {
  return GetItemViewAt([apps_grid_controller_ selectedItemIndex]);
}

}  // namespace test
}  // namespace app_list
