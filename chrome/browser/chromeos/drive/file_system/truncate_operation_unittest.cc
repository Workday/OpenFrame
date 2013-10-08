// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/drive/file_system/truncate_operation.h"

#include "base/file_util.h"
#include "base/files/file_path.h"
#include "chrome/browser/chromeos/drive/drive.pb.h"
#include "chrome/browser/chromeos/drive/fake_free_disk_space_getter.h"
#include "chrome/browser/chromeos/drive/file_system/operation_test_base.h"
#include "chrome/browser/google_apis/test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace drive {
namespace file_system {

class TruncateOperationTest : public OperationTestBase {
 protected:
  virtual void SetUp() {
    OperationTestBase::SetUp();

    operation_.reset(new TruncateOperation(
        blocking_task_runner(), observer(), scheduler(),
        metadata(), cache(), temp_dir()));
  }

  scoped_ptr<TruncateOperation> operation_;
};

TEST_F(TruncateOperationTest, Truncate) {
  base::FilePath file_in_root(FILE_PATH_LITERAL("drive/root/File 1.txt"));
  ResourceEntry src_entry;
  ASSERT_EQ(FILE_ERROR_OK, GetLocalResourceEntry(file_in_root, &src_entry));
  const int64 file_size = src_entry.file_info().size();

  // Make sure the file has at least 2 bytes.
  ASSERT_GE(file_size, 2);

  FileError error = FILE_ERROR_FAILED;
  operation_->Truncate(
      file_in_root,
      1,  // Truncate to 1 byte.
      google_apis::test_util::CreateCopyResultCallback(&error));
  test_util::RunBlockingPoolTask();
  EXPECT_EQ(FILE_ERROR_OK, error);

  base::FilePath local_path;
  error = FILE_ERROR_FAILED;
  cache()->GetFileOnUIThread(
      src_entry.resource_id(),
      google_apis::test_util::CreateCopyResultCallback(&error, &local_path));
  test_util::RunBlockingPoolTask();
  ASSERT_EQ(FILE_ERROR_OK, error);

  // The local file should be truncated.
  int64 local_file_size = 0;
  file_util::GetFileSize(local_path, &local_file_size);
  EXPECT_EQ(1, local_file_size);
}

TEST_F(TruncateOperationTest, NegativeSize) {
  base::FilePath file_in_root(FILE_PATH_LITERAL("drive/root/File 1.txt"));
  ResourceEntry src_entry;
  ASSERT_EQ(FILE_ERROR_OK, GetLocalResourceEntry(file_in_root, &src_entry));
  const int64 file_size = src_entry.file_info().size();

  // Make sure the file has at least 2 bytes.
  ASSERT_GE(file_size, 2);

  FileError error = FILE_ERROR_FAILED;
  operation_->Truncate(
      file_in_root,
      -1,  // Truncate to "-1" byte.
      google_apis::test_util::CreateCopyResultCallback(&error));
  test_util::RunBlockingPoolTask();
  EXPECT_EQ(FILE_ERROR_INVALID_OPERATION, error);
}

TEST_F(TruncateOperationTest, HostedDocument) {
  base::FilePath file_in_root(FILE_PATH_LITERAL(
      "drive/root/Document 1 excludeDir-test.gdoc"));

  FileError error = FILE_ERROR_FAILED;
  operation_->Truncate(
      file_in_root,
      1,  // Truncate to 1 byte.
      google_apis::test_util::CreateCopyResultCallback(&error));
  test_util::RunBlockingPoolTask();
  EXPECT_EQ(FILE_ERROR_INVALID_OPERATION, error);
}

TEST_F(TruncateOperationTest, Extend) {
  base::FilePath file_in_root(FILE_PATH_LITERAL("drive/root/File 1.txt"));
  ResourceEntry src_entry;
  ASSERT_EQ(FILE_ERROR_OK, GetLocalResourceEntry(file_in_root, &src_entry));
  const int64 file_size = src_entry.file_info().size();

  FileError error = FILE_ERROR_FAILED;
  operation_->Truncate(
      file_in_root,
      file_size + 10,  // Extend to 10 bytes.
      google_apis::test_util::CreateCopyResultCallback(&error));
  test_util::RunBlockingPoolTask();
  EXPECT_EQ(FILE_ERROR_OK, error);

  base::FilePath local_path;
  error = FILE_ERROR_FAILED;
  cache()->GetFileOnUIThread(
      src_entry.resource_id(),
      google_apis::test_util::CreateCopyResultCallback(&error, &local_path));
  test_util::RunBlockingPoolTask();
  ASSERT_EQ(FILE_ERROR_OK, error);

  // The local file should be truncated.
  std::string content;
  ASSERT_TRUE(file_util::ReadFileToString(local_path, &content));

  EXPECT_EQ(file_size + 10, static_cast<int64>(content.size()));
  // All trailing 10 bytes should be '\0'.
  EXPECT_EQ(std::string(10, '\0'), content.substr(file_size));
}

}  // namespace file_system
}  // namespace drive
