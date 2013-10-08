// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test_utils.h"

using content::WebContents;

namespace {

const char kSharedWorkerTestPage[] =
    "files/workers/workers_ui_shared_worker.html";
const char kSharedWorkerJs[] =
    "files/workers/workers_ui_shared_worker.js";

class InspectUITest : public InProcessBrowserTest {
 public:
  InspectUITest() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(InspectUITest);
};

// The test fails on Mac OS X and Windows, see crbug.com/89583
// Intermittently fails on Linux.
IN_PROC_BROWSER_TEST_F(InspectUITest, DISABLED_SharedWorkersList) {
  ASSERT_TRUE(test_server()->Start());
  GURL url = test_server()->GetURL(kSharedWorkerTestPage);
  ui_test_utils::NavigateToURL(browser(), url);

  ui_test_utils::NavigateToURLWithDisposition(
      browser(),
      GURL(chrome::kChromeUIInspectURL),
      NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);

  WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(web_contents != NULL);

  std::string result;
  ASSERT_TRUE(
      content::ExecuteScriptAndExtractString(
          web_contents,
          "window.domAutomationController.send("
          "    '' + document.body.textContent);",
          &result));
  ASSERT_TRUE(result.find(kSharedWorkerJs) != std::string::npos);
  ASSERT_TRUE(result.find(kSharedWorkerTestPage) != std::string::npos);
}

IN_PROC_BROWSER_TEST_F(InspectUITest, ReloadCrash) {
  ASSERT_TRUE(test_server()->Start());
  // Make sure that loading the inspect UI twice in the same tab
  // connects/disconnects listeners without crashing.
  ui_test_utils::NavigateToURL(browser(), GURL(chrome::kChromeUIInspectURL));
  ui_test_utils::NavigateToURL(browser(), GURL(chrome::kChromeUIInspectURL));
}

}  // namespace
