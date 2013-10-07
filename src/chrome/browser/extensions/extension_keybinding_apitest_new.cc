// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/active_tab_permission_granter.h"
#include "chrome/browser/extensions/browser_action_test_util.h"
#include "chrome/browser/extensions/extension_action.h"
#include "chrome/browser/extensions/extension_action_manager.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/extensions/tab_helper.h"
#include "chrome/browser/sessions/session_tab_helper.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test_utils.h"

using content::WebContents;

namespace extensions {

class CommandsApiTest : public ExtensionApiTest {
 public:
  CommandsApiTest() {}
  virtual ~CommandsApiTest() {}

 protected:
  BrowserActionTestUtil GetBrowserActionsBar() {
    return BrowserActionTestUtil(browser());
  }
};

class ScriptBadgesCommandsApiTest : public ExtensionApiTest {
 public:
  ScriptBadgesCommandsApiTest() {
    // We cannot add this to CommandsApiTest because then PageActions get
    // treated like BrowserActions and the PageAction test starts failing.
    CommandLine::ForCurrentProcess()->AppendSwitchASCII(
        switches::kScriptBadges, "1");
  }
  virtual ~ScriptBadgesCommandsApiTest() {}
};

// Test the basic functionality of the Keybinding API:
// - That pressing the shortcut keys should perform actions (activate the
//   browser action or send an event).
// - Note: Page action keybindings are tested in PageAction test below.
// - The shortcut keys taken by one extension are not overwritten by the last
//   installed extension.
IN_PROC_BROWSER_TEST_F(CommandsApiTest, Basic) {
  ASSERT_TRUE(test_server()->Start());
  ASSERT_TRUE(RunExtensionTest("keybinding/basics")) << message_;
  const Extension* extension = GetSingleLoadedExtension();
  ASSERT_TRUE(extension) << message_;

  // Load this extension, which uses the same keybindings but sets the page
  // to different colors. This is so we can see that it doesn't interfere. We
  // don't test this extension in any other way (it should otherwise be
  // immaterial to this test).
  ASSERT_TRUE(RunExtensionTest("keybinding/conflicting")) << message_;

  // Test that there are two browser actions in the toolbar.
  ASSERT_EQ(2, GetBrowserActionsBar().NumberOfBrowserActions());

  ui_test_utils::NavigateToURL(browser(),
      test_server()->GetURL("files/extensions/test_file.txt"));

  // activeTab shouldn't have been granted yet.
  WebContents* tab = browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(tab);

  ActiveTabPermissionGranter* granter =
      TabHelper::FromWebContents(tab)->active_tab_permission_granter();
  EXPECT_FALSE(granter->IsGranted(extension));

  // Activate the shortcut (Ctrl+Shift+F).
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(
      browser(), ui::VKEY_F, true, true, false, false));

  // activeTab should now be granted.
  EXPECT_TRUE(granter->IsGranted(extension));

  // Verify the command worked.
  bool result = false;
  ASSERT_TRUE(content::ExecuteScriptAndExtractBool(
      tab,
      "setInterval(function(){"
      "  if(document.body.bgColor == 'red'){"
      "    window.domAutomationController.send(true)}}, 100)",
      &result));
  ASSERT_TRUE(result);

  // Activate the shortcut (Ctrl+Shift+Y).
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(
      browser(), ui::VKEY_Y, true, true, false, false));

  result = false;
  ASSERT_TRUE(content::ExecuteScriptAndExtractBool(
      tab,
      "setInterval(function(){"
      "  if(document.body.bgColor == 'blue'){"
      "    window.domAutomationController.send(true)}}, 100)",
      &result));
  ASSERT_TRUE(result);
}

// Flaky on linux and chromeos, http://crbug.com/165825
#if defined(OS_MACOSX) || defined(OS_WIN)
#define MAYBE_PageAction PageAction
#else
#define MAYBE_PageAction DISABLED_PageAction
#endif
IN_PROC_BROWSER_TEST_F(CommandsApiTest, MAYBE_PageAction) {
  ASSERT_TRUE(test_server()->Start());
  ASSERT_TRUE(RunExtensionTest("keybinding/page_action")) << message_;
  const Extension* extension = GetSingleLoadedExtension();
  ASSERT_TRUE(extension) << message_;

  {
    // Load a page, the extension will detect the navigation and request to show
    // the page action icon.
    ResultCatcher catcher;
    ui_test_utils::NavigateToURL(browser(),
        test_server()->GetURL("files/extensions/test_file.txt"));
    ASSERT_TRUE(catcher.GetNextResult());
  }

  // Make sure it appears and is the right one.
  ASSERT_TRUE(WaitForPageActionVisibilityChangeTo(1));
  int tab_id = SessionTabHelper::FromWebContents(
      browser()->tab_strip_model()->GetActiveWebContents())->session_id().id();
  ExtensionAction* action =
      ExtensionActionManager::Get(browser()->profile())->
      GetPageAction(*extension);
  ASSERT_TRUE(action);
  EXPECT_EQ("Make this page red", action->GetTitle(tab_id));

  // Activate the shortcut (Alt+Shift+F).
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(
      browser(), ui::VKEY_F, false, true, true, false));

  // Verify the command worked (the page action turns the page red).
  WebContents* tab = browser()->tab_strip_model()->GetActiveWebContents();
  bool result = false;
  ASSERT_TRUE(content::ExecuteScriptAndExtractBool(
      tab,
      "setInterval(function(){"
      "  if(document.body.bgColor == 'red'){"
      "    window.domAutomationController.send(true)}}, 100)",
      &result));
  ASSERT_TRUE(result);
}

// Checked-in in a disabled state, because the necessary functionality to
// automatically verify that the test works hasn't been implemented for the
// script badges yet (see http://crbug.com/140016). The test results, can be
// verified manually by running the test and verifying that the synthesized
// popup for script badges appear. When bug 140016 has been fixed, the popup
// code can signal to the test that the test passed.
// TODO(finnur): Enable this test once the bug is fixed.
IN_PROC_BROWSER_TEST_F(ScriptBadgesCommandsApiTest, DISABLED_ScriptBadge) {
  ASSERT_TRUE(test_server()->Start());
  ASSERT_TRUE(RunExtensionTest("keybinding/script_badge")) << message_;
  const Extension* extension = GetSingleLoadedExtension();
  ASSERT_TRUE(extension) << message_;

  {
    ResultCatcher catcher;
    // Tell the extension to update the script badge state.
    ui_test_utils::NavigateToURL(
        browser(), GURL(extension->GetResourceURL("show.html")));
    ASSERT_TRUE(catcher.GetNextResult());
  }

  {
    ResultCatcher catcher;
    // Activate the shortcut (Ctrl+Shift+F).
    ASSERT_TRUE(ui_test_utils::SendKeyPressSync(
        browser(), ui::VKEY_F, true, true, false, false));
    ASSERT_TRUE(catcher.GetNextResult());
  }
}

// This test validates that the getAll query API function returns registered
// commands as well as synthesized ones and that inactive commands (like the
// synthesized ones are in nature) have no shortcuts.
IN_PROC_BROWSER_TEST_F(CommandsApiTest, SynthesizedCommand) {
  ASSERT_TRUE(test_server()->Start());
  ASSERT_TRUE(RunExtensionTest("keybinding/synthesized")) << message_;
}

}  // namespace extensions
