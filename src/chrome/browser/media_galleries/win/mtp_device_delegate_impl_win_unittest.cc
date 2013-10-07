// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/stl_util.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/extensions/test_extension_system.h"
#include "chrome/browser/media_galleries/media_file_system_registry.h"
#include "chrome/browser/media_galleries/media_galleries_test_util.h"
#include "chrome/browser/storage_monitor/storage_info.h"
#include "chrome/browser/storage_monitor/storage_monitor.h"
#include "chrome/browser/storage_monitor/test_portable_device_watcher_win.h"
#include "chrome/browser/storage_monitor/test_storage_monitor.h"
#include "chrome/browser/storage_monitor/test_storage_monitor_win.h"
#include "chrome/browser/storage_monitor/test_volume_mount_watcher_win.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace chrome {

namespace {

typedef std::map<MediaGalleryPrefId, MediaFileSystemInfo> FSInfoMap;

void GetGalleryInfoCallback(
    FSInfoMap* results,
    const std::vector<MediaFileSystemInfo>& file_systems) {
  for (size_t i = 0; i < file_systems.size(); ++i) {
    ASSERT_FALSE(ContainsKey(*results, file_systems[i].pref_id));
    (*results)[file_systems[i].pref_id] = file_systems[i];
  }
}

}  // namespace

class MTPDeviceDelegateImplWinTest : public ChromeRenderViewHostTestHarness {
 protected:
  void SetUp() OVERRIDE;
  void TearDown() OVERRIDE;

  void ProcessAttach(const std::string& id,
                     const string16& name,
                     const base::FilePath::StringType& location);
  std::string AttachDevice(StorageInfo::Type type,
                           const std::string& unique_id,
                           const base::FilePath& location);
  void CheckGalleryInfo(const MediaFileSystemInfo& info,
                        const string16& name,
                        const base::FilePath& path,
                        bool removable,
                        bool media_device);

  // Pointer to the storage monitor. Owned by TestingBrowserProcess.
  test::TestStorageMonitorWin* monitor_;
  scoped_refptr<extensions::Extension> extension_;

  EnsureMediaDirectoriesExists media_directories_;
};

void MTPDeviceDelegateImplWinTest::SetUp() {
  ChromeRenderViewHostTestHarness::SetUp();

  test::TestStorageMonitor::RemoveSingleton();
  test::TestPortableDeviceWatcherWin* portable_device_watcher =
      new test::TestPortableDeviceWatcherWin;
  test::TestVolumeMountWatcherWin* mount_watcher =
      new test::TestVolumeMountWatcherWin;
  portable_device_watcher->set_use_dummy_mtp_storage_info(true);
  scoped_ptr<test::TestStorageMonitorWin> monitor(
      new test::TestStorageMonitorWin(
          mount_watcher, portable_device_watcher));
  TestingBrowserProcess* browser_process = TestingBrowserProcess::GetGlobal();
  DCHECK(browser_process);
  monitor_ = monitor.get();
  browser_process->SetStorageMonitor(monitor.Pass());

  base::RunLoop runloop;
  monitor_->EnsureInitialized(runloop.QuitClosure());
  runloop.Run();

  extensions::TestExtensionSystem* extension_system(
      static_cast<extensions::TestExtensionSystem*>(
          extensions::ExtensionSystem::Get(profile())));
  extension_system->CreateExtensionService(
      CommandLine::ForCurrentProcess(), base::FilePath(), false);

  std::vector<std::string> all_permissions;
  all_permissions.push_back("allAutoDetected");
  all_permissions.push_back("read");
  extension_ = AddMediaGalleriesApp("all", all_permissions, profile());
}

void MTPDeviceDelegateImplWinTest::TearDown() {
  // Windows storage monitor must be destroyed on the same thread
  // as construction.
  test::TestStorageMonitor::RemoveSingleton();

  ChromeRenderViewHostTestHarness::TearDown();
}

void MTPDeviceDelegateImplWinTest::ProcessAttach(
    const std::string& id,
    const string16& label,
    const base::FilePath::StringType& location) {
  StorageInfo info(id, string16(), location, label, string16(), string16(), 0);
  monitor_->receiver()->ProcessAttach(info);
}

std::string MTPDeviceDelegateImplWinTest::AttachDevice(
    StorageInfo::Type type,
    const std::string& unique_id,
    const base::FilePath& location) {
  std::string device_id = StorageInfo::MakeDeviceId(type, unique_id);
  DCHECK(StorageInfo::IsRemovableDevice(device_id));
  string16 label = location.LossyDisplayName();
  ProcessAttach(device_id, label, location.value());
  base::RunLoop().RunUntilIdle();
  return device_id;
}

void MTPDeviceDelegateImplWinTest::CheckGalleryInfo(
    const MediaFileSystemInfo& info,
    const string16& name,
    const base::FilePath& path,
    bool removable,
    bool media_device) {
  EXPECT_EQ(name, info.name);
  EXPECT_EQ(path, info.path);
  EXPECT_EQ(removable, info.removable);
  EXPECT_EQ(media_device, info.media_device);
  EXPECT_NE(0UL, info.pref_id);

  if (removable)
    EXPECT_NE(0UL, info.transient_device_id.size());
  else
    EXPECT_EQ(0UL, info.transient_device_id.size());
}

TEST_F(MTPDeviceDelegateImplWinTest, GalleryNameMTP) {
  base::FilePath location(
      PortableDeviceWatcherWin::GetStoragePathFromStorageId(
          test::TestPortableDeviceWatcherWin::kStorageUniqueIdA));
  AttachDevice(StorageInfo::MTP_OR_PTP, "mtp_fake_id", location);

  content::RenderViewHost* rvh = web_contents()->GetRenderViewHost();
  FSInfoMap results;
  MediaFileSystemRegistry* registry =
      g_browser_process->media_file_system_registry();
  registry->GetMediaFileSystemsForExtension(
      rvh, extension_.get(),
      base::Bind(&GetGalleryInfoCallback, base::Unretained(&results)));
  base::RunLoop().RunUntilIdle();

  ASSERT_EQ(media_directories_.num_galleries() + 1, results.size());
  bool checked = false;
  for (FSInfoMap::iterator i = results.begin(); i != results.end(); ++i) {
    MediaFileSystemInfo info = i->second;
    if (info.path == location) {
      CheckGalleryInfo(info, location.LossyDisplayName(), location, true, true);
      checked = true;
      break;
    }
  }
  EXPECT_TRUE(checked);
}

}  // namespace chrome
