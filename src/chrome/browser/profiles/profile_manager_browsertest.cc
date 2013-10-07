// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/command_line.h"
#include "base/prefs/pref_service.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile_info_cache.h"
#include "chrome/browser/profiles/profile_info_cache_observer.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_window.h"
#include "chrome/browser/profiles/profiles_state.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/host_desktop.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/test_switches.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/ui_test_utils.h"

namespace {

// An observer that returns back to test code after a new profile is
// initialized.
void OnUnblockOnProfileCreation(Profile* profile,
                                Profile::CreateStatus status) {
  if (status == Profile::CREATE_STATUS_INITIALIZED)
    base::MessageLoop::current()->Quit();
}

void ProfileCreationComplete(Profile* profile, Profile::CreateStatus status) {
  ASSERT_NE(status, Profile::CREATE_STATUS_LOCAL_FAIL);
  ASSERT_NE(status, Profile::CREATE_STATUS_REMOTE_FAIL);
  // No browser should have been created for this profile yet.
  EXPECT_EQ(chrome::GetTotalBrowserCountForProfile(profile), 0U);
  EXPECT_EQ(chrome::GetTotalBrowserCount(), 1U);
  if (status == Profile::CREATE_STATUS_INITIALIZED)
    base::MessageLoop::current()->Quit();
}

class ProfileRemovalObserver : public ProfileInfoCacheObserver {
 public:
  ProfileRemovalObserver() {
    g_browser_process->profile_manager()->GetProfileInfoCache().AddObserver(
        this);
  }

  virtual ~ProfileRemovalObserver() {
    g_browser_process->profile_manager()->GetProfileInfoCache().RemoveObserver(
        this);
  }

  std::string last_used_profile_name() { return last_used_profile_name_; }

  // ProfileInfoCacheObserver overrides:
  virtual void OnProfileWillBeRemoved(
      const base::FilePath& profile_path) OVERRIDE {
    last_used_profile_name_ = g_browser_process->local_state()->GetString(
        prefs::kProfileLastUsed);
  }

 private:
  std::string last_used_profile_name_;

  DISALLOW_COPY_AND_ASSIGN(ProfileRemovalObserver);
};

} // namespace

// This file contains tests for the ProfileManager that require a heavyweight
// InProcessBrowserTest.  These include tests involving profile deletion.

// TODO(jeremy): crbug.com/103355 - These tests should be enabled on all
// platforms.
class ProfileManagerBrowserTest : public InProcessBrowserTest {
};

#if defined(OS_MACOSX)

// Delete single profile and make sure a new one is created.
IN_PROC_BROWSER_TEST_F(ProfileManagerBrowserTest, DeleteSingletonProfile) {
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  ProfileInfoCache& cache = profile_manager->GetProfileInfoCache();
  ProfileRemovalObserver observer;

  // We should start out with 1 profile.
  ASSERT_EQ(cache.GetNumberOfProfiles(), 1U);

  // Delete singleton profile.
  base::FilePath singleton_profile_path = cache.GetPathOfProfileAtIndex(0);
  EXPECT_FALSE(singleton_profile_path.empty());
  profile_manager->ScheduleProfileForDeletion(singleton_profile_path,
                                              ProfileManager::CreateCallback());

  // Spin things till profile is actually deleted.
  content::RunAllPendingInMessageLoop();

  // Make sure a new profile was created automatically.
  EXPECT_EQ(cache.GetNumberOfProfiles(), 1U);
  base::FilePath new_profile_path = cache.GetPathOfProfileAtIndex(0);
  EXPECT_NE(new_profile_path, singleton_profile_path);

  // Make sure that last used profile preference is set correctly.
  Profile* last_used = ProfileManager::GetLastUsedProfile();
  EXPECT_EQ(new_profile_path, last_used->GetPath());

  // Make sure the last used profile was set correctly before the notification
  // was sent.
  std::string last_used_profile_name =
      last_used->GetPath().BaseName().MaybeAsASCII();
  EXPECT_EQ(last_used_profile_name, observer.last_used_profile_name());
}

