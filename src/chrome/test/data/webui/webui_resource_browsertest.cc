// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test_utils.h"
#include "grit/webui_resources.h"
#include "ui/base/resource/resource_bundle.h"

class WebUIResourceBrowserTest : public InProcessBrowserTest {
 public:

  // Runs all test functions in |file| and waits for a single
  // "SUCCESS" or "FAILURE" at the conclusion of the tests.
  void RunTest(const base::FilePath& file) {
    GURL url = ui_test_utils::GetTestUrl(
        base::FilePath(FILE_PATH_LITERAL("webui")),
        file);
    ui_test_utils::NavigateToURL(browser(), url);

    // Inject scripts for internal libraries.
    std::string script;
    std::vector<int>::iterator include_libraries_iterator;
    for (include_libraries_iterator = include_libraries.begin();
         include_libraries_iterator != include_libraries.end();
         ++include_libraries_iterator) {
      base::StringPiece library_content =
          ResourceBundle::GetSharedInstance().GetRawDataResource(
              *include_libraries_iterator);
      library_content.AppendToString(&script);
      script.append("\n");
    }
    ExecuteJavascriptOnCurrentTab(script);

    content::DOMMessageQueue message_queue;
    std::string message;
    ExecuteJavascriptOnCurrentTab("runTests()");
    ASSERT_TRUE(message_queue.WaitForMessage(&message));
    while (message.compare("\"PENDING\"") == 0) {
      ASSERT_TRUE(message_queue.WaitForMessage(&message));
    }
    EXPECT_STREQ("\"SUCCESS\"", message.c_str());
  }

  // Queues the library corresponding to |resource_id| for injection into the
  // test. The code injection is performed post-load, so any common test
  // initialization that depends on the library should be placed in a setUp
  // function.
  void AddLibrary(int resource_id) {
    include_libraries.push_back(resource_id);
  }

 private:
  void ExecuteJavascriptOnCurrentTab(const std::string& script) {
    content::RenderViewHost* rvh = browser()->tab_strip_model()->
        GetActiveWebContents()->GetRenderViewHost();
    ASSERT_TRUE(rvh);
    ASSERT_TRUE(content::ExecuteScript(rvh, script));
  }

  // Resource IDs for internal javascript libraries to inject into the test.
  std::vector<int> include_libraries;
};

IN_PROC_BROWSER_TEST_F(WebUIResourceBrowserTest, ArrayDataModelTest) {
  AddLibrary(IDR_WEBUI_JS_CR);
  AddLibrary(IDR_WEBUI_JS_CR_EVENT_TARGET);
  AddLibrary(IDR_WEBUI_JS_CR_UI_ARRAY_DATA_MODEL);
  RunTest(base::FilePath(FILE_PATH_LITERAL("array_data_model_test.html")));
}

IN_PROC_BROWSER_TEST_F(WebUIResourceBrowserTest, PropertyTest) {
  AddLibrary(IDR_WEBUI_JS_CR);
  AddLibrary(IDR_WEBUI_JS_CR_EVENT_TARGET);
  RunTest(base::FilePath(FILE_PATH_LITERAL("cr_test.html")));
}

IN_PROC_BROWSER_TEST_F(WebUIResourceBrowserTest, EventTargetTest) {
  AddLibrary(IDR_WEBUI_JS_CR);
  AddLibrary(IDR_WEBUI_JS_CR_EVENT_TARGET);
  RunTest(base::FilePath(FILE_PATH_LITERAL("event_target_test.html")));
}

