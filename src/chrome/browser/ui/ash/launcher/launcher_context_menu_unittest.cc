// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ash/launcher/launcher_context_menu.h"

#include "ash/launcher/launcher.h"
#include "ash/launcher/launcher_model.h"
#include "ash/launcher/launcher_types.h"
#include "ash/test/ash_test_base.h"
#include "base/prefs/pref_service.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/prefs/incognito_mode_prefs.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/ash/launcher/chrome_launcher_controller.h"
#include "chrome/browser/ui/ash/launcher/chrome_launcher_controller_per_browser.h"
#include "chrome/test/base/testing_profile.h"
#include "ui/aura/root_window.h"

class TestChromeLauncherControllerPerBrowser :
    public ChromeLauncherControllerPerBrowser {
 public:
  TestChromeLauncherControllerPerBrowser(
      Profile* profile, ash::LauncherModel* model)
      : ChromeLauncherControllerPerBrowser(profile, model) {}
  virtual bool IsLoggedInAsGuest() OVERRIDE {
    return false;
  }
 private:
  DISALLOW_COPY_AND_ASSIGN(TestChromeLauncherControllerPerBrowser);
};

class LauncherContextMenuTest : public ash::test::AshTestBase {
 protected:
  static bool IsItemPresentInMenu(LauncherContextMenu* menu, int command_id) {
    DCHECK(menu);
    return menu->GetIndexOfCommandId(command_id) != -1;
  }

  LauncherContextMenuTest()
      : profile_(new TestingProfile()) {}

  virtual void SetUp() OVERRIDE {
    ash::test::AshTestBase::SetUp();
    controller_.reset(
        new TestChromeLauncherControllerPerBrowser(profile(),
                                                   &launcher_model_));
  }

  virtual void TearDown() OVERRIDE {
    controller_.reset(NULL);
    ash::test::AshTestBase::TearDown();
  }

  LauncherContextMenu* CreateLauncherContextMenu(
      ash::LauncherItemType launcher_item_type) {
    ash::LauncherItem item;
    item.id = 1;  // dummy id
    item.type = launcher_item_type;
    return new LauncherContextMenu(controller_.get(), &item, CurrentContext());
  }

  Profile* profile() { return profile_.get(); }

 private:
  scoped_ptr<TestingProfile> profile_;
  ash::LauncherModel launcher_model_;
  scoped_ptr<ChromeLauncherController> controller_;

  DISALLOW_COPY_AND_ASSIGN(LauncherContextMenuTest);
};

// Verifies that "New Incognito window" menu item in the launcher context
// menu is disabled when Incognito mode is switched off (by a policy).
TEST_F(LauncherContextMenuTest,
       NewIncognitoWindowMenuIsDisabledWhenIncognitoModeOff) {
  // Initially, "New Incognito window" should be enabled.
  scoped_ptr<LauncherContextMenu> menu(
      CreateLauncherContextMenu(ash::TYPE_BROWSER_SHORTCUT));
  ASSERT_TRUE(IsItemPresentInMenu(
      menu.get(), LauncherContextMenu::MENU_NEW_INCOGNITO_WINDOW));
  EXPECT_TRUE(menu->IsCommandIdEnabled(
      LauncherContextMenu::MENU_NEW_INCOGNITO_WINDOW));

  // Disable Incognito mode.
  IncognitoModePrefs::SetAvailability(profile()->GetPrefs(),
                                      IncognitoModePrefs::DISABLED);
  menu.reset(CreateLauncherContextMenu(ash::TYPE_BROWSER_SHORTCUT));
  // The item should be disabled.
  ASSERT_TRUE(IsItemPresentInMenu(
      menu.get(), LauncherContextMenu::MENU_NEW_INCOGNITO_WINDOW));
  EXPECT_FALSE(menu->IsCommandIdEnabled(
      LauncherContextMenu::MENU_NEW_INCOGNITO_WINDOW));
}

// Verifies that "New window" menu item in the launcher context
// menu is disabled when Incognito mode is forced (by a policy).
TEST_F(LauncherContextMenuTest,
       NewWindowMenuIsDisabledWhenIncognitoModeForced) {
  // Initially, "New window" should be enabled.
  scoped_ptr<LauncherContextMenu> menu(
      CreateLauncherContextMenu(ash::TYPE_BROWSER_SHORTCUT));
  ASSERT_TRUE(IsItemPresentInMenu(
      menu.get(), LauncherContextMenu::MENU_NEW_WINDOW));
  EXPECT_TRUE(menu->IsCommandIdEnabled(LauncherContextMenu::MENU_NEW_WINDOW));

  // Disable Incognito mode.
  IncognitoModePrefs::SetAvailability(profile()->GetPrefs(),
                                      IncognitoModePrefs::FORCED);
  menu.reset(CreateLauncherContextMenu(ash::TYPE_BROWSER_SHORTCUT));
  ASSERT_TRUE(IsItemPresentInMenu(
      menu.get(), LauncherContextMenu::MENU_NEW_WINDOW));
  EXPECT_FALSE(menu->IsCommandIdEnabled(LauncherContextMenu::MENU_NEW_WINDOW));
}
