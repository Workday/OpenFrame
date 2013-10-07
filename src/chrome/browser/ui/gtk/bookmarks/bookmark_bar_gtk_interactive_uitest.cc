// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtk/gtk.h>

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/gtk/view_id_util.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"

namespace {

const char kSimplePage[] = "/404_is_enough_for_us.html";

void OnClicked(GtkWidget* widget, bool* clicked_bit) {
  *clicked_bit = true;
}

}  // namespace

class BookmarkBarGtkInteractiveUITest : public InProcessBrowserTest {
};

// Makes sure that when you switch back to an NTP with an active findbar,
// the findbar is above the floating bookmark bar.
IN_PROC_BROWSER_TEST_F(BookmarkBarGtkInteractiveUITest, FindBarTest) {
  ASSERT_TRUE(embedded_test_server()->InitializeAndWaitUntilReady());

  // Create new tab; open findbar.
  chrome::NewTab(browser());
  chrome::Find(browser());

  // Create new tab with an arbitrary URL.
  GURL url = embedded_test_server()->GetURL(kSimplePage);
  chrome::AddSelectedTabWithURL(browser(), url, content::PAGE_TRANSITION_TYPED);

  // Switch back to the NTP with the active findbar.
  browser()->tab_strip_model()->ActivateTabAt(1, false);

  // Wait for the findbar to show.
  base::MessageLoop::current()->RunUntilIdle();

  // Set focus somewhere else, so that we can test clicking on the findbar
  // works.
  chrome::FocusLocationBar(browser());
  ui_test_utils::ClickOnView(browser(), VIEW_ID_FIND_IN_PAGE_TEXT_FIELD);
  ui_test_utils::IsViewFocused(browser(), VIEW_ID_FIND_IN_PAGE_TEXT_FIELD);
}

// Makes sure that you can click on the floating bookmark bar.
// Disabled due to http://crbug.com/88933.
IN_PROC_BROWSER_TEST_F(
    BookmarkBarGtkInteractiveUITest, DISABLED_ClickOnFloatingTest) {
  ASSERT_TRUE(embedded_test_server()->InitializeAndWaitUntilReady());

  GtkWidget* other_bookmarks =
      ViewIDUtil::GetWidget(GTK_WIDGET(browser()->window()->GetNativeWindow()),
      VIEW_ID_OTHER_BOOKMARKS);
  bool has_been_clicked = false;
  g_signal_connect(other_bookmarks, "clicked",
                   G_CALLBACK(OnClicked), &has_been_clicked);

  // Create new tab.
  chrome::NewTab(browser());

  // Wait for the floating bar to appear.
  base::MessageLoop::current()->RunUntilIdle();

  // This is kind of a hack. Calling this just once doesn't seem to send a click
  // event, but doing it twice works.
  // http://crbug.com/35581
  ui_test_utils::ClickOnView(browser(), VIEW_ID_OTHER_BOOKMARKS);
  ui_test_utils::ClickOnView(browser(), VIEW_ID_OTHER_BOOKMARKS);

  EXPECT_TRUE(has_been_clicked);
}
