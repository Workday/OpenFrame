// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/drive/file_system/get_file_for_saving_operation.h"

#include "base/callback.h"
#include "base/file_util.h"
#include "base/files/file_path.h"
#include "base/run_loop.h"
#include "chrome/browser/chromeos/drive/drive.pb.h"
#include "chrome/browser/chromeos/drive/file_errors.h"
#include "chrome/browser/chromeos/drive/file_system/operation_test_base.h"
#include "chrome/browser/chromeos/drive/file_write_watcher.h"
#include "chrome/browser/google_apis/test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace drive {
namespace file_system {

namespace {

// If OnCacheFileUploadNeededByOperation is called, records the resource ID and
// calls |quit_closure|.
class TestObserver : public OperationObserver {
 public:
  void set_quit_closure(const base::Closure& quit_closure) {
    quit_closure_ = quit_closure;
  }

  const std::string& observerd_resource_id() const {
    return observed_resource_id_;
  }

  // OperationObserver overrides.
  virtual void OnDirectoryChangedByOperation(
      const base::FilePath& path) OVERRIDE {}

  virtual void OnCacheFileUploadNeededByOperation(
      const std::string& resource_id) OVERRIDE {
    observed_resource_id_ = resource_id;
    quit_closure_.Run();
  }

 private:
  std::string observed_resource_id_;
  base::Closure quit_closure_;
};

}  // namespace

class GetFileForSavingOperationTest : public OperationTestBase {
 protected:
  // FileWriteWatcher requires TYPE_IO message loop to run.
  GetFileForSavingOperationTest()
      : OperationTestBase(content::TestBrowserThreadBundle::IO_MAINLOOP) {
  }

  virtual void SetUp() OVERRIDE {
    OperationTestBase::SetUp();

    operation_.reset(new GetFileForSavingOperation(
        blocking_task_runner(), &observer_, scheduler(), metadata(), cache(),
        temp_dir()));
    operation_->file_write_watcher_for_testing()->DisableDelayForTesting();
  }

  TestObserver observer_;
  scoped_ptr<GetFileForSavingOperation> operation_;
};

TEST_F(GetFileForSavingOperationTest, GetFileForSaving_Exist) {
  base::FilePath drive_path(FILE_PATH_LITERAL("drive/root/File 1.txt"));
  ResourceEntry src_entry;
  ASSERT_EQ(FILE_ERROR_OK, GetLocalResourceEntry(drive_path, &src_entry));

  // Run the operation.
  FileError error = FILE_ERROR_FAILED;
  scoped_ptr<ResourceEntry> entry;
  base::FilePath local_path;
  operation_->GetFileForSaving(
      drive_path,
      google_apis::test_util::CreateCopyResultCallback(
          &error, &local_path, &entry));
  test_util::RunBlockingPoolTask();

  // Checks that the file is retrieved.
  EXPECT_EQ(FILE_ERROR_OK, error);
  ASSERT_TRUE(entry);
  EXPECT_EQ(src_entry.resource_id(), entry->resource_id());

  // Checks that it presents in cache and marked dirty.
  bool success = false;
  FileCacheEntry cache_entry;
  cache()->GetCacheEntryOnUIThread(
      src_entry.resource_id(),
      google_apis::test_util::CreateCopyResultCallback(&success, &cache_entry));
  test_util::RunBlockingPoolTask();
  EXPECT_TRUE(success);
  EXPECT_TRUE(cache_entry.is_present());
  EXPECT_TRUE(cache_entry.is_dirty());

  // Write something to the cache and checks that the event is reported.
  {
    base::RunLoop run_loop;
    observer_.set_quit_closure(run_loop.QuitClosure());
    google_apis::test_util::WriteStringToFile(local_path, "hello");
    run_loop.Run();
    EXPECT_EQ(entry->resource_id(), observer_.observerd_resource_id());
  }
}

TEST_F(GetFileForSavingOperationTest, GetFileForSaving_NotExist) {
  base::FilePath drive_path(FILE_PATH_LITERAL("drive/root/NotExist.txt"));
  ResourceEntry src_entry;
  ASSERT_EQ(FILE_ERROR_NOT_FOUND,
            GetLocalResourceEntry(drive_path, &src_entry));

  // Run the operation.
  FileError error = FILE_ERROR_FAILED;
  scoped_ptr<ResourceEntry> entry;
  base::FilePath local_path;
  operation_->GetFileForSaving(
      drive_path,
      google_apis::test_util::CreateCopyResultCallback(
          &error, &local_path, &entry));
  test_util::RunBlockingPoolTask();

  // Checks that the file is created and retrieved.
  EXPECT_EQ(FILE_ERROR_OK, error);
  EXPECT_EQ(FILE_ERROR_OK, GetLocalResourceEntry(drive_path, &src_entry));
  int64 size = -1;
  EXPECT_TRUE(file_util::GetFileSize(local_path, &size));
  EXPECT_EQ(0, size);
}

TEST_F(GetFileForSavingOperationTest, GetFileForSaving_Directory) {
  base::FilePath drive_path(FILE_PATH_LITERAL("drive/root/Directory 1"));
  ResourceEntry src_entry;
  ASSERT_EQ(FILE_ERROR_OK, GetLocalResourceEntry(drive_path, &src_entry));
  ASSERT_TRUE(src_entry.file_info().is_directory());

  // Run the operation.
  FileError error = FILE_ERROR_FAILED;
  scoped_ptr<ResourceEntry> entry;
  base::FilePath local_path;
  operation_->GetFileForSaving(
      drive_path,
      google_apis::test_util::CreateCopyResultCallback(
          &error, &local_path, &entry));
  test_util::RunBlockingPoolTask();

  // Checks that an error is returned.
  EXPECT_EQ(FILE_ERROR_EXISTS, error);
}

}  // namespace file_system
}  // namespace drive
