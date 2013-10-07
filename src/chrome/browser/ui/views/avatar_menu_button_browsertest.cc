// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/avatar_menu_button.h"

#include "base/command_line.h"
#include "base/path_service.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/profiles/avatar_menu_model.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profiles_state.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/avatar_menu_bubble_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/profile_chooser_view.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/test_switches.h"
#include "chrome/test/base/testing_browser_process.h"
#include "content/public/test/test_utils.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/views/controls/button/label_button.h"

class AvatarMenuButtonTest : public InProcessBrowserTest,
                             public testing::WithParamInterface<bool> {
 public:
  AvatarMenuButtonTest();
  virtual ~AvatarMenuButtonTest();

 protected:
  virtual void SetUp() OVERRIDE;

  bool UsingNewProfileChooser();
  void CreateTestingProfile();
  AvatarMenuButton* GetAvatarMenuButton();
  void StartAvatarMenu();

 private:
  DISALLOW_COPY_AND_ASSIGN(AvatarMenuButtonTest);
};

AvatarMenuButtonTest::AvatarMenuButtonTest() {
}

AvatarMenuButtonTest::~AvatarMenuButtonTest() {
}

void AvatarMenuButtonTest::SetUp() {
  if (GetParam()) {
    if (!UsingNewProfileChooser()) {
      CommandLine::ForCurrentProcess()->AppendSwitch(
        switches::kNewProfileManagement);
    }
    DCHECK(UsingNewProfileChooser());
  } else {
    DCHECK(!UsingNewProfileChooser());
  }

  InProcessBrowserTest::SetUp();
}

bool AvatarMenuButtonTest::UsingNewProfileChooser() {
  return CommandLine::ForCurrentProcess()->HasSwitch(
    switches::kNewProfileManagement);
}

void AvatarMenuButtonTest::CreateTestingProfile() {
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  EXPECT_EQ(1u, profile_manager->GetNumberOfProfiles());

  base::FilePath path;
  PathService::Get(chrome::DIR_USER_DATA, &path);
  path = path.AppendASCII("test_profile");
  if (!base::PathExists(path))
    ASSERT_TRUE(file_util::CreateDirectory(path));
  Profile* profile =
      Profile::CreateProfile(path, NULL, Profile::CREATE_MODE_SYNCHRONOUS);
  profile_manager->RegisterTestingProfile(profile, true, false);

  EXPECT_EQ(2u, profile_manager->GetNumberOfProfiles());
}

AvatarMenuButton* AvatarMenuButtonTest::GetAvatarMenuButton() {
  BrowserView* browser_view = reinterpret_cast<BrowserView*>(
      browser()->window());
  return browser_view->frame()->GetAvatarMenuButton();
}

void AvatarMenuButtonTest::StartAvatarMenu() {
  AvatarMenuButton* button = GetAvatarMenuButton();
  ASSERT_TRUE(button);

  AvatarMenuBubbleView::set_close_on_deactivate(false);
  ProfileChooserView::set_close_on_deactivate(false);
  static_cast<views::MenuButtonListener*>(button)->OnMenuButtonClicked(
      NULL, gfx::Point());
  base::MessageLoop::current()->RunUntilIdle();
  EXPECT_NE(AvatarMenuBubbleView::IsShowing(),
            ProfileChooserView::IsShowing());
}

IN_PROC_BROWSER_TEST_P(AvatarMenuButtonTest, HideOnSecondClick) {
#if defined(OS_WIN) && defined(USE_ASH)
  // Disable this test in Metro+Ash for now (http://crbug.com/262796).
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kAshBrowserTests))
    return;
#endif

  if (!profiles::IsMultipleProfilesEnabled() ||
      UsingNewProfileChooser()) {
    return;
  }

  CreateTestingProfile();
  StartAvatarMenu();

  // Verify that clicking again doesn't reshow it.
  AvatarMenuButton* button = GetAvatarMenuButton();
  static_cast<views::MenuButtonListener*>(button)->OnMenuButtonClicked(
      NULL, gfx::Point());
  // Hide the bubble manually. In the browser this would normally happen during
  // the event processing.
  AvatarMenuBubbleView::Hide();
  base::MessageLoop::current()->RunUntilIdle();
  EXPECT_FALSE(AvatarMenuBubbleView::IsShowing());
  EXPECT_FALSE(ProfileChooserView::IsShowing());
}

IN_PROC_BROWSER_TEST_P(AvatarMenuButtonTest, NewSignOut) {
  if (!profiles::IsMultipleProfilesEnabled() ||
      !UsingNewProfileChooser()) {
    return;
  }

  CreateTestingProfile();
  StartAvatarMenu();

  BrowserList* browser_list =
      BrowserList::GetInstance(chrome::GetActiveDesktop());
  EXPECT_EQ(1U, browser_list->size());
  content::WindowedNotificationObserver window_close_observer(
      chrome::NOTIFICATION_BROWSER_CLOSED,
      content::Source<Browser>(browser()));

  AvatarMenuModel* model =
      ProfileChooserView::profile_bubble_->avatar_menu_model_.get();
  const AvatarMenuModel::Item& model_item_before =
      model->GetItemAt(model->GetActiveProfileIndex());
  EXPECT_FALSE(model_item_before.signin_required);

  ui::MouseEvent mouse_ev(ui::ET_MOUSE_RELEASED, gfx::Point(), gfx::Point(), 0);
  model->SetLogoutURL("about:blank");
  ProfileChooserView::profile_bubble_->ButtonPressed(
      static_cast<views::Button*>(
          ProfileChooserView::profile_bubble_->signout_current_profile_view_),
      mouse_ev);

  EXPECT_TRUE(model->GetItemAt(model->GetActiveProfileIndex()).signin_required);

  window_close_observer.Wait();  // Rely on test timeout for failure indication.
  EXPECT_TRUE(browser_list->empty());
}

IN_PROC_BROWSER_TEST_P(AvatarMenuButtonTest, LaunchUserManagerScreen) {
  if (!profiles::IsMultipleProfilesEnabled() ||
      !UsingNewProfileChooser()) {
    return;
  }

  CreateTestingProfile();
  StartAvatarMenu();

  int starting_tab_count = browser()->tab_strip_model()->count();
  BrowserList* browser_list =
      BrowserList::GetInstance(chrome::HOST_DESKTOP_TYPE_NATIVE);
  EXPECT_EQ(1U, browser_list->size());

  ui::MouseEvent mouse_ev(ui::ET_MOUSE_RELEASED, gfx::Point(), gfx::Point(), 0);
  ProfileChooserView::profile_bubble_->ButtonPressed(
      static_cast<views::Button*>(
          ProfileChooserView::profile_bubble_->users_button_view_),
      mouse_ev);

  // The user manager screen should go into a new, active tab.
  int final_tab_count = browser()->tab_strip_model()->count();
  EXPECT_EQ(1U, browser_list->size());
  EXPECT_EQ(starting_tab_count + 1, final_tab_count);

  GURL tab_url = browser()->tab_strip_model()->GetActiveWebContents()->GetURL();
  EXPECT_EQ(std::string(chrome::kChromeUIUserManagerURL), tab_url.spec());
}

INSTANTIATE_TEST_CASE_P(Old, AvatarMenuButtonTest, testing::Values(false));
INSTANTIATE_TEST_CASE_P(New, AvatarMenuButtonTest, testing::Values(true));