IN_PROC_BROWSER_TEST_F(WebUIResourceBrowserTest, GridTest) {
  AddLibrary(IDR_WEBUI_JS_CR);
  AddLibrary(IDR_WEBUI_JS_CR_EVENT_TARGET);
  AddLibrary(IDR_WEBUI_JS_CR_UI);
  AddLibrary(IDR_WEBUI_JS_CR_UI_ARRAY_DATA_MODEL);
  AddLibrary(IDR_WEBUI_JS_CR_UI_LIST);
  AddLibrary(IDR_WEBUI_JS_CR_UI_LIST_ITEM);
  AddLibrary(IDR_WEBUI_JS_CR_UI_LIST_SELECTION_CONTROLLER);
  AddLibrary(IDR_WEBUI_JS_CR_UI_LIST_SELECTION_MODEL);
  // Grid must be the last addition as it depends on list libraries.
  AddLibrary(IDR_WEBUI_JS_CR_UI_GRID);
  RunTest(base::FilePath(FILE_PATH_LITERAL("grid_test.html")));
}

IN_PROC_BROWSER_TEST_F(WebUIResourceBrowserTest, LinkControllerTest) {
  AddLibrary(IDR_WEBUI_JS_CR);
  AddLibrary(IDR_WEBUI_JS_CR_LINK_CONTROLLER);
  RunTest(base::FilePath(FILE_PATH_LITERAL("link_controller_test.html")));
}

IN_PROC_BROWSER_TEST_F(WebUIResourceBrowserTest, ListSelectionModelTest) {
  AddLibrary(IDR_WEBUI_JS_CR);
  AddLibrary(IDR_WEBUI_JS_CR_EVENT_TARGET);
  AddLibrary(IDR_WEBUI_JS_CR_UI_LIST_SELECTION_MODEL);
  RunTest(base::FilePath(FILE_PATH_LITERAL("list_selection_model_test.html")));
}

IN_PROC_BROWSER_TEST_F(WebUIResourceBrowserTest, ListSingleSelectionModelTest) {
  AddLibrary(IDR_WEBUI_JS_CR);
  AddLibrary(IDR_WEBUI_JS_CR_EVENT_TARGET);
  AddLibrary(IDR_WEBUI_JS_CR_UI_LIST_SINGLE_SELECTION_MODEL);
  RunTest(base::FilePath(FILE_PATH_LITERAL(
      "list_single_selection_model_test.html")));
}

IN_PROC_BROWSER_TEST_F(WebUIResourceBrowserTest, LocalStringsTest) {
  AddLibrary(IDR_WEBUI_JS_LOCAL_STRINGS);
  RunTest(base::FilePath(FILE_PATH_LITERAL("local_strings_test.html")));
}

IN_PROC_BROWSER_TEST_F(WebUIResourceBrowserTest, MockTimerTest) {
  RunTest(base::FilePath(FILE_PATH_LITERAL("mock_timer_test.html")));
}

IN_PROC_BROWSER_TEST_F(WebUIResourceBrowserTest, ParseHtmlSubsetTest) {
  AddLibrary(IDR_WEBUI_JS_PARSE_HTML_SUBSET);
  RunTest(base::FilePath(FILE_PATH_LITERAL("parse_html_subset_test.html")));
}

IN_PROC_BROWSER_TEST_F(WebUIResourceBrowserTest, PositionUtilTest) {
  AddLibrary(IDR_WEBUI_JS_CR);
  AddLibrary(IDR_WEBUI_JS_CR_UI_POSITION_UTIL);
  RunTest(base::FilePath(FILE_PATH_LITERAL("position_util_test.html")));
}

IN_PROC_BROWSER_TEST_F(WebUIResourceBrowserTest, PromiseTest) {
  AddLibrary(IDR_WEBUI_JS_CR);
  AddLibrary(IDR_WEBUI_JS_CR_PROMISE);
  RunTest(base::FilePath(FILE_PATH_LITERAL("promise_test.html")));
}

IN_PROC_BROWSER_TEST_F(WebUIResourceBrowserTest, RepeatingButtonTest) {
  AddLibrary(IDR_WEBUI_JS_CR);
  AddLibrary(IDR_WEBUI_JS_CR_UI);
  AddLibrary(IDR_WEBUI_JS_CR_UI_REPEATING_BUTTON);
  RunTest(base::FilePath(FILE_PATH_LITERAL("repeating_button_test.html")));
}
