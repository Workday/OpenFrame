// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/message_loop/message_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "base/synchronization/waitable_event.h"
#include "chrome/browser/storage_monitor/media_storage_util.h"
#include "chrome/browser/storage_monitor/removable_device_constants.h"
#include "chrome/browser/storage_monitor/storage_monitor.h"
#include "chrome/browser/storage_monitor/test_storage_monitor.h"
#include "chrome/test/base/testing_browser_process.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/test/test_browser_thread.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace chrome {

namespace {

const char kImageCaptureDeviceId[] = "ic:xyz";

}  // namespace

using content::BrowserThread;

class MediaStorageUtilTest : public testing::Test {
 public:
  MediaStorageUtilTest()
        : ui_thread_(BrowserThread::UI, &message_loop_),
          file_thread_(BrowserThread::FILE) {}
  virtual ~MediaStorageUtilTest() { }

  // Verify mounted device type.
  void CheckDeviceType(const base::FilePath& mount_point,
                       bool expected_val) {
    if (expected_val)
      EXPECT_TRUE(MediaStorageUtil::HasDcim(mount_point));
    else
      EXPECT_FALSE(MediaStorageUtil::HasDcim(mount_point));
  }

  void ProcessAttach(const std::string& id,
                     const string16& name,
                     const base::FilePath::StringType& location) {
    StorageInfo info(id, name, location, string16(), string16(), string16(), 0);
    monitor_->receiver()->ProcessAttach(info);
  }

 protected:
  // Create mount point for the test device.
  base::FilePath CreateMountPoint(bool create_dcim_dir) {
    base::FilePath path(scoped_temp_dir_.path());
    if (create_dcim_dir)
      path = path.Append(kDCIMDirectoryName);
    if (!file_util::CreateDirectory(path))
      return base::FilePath();
    return scoped_temp_dir_.path();
  }

  virtual void SetUp() OVERRIDE {
    monitor_ = chrome::test::TestStorageMonitor::CreateAndInstall();
    ASSERT_TRUE(scoped_temp_dir_.CreateUniqueTempDir());
    file_thread_.Start();
  }

  virtual void TearDown() OVERRIDE {
    WaitForFileThread();
  }

  static void PostQuitToUIThread() {
    BrowserThread::PostTask(BrowserThread::UI,
                            FROM_HERE,
                            base::MessageLoop::QuitClosure());
  }

  static void WaitForFileThread() {
    BrowserThread::PostTask(BrowserThread::FILE,
                            FROM_HERE,
                            base::Bind(&PostQuitToUIThread));
    base::MessageLoop::current()->Run();
  }

  base::MessageLoop message_loop_;

 private:
  chrome::test::TestStorageMonitor* monitor_;
  content::TestBrowserThread ui_thread_;
  content::TestBrowserThread file_thread_;
  base::ScopedTempDir scoped_temp_dir_;
};

// Test to verify that HasDcim() function returns true for the given media
// device mount point.
TEST_F(MediaStorageUtilTest, MediaDeviceAttached) {
  // Create a dummy mount point with DCIM Directory.
  base::FilePath mount_point(CreateMountPoint(true));
  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      base::Bind(&MediaStorageUtilTest::CheckDeviceType,
                 base::Unretained(this), mount_point, true));
  message_loop_.RunUntilIdle();
}

// Test to verify that HasDcim() function returns false for a given non-media
// device mount point.
TEST_F(MediaStorageUtilTest, NonMediaDeviceAttached) {
  // Create a dummy mount point without DCIM Directory.
  base::FilePath mount_point(CreateMountPoint(false));
  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      base::Bind(&MediaStorageUtilTest::CheckDeviceType,
                 base::Unretained(this), mount_point, false));
  message_loop_.RunUntilIdle();
}

TEST_F(MediaStorageUtilTest, CanCreateFileSystemForImageCapture) {
  EXPECT_TRUE(MediaStorageUtil::CanCreateFileSystem(kImageCaptureDeviceId,
                                                    base::FilePath()));
  EXPECT_FALSE(MediaStorageUtil::CanCreateFileSystem(
      "dcim:xyz", base::FilePath(FILE_PATH_LITERAL("relative"))));
  EXPECT_FALSE(MediaStorageUtil::CanCreateFileSystem(
      "dcim:xyz", base::FilePath(FILE_PATH_LITERAL("../refparent"))));
}

TEST_F(MediaStorageUtilTest, DetectDeviceFiltered) {
  MediaStorageUtil::DeviceIdSet devices;
  devices.insert(kImageCaptureDeviceId);

  base::WaitableEvent event(true, false);
  base::Closure signal_event =
      base::Bind(&base::WaitableEvent::Signal, base::Unretained(&event));

  // We need signal_event to be executed on the FILE thread, as the test thread
  // is blocked. Therefore, we invoke FilterAttachedDevices on the FILE thread.
  BrowserThread::PostTask(BrowserThread::FILE, FROM_HERE,
                          base::Bind(&MediaStorageUtil::FilterAttachedDevices,
                                     base::Unretained(&devices), signal_event));
  event.Wait();
  EXPECT_FALSE(devices.find(kImageCaptureDeviceId) != devices.end());

  ProcessAttach(kImageCaptureDeviceId, ASCIIToUTF16("name"),
                FILE_PATH_LITERAL("/location"));
  devices.insert(kImageCaptureDeviceId);
  event.Reset();
  BrowserThread::PostTask(BrowserThread::FILE, FROM_HERE,
                          base::Bind(&MediaStorageUtil::FilterAttachedDevices,
                                     base::Unretained(&devices), signal_event));
  event.Wait();

  EXPECT_TRUE(devices.find(kImageCaptureDeviceId) != devices.end());
}

}  // namespace chrome
