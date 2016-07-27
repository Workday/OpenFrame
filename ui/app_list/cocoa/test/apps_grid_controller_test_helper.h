// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_APP_LIST_COCOA_TEST_APPS_GRID_CONTROLLER_TEST_HELPER_H_
#define UI_APP_LIST_COCOA_TEST_APPS_GRID_CONTROLLER_TEST_HELPER_H_

#include <string>

#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#import "ui/gfx/test/ui_cocoa_test_helper.h"

@class AppsGridController;

namespace app_list {

namespace test {

class AppsGridControllerTestHelper : public ui::CocoaTest {
 public:
  static const size_t kItemsPerPage;

  AppsGridControllerTestHelper();
  ~AppsGridControllerTestHelper() override;

  void SetUpWithGridController(AppsGridController* grid_controller);

 protected:
  // Send a click to the test window in the centre of |view|.
  void SimulateClick(NSView* view);

  // Send a key action using handleCommandBySelector.
  void SimulateKeyAction(SEL c);

  void SimulateMouseEnterItemAt(size_t index);
  void SimulateMouseExitItemAt(size_t index);

  // Get a string representation of the items as they are currently ordered in
  // the view. Each page will start and end with a | character.
  std::string GetViewContent() const;

  // Find the page containing |item_id|, and return the index of that page.
  // Return NSNotFound if the item is not found, or if the item appears more
  // than once.
  size_t GetPageIndexForItem(int item_id) const;

  void DelayForCollectionView();
  void SinkEvents();

  NSButton* GetItemViewAt(size_t index);
  NSCollectionView* GetPageAt(size_t index);
  NSView* GetSelectedView();

  AppsGridController* apps_grid_controller_;

 private:
  base::MessageLoopForUI message_loop_;

  DISALLOW_COPY_AND_ASSIGN(AppsGridControllerTestHelper);
};

}  // namespace test
}  // namespace app_list

#endif  // UI_APP_LIST_COCOA_TEST_APPS_GRID_CONTROLLER_TEST_HELPER_H_
