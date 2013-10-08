// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "chrome/browser/extensions/component_loader.h"
#include "chrome/browser/extensions/extension_action.h"
#include "chrome/browser/extensions/extension_action_manager.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/extensions/script_bubble_controller.h"
#include "chrome/browser/extensions/test_extension_system.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_builder.h"
#include "chrome/common/extensions/feature_switch.h"
#include "chrome/common/extensions/value_builder.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/test/test_browser_thread.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/browser/chromeos/settings/device_settings_service.h"
#endif

namespace extensions {
namespace {

class ScriptBubbleControllerTest : public ChromeRenderViewHostTestHarness {
 protected:
  ScriptBubbleControllerTest()
      : enable_script_bubble_(FeatureSwitch::script_bubble(), true) {
  }

  virtual void SetUp() OVERRIDE {
    ChromeRenderViewHostTestHarness::SetUp();
#if defined OS_CHROMEOS
  test_user_manager_.reset(new chromeos::ScopedTestUserManager());
#endif
    CommandLine command_line(CommandLine::NO_PROGRAM);
    Profile* profile =
        Profile::FromBrowserContext(web_contents()->GetBrowserContext());
    extension_service_ = static_cast<TestExtensionSystem*>(
        ExtensionSystem::Get(profile))->CreateExtensionService(
            &command_line, base::FilePath(), false);
    extension_service_->Init();

    TabHelper::CreateForWebContents(web_contents());

    script_bubble_controller_ =
        TabHelper::FromWebContents(web_contents())->script_bubble_controller();
  }

  virtual void TearDown() OVERRIDE {
#if defined OS_CHROMEOS
    test_user_manager_.reset();
#endif
    ChromeRenderViewHostTestHarness::TearDown();
  }

  int tab_id() {
    return ExtensionTabUtil::GetTabId(web_contents());
  }

  ExtensionService* extension_service_;
  ScriptBubbleController* script_bubble_controller_;

 private:
  FeatureSwitch::ScopedOverride enable_script_bubble_;

#if defined OS_CHROMEOS
  chromeos::ScopedTestDeviceSettingsService test_device_settings_service_;
  chromeos::ScopedTestCrosSettings test_cros_settings_;
  scoped_ptr<chromeos::ScopedTestUserManager> test_user_manager_;
#endif
};

TEST_F(ScriptBubbleControllerTest, Basics) {
#if defined(OS_WIN)
  base::FilePath root(FILE_PATH_LITERAL("c:\\"));
#else
  base::FilePath root(FILE_PATH_LITERAL("/root"));
#endif
  scoped_refptr<const Extension> extension1 =
      ExtensionBuilder()
      .SetPath(root.AppendASCII("f1"))
      .SetManifest(DictionaryBuilder()
                   .Set("name", "ex1")
                   .Set("version", "1")
                   .Set("manifest_version", 2)
                   .Set("permissions", ListBuilder()
                        .Append("activeTab")))
      .Build();

  scoped_refptr<const Extension> extension2 =
      ExtensionBuilder()
      .SetPath(root.AppendASCII("f2"))
      .SetManifest(DictionaryBuilder()
                   .Set("name", "ex2")
                   .Set("version", "1")
                   .Set("manifest_version", 2)
                   .Set("permissions", ListBuilder()
                        .Append("activeTab")))
      .Build();

  scoped_refptr<const Extension> extension3 =
      ExtensionBuilder()
      .SetPath(root.AppendASCII("f3"))
      .SetManifest(DictionaryBuilder()
                   .Set("name", "ex3")
                   .Set("version", "1")
                   .Set("manifest_version", 2)
                   .Set("permissions", ListBuilder()
                        .Append("activeTab")))
      .Build();

  extension_service_->AddExtension(extension1.get());
  extension_service_->AddExtension(extension2.get());
  extension_service_->AddExtension(extension3.get());

  EXPECT_EQ(0u, script_bubble_controller_->extensions_running_scripts().size());

  NavigateAndCommit(GURL("http://www.google.com"));

  // Running a script on the tab causes the bubble to be visible.
  TabHelper::ScriptExecutionObserver::ExecutingScriptsMap executing_scripts;
  executing_scripts[extension1->id()].insert("script1");
  script_bubble_controller_->OnScriptsExecuted(
      web_contents(),
      executing_scripts,
      web_contents()->GetController().GetActiveEntry()->GetPageID(),
      web_contents()->GetController().GetActiveEntry()->GetURL());
  EXPECT_EQ(1u, script_bubble_controller_->extensions_running_scripts().size());
  std::set<std::string> extension_ids;
  extension_ids.insert(extension1->id());
  EXPECT_EQ(1u, script_bubble_controller_->extensions_running_scripts().size());
  EXPECT_TRUE(extension_ids ==
             script_bubble_controller_->extensions_running_scripts());

  // Running a script from another extension increments the count.
  executing_scripts.clear();
  executing_scripts[extension2->id()].insert("script2");
  script_bubble_controller_->OnScriptsExecuted(
      web_contents(),
      executing_scripts,
      web_contents()->GetController().GetActiveEntry()->GetPageID(),
      web_contents()->GetController().GetActiveEntry()->GetURL());
  EXPECT_EQ(2u, script_bubble_controller_->extensions_running_scripts().size());
  extension_ids.insert(extension2->id());
  EXPECT_TRUE(extension_ids ==
             script_bubble_controller_->extensions_running_scripts());

  // Running another script from an already-seen extension does not affect
  // count.
  executing_scripts.clear();
  executing_scripts[extension2->id()].insert("script3");
  script_bubble_controller_->OnScriptsExecuted(
      web_contents(),
      executing_scripts,
      web_contents()->GetController().GetActiveEntry()->GetPageID(),
      web_contents()->GetController().GetActiveEntry()->GetURL());
  EXPECT_EQ(2u, script_bubble_controller_->extensions_running_scripts().size());

  // Running tabs.executeScript from an already-seen extension does not affect
  // count.
  executing_scripts.clear();
  executing_scripts[extension1->id()] = std::set<std::string>();
  script_bubble_controller_->OnScriptsExecuted(
      web_contents(), executing_scripts, 0, GURL());
  EXPECT_EQ(2u, script_bubble_controller_->extensions_running_scripts().size());

  // Running tabs.executeScript from a new extension increments the count.
  executing_scripts.clear();
  executing_scripts[extension3->id()] = std::set<std::string>();
  script_bubble_controller_->OnScriptsExecuted(
      web_contents(), executing_scripts, 0, GURL());
  EXPECT_EQ(3u, script_bubble_controller_->extensions_running_scripts().size());

  // Navigating away resets the badge.
  NavigateAndCommit(GURL("http://www.google.com"));
  EXPECT_EQ(0u, script_bubble_controller_->extensions_running_scripts().size());
};

}  // namespace
}  // namespace extensions