// Delete all profiles in a multi profile setup and make sure a new one is
// created.
// Crashes/CHECKs. See crbug.com/104851
IN_PROC_BROWSER_TEST_F(ProfileManagerBrowserTest, DISABLED_DeleteAllProfiles) {
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  ProfileInfoCache& cache = profile_manager->GetProfileInfoCache();

  // Create an additional profile.
  base::FilePath new_path = profile_manager->GenerateNextProfileDirectoryPath();
  profile_manager->CreateProfileAsync(new_path,
                                      base::Bind(&OnUnblockOnProfileCreation),
                                      string16(), string16(), std::string());

  // Spin to allow profile creation to take place, loop is terminated
  // by OnUnblockOnProfileCreation when the profile is created.
  content::RunMessageLoop();

  ASSERT_EQ(cache.GetNumberOfProfiles(), 2U);

  // Delete all profiles.
  base::FilePath profile_path1 = cache.GetPathOfProfileAtIndex(0);
  base::FilePath profile_path2 = cache.GetPathOfProfileAtIndex(1);
  EXPECT_FALSE(profile_path1.empty());
  EXPECT_FALSE(profile_path2.empty());
  profile_manager->ScheduleProfileForDeletion(profile_path1,
                                              ProfileManager::CreateCallback());
  profile_manager->ScheduleProfileForDeletion(profile_path2,
                                              ProfileManager::CreateCallback());

  // Spin things so deletion can take place.
  content::RunAllPendingInMessageLoop();

  // Make sure a new profile was created automatically.
  EXPECT_EQ(cache.GetNumberOfProfiles(), 1U);
  base::FilePath new_profile_path = cache.GetPathOfProfileAtIndex(0);
  EXPECT_NE(new_profile_path, profile_path1);
  EXPECT_NE(new_profile_path, profile_path2);

  // Make sure that last used profile preference is set correctly.
  Profile* last_used = ProfileManager::GetLastUsedProfile();
  EXPECT_EQ(new_profile_path, last_used->GetPath());
}
#endif  // OS_MACOSX

// Times out (http://crbug.com/159002)
IN_PROC_BROWSER_TEST_F(ProfileManagerBrowserTest,
                       DISABLED_CreateProfileWithCallback) {
  ProfileManager* profile_manager = g_browser_process->profile_manager();

  ASSERT_EQ(profile_manager->GetNumberOfProfiles(), 1U);
  EXPECT_EQ(chrome::GetTotalBrowserCount(), 1U);

  // Create a profile, make sure callback is invoked before any callbacks are
  // invoked (so they can do things like sign in the profile, etc).
  ProfileManager::CreateMultiProfileAsync(
      string16(), // name
      string16(), // icon url
      base::Bind(ProfileCreationComplete),
      std::string());
  // Wait for profile to finish loading.
  content::RunMessageLoop();
  EXPECT_EQ(profile_manager->GetNumberOfProfiles(), 2U);
  EXPECT_EQ(chrome::GetTotalBrowserCount(), 2U);

  // Now close all browser windows.
  std::vector<Profile*> profiles = profile_manager->GetLoadedProfiles();
  for (std::vector<Profile*>::const_iterator it = profiles.begin();
       it != profiles.end(); ++it) {
    BrowserList::CloseAllBrowsersWithProfile(*it);
  }
}

IN_PROC_BROWSER_TEST_F(ProfileManagerBrowserTest,
                       SwitchToProfile) {
#if defined(OS_WIN) && defined(USE_ASH)
  // Disable this test in Metro+Ash for now (http://crbug.com/262796).
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kAshBrowserTests))
    return;
#endif

  // If multiprofile mode is not enabled, you can't switch between profiles.
  if (!profiles::IsMultipleProfilesEnabled())
    return;

  ProfileManager* profile_manager = g_browser_process->profile_manager();
  ProfileInfoCache& cache = profile_manager->GetProfileInfoCache();
  base::FilePath path_profile1 = cache.GetPathOfProfileAtIndex(0);

  ASSERT_EQ(profile_manager->GetNumberOfProfiles(), 1U);
  EXPECT_EQ(chrome::GetTotalBrowserCount(), 1U);

  // Create an additional profile.
  base::FilePath path_profile2 =
      profile_manager->GenerateNextProfileDirectoryPath();
  profile_manager->CreateProfileAsync(path_profile2,
                                      base::Bind(&OnUnblockOnProfileCreation),
                                      string16(), string16(), std::string());

  // Spin to allow profile creation to take place, loop is terminated
  // by OnUnblockOnProfileCreation when the profile is created.
  content::RunMessageLoop();

  chrome::HostDesktopType desktop_type = chrome::GetActiveDesktop();
  BrowserList* browser_list = BrowserList::GetInstance(desktop_type);
  ASSERT_EQ(cache.GetNumberOfProfiles(), 2U);
  EXPECT_EQ(1U, browser_list->size());

  // Open a browser window for the first profile.
  profiles::SwitchToProfile(path_profile1, desktop_type, false);
  EXPECT_EQ(chrome::GetTotalBrowserCount(), 1U);
  EXPECT_EQ(1U, browser_list->size());
  EXPECT_EQ(path_profile1, browser_list->get(0)->profile()->GetPath());

  // Open a browser window for the second profile.
  profiles::SwitchToProfile(path_profile2, desktop_type, false);
  EXPECT_EQ(chrome::GetTotalBrowserCount(), 2U);
  EXPECT_EQ(2U, browser_list->size());
  EXPECT_EQ(path_profile2, browser_list->get(1)->profile()->GetPath());

  // Switch to the first profile without opening a new window.
  profiles::SwitchToProfile(path_profile1, desktop_type, false);
  EXPECT_EQ(chrome::GetTotalBrowserCount(), 2U);
  EXPECT_EQ(2U, browser_list->size());

  EXPECT_EQ(path_profile1, browser_list->get(0)->profile()->GetPath());
  EXPECT_EQ(path_profile2, browser_list->get(1)->profile()->GetPath());
}
