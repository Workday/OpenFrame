// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/app_restore_service.h"
#include "apps/app_restore_service_factory.h"
#include "apps/saved_files_service.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/api/file_system/file_system_api.h"
#include "chrome/browser/extensions/extension_prefs.h"
#include "chrome/browser/extensions/extension_test_message_listener.h"
#include "chrome/browser/extensions/platform_app_browsertest_util.h"
#include "chrome/common/extensions/extension.h"
#include "content/public/browser/notification_service.h"
#include "content/public/test/test_utils.h"

using extensions::Extension;
using extensions::ExtensionPrefs;
using extensions::ExtensionSystem;
using extensions::FileSystemChooseEntryFunction;

// TODO(benwells): Move PlatformAppBrowserTest to apps namespace in apps
// component.
using extensions::PlatformAppBrowserTest;

namespace apps {

// Tests that a running app is recorded in the preferences as such.
IN_PROC_BROWSER_TEST_F(PlatformAppBrowserTest, RunningAppsAreRecorded) {
  content::WindowedNotificationObserver extension_suspended(
      chrome::NOTIFICATION_EXTENSION_HOST_DESTROYED,
      content::NotificationService::AllSources());

  const Extension* extension = LoadExtension(
      test_data_dir_.AppendASCII("platform_apps/restart_test"));
  ASSERT_TRUE(extension);
  ExtensionPrefs* extension_prefs = ExtensionPrefs::Get(browser()->profile());

  // App is running.
  ASSERT_TRUE(extension_prefs->IsExtensionRunning(extension->id()));

  // Wait for the extension to get suspended.
  extension_suspended.Wait();

  // App isn't running because it got suspended.
  ASSERT_FALSE(extension_prefs->IsExtensionRunning(extension->id()));

  // Pretend that the app is supposed to be running.
  extension_prefs->SetExtensionRunning(extension->id(), true);

  ExtensionTestMessageListener restart_listener("onRestarted", false);
  apps::AppRestoreServiceFactory::GetForProfile(browser()->profile())->
      HandleStartup(true);
  restart_listener.WaitUntilSatisfied();
}

IN_PROC_BROWSER_TEST_F(PlatformAppBrowserTest, FileAccessIsSavedToPrefs) {
  content::WindowedNotificationObserver extension_suspended(
      chrome::NOTIFICATION_EXTENSION_HOST_DESTROYED,
      content::NotificationService::AllSources());

  base::ScopedTempDir temp_directory;
  ASSERT_TRUE(temp_directory.CreateUniqueTempDir());
  base::FilePath temp_file;
  ASSERT_TRUE(file_util::CreateTemporaryFileInDir(temp_directory.path(),
                                                  &temp_file));

  FileSystemChooseEntryFunction::SkipPickerAndAlwaysSelectPathForTest(
      &temp_file);
  FileSystemChooseEntryFunction::RegisterTempExternalFileSystemForTest(
      "temp", temp_directory.path());

  ExtensionTestMessageListener file_written_listener("fileWritten", false);
  ExtensionTestMessageListener access_ok_listener(
      "restartedFileAccessOK", false);

  const Extension* extension =
      LoadAndLaunchPlatformApp("file_access_saved_to_prefs_test");
  ASSERT_TRUE(extension);
  file_written_listener.WaitUntilSatisfied();

  SavedFilesService* saved_files_service = SavedFilesService::Get(profile());

  std::vector<SavedFileEntry> file_entries =
      saved_files_service->GetAllFileEntries(extension->id());
  // One for the read-only file entry and one for the writable file entry.
  ASSERT_EQ(2u, file_entries.size());

  extension_suspended.Wait();
  file_entries = saved_files_service->GetAllFileEntries(extension->id());
  // File entries should be cleared when the extension is suspended.
  ASSERT_TRUE(file_entries.empty());
}

// Flaky: crbug.com/269613
#if defined(OS_LINUX)
#define MAYBE_FileAccessIsRestored DISABLED_FileAccessIsRestored
#else
#define MAYBE_FileAccessIsRestored FileAccessIsRestored
#endif

IN_PROC_BROWSER_TEST_F(PlatformAppBrowserTest, MAYBE_FileAccessIsRestored) {
  content::WindowedNotificationObserver extension_suspended(
      chrome::NOTIFICATION_EXTENSION_HOST_DESTROYED,
      content::NotificationService::AllSources());

  base::ScopedTempDir temp_directory;
  ASSERT_TRUE(temp_directory.CreateUniqueTempDir());
  base::FilePath temp_file;
  ASSERT_TRUE(file_util::CreateTemporaryFileInDir(temp_directory.path(),
                                                  &temp_file));

  FileSystemChooseEntryFunction::SkipPickerAndAlwaysSelectPathForTest(
      &temp_file);
  FileSystemChooseEntryFunction::RegisterTempExternalFileSystemForTest(
      "temp", temp_directory.path());

  ExtensionTestMessageListener file_written_listener("fileWritten", false);
  ExtensionTestMessageListener access_ok_listener(
      "restartedFileAccessOK", false);

  const Extension* extension =
      LoadAndLaunchPlatformApp("file_access_restored_test");
  ASSERT_TRUE(extension);
  file_written_listener.WaitUntilSatisfied();

  ExtensionPrefs* extension_prefs =
      ExtensionPrefs::Get(browser()->profile());
  SavedFilesService* saved_files_service = SavedFilesService::Get(profile());
  std::vector<SavedFileEntry> file_entries =
      saved_files_service->GetAllFileEntries(extension->id());
  extension_suspended.Wait();

  // Simulate a restart by populating the preferences as if the browser didn't
  // get time to clean itself up.
  extension_prefs->SetExtensionRunning(extension->id(), true);
  for (std::vector<SavedFileEntry>::const_iterator it = file_entries.begin();
       it != file_entries.end(); ++it) {
    saved_files_service->RegisterFileEntry(
        extension->id(), it->id, it->path, it->writable);
  }

  apps::AppRestoreServiceFactory::GetForProfile(browser()->profile())->
      HandleStartup(true);

  access_ok_listener.WaitUntilSatisfied();
}

}  // namespace apps
