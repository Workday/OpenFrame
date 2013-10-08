// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/file_util.h"
#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/platform_file.h"
#include "base/win/scoped_handle.h"
#include "chrome/installer/util/logging_installer.h"
#include "testing/gtest/include/gtest/gtest.h"

TEST(LoggingInstallerTest, TestTruncate) {
  const std::string test_data(installer::kMaxInstallerLogFileSize + 1, 'a');

  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  base::FilePath temp_file = temp_dir.path().Append(L"temp");
  EXPECT_EQ(test_data.size(),
            file_util::WriteFile(temp_file, &test_data[0], test_data.size()));
  ASSERT_TRUE(base::PathExists(temp_file));

  int64 file_size = 0;
  EXPECT_TRUE(file_util::GetFileSize(temp_file, &file_size));
  EXPECT_EQ(test_data.size(), file_size);

  EXPECT_EQ(installer::LOGFILE_TRUNCATED,
            installer::TruncateLogFileIfNeeded(temp_file));

  EXPECT_TRUE(file_util::GetFileSize(temp_file, &file_size));
  EXPECT_EQ(installer::kTruncatedInstallerLogFileSize , file_size);

  // Check that the temporary file was deleted.
  EXPECT_FALSE(base::PathExists(temp_file.Append(L".tmp")));
}

TEST(LoggingInstallerTest, TestTruncationNotNeeded) {
  const std::string test_data(installer::kMaxInstallerLogFileSize, 'a');

  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  base::FilePath temp_file = temp_dir.path().Append(L"temp");
  EXPECT_EQ(test_data.size(),
            file_util::WriteFile(temp_file, &test_data[0], test_data.size()));
  ASSERT_TRUE(base::PathExists(temp_file));

  int64 file_size = 0;
  EXPECT_TRUE(file_util::GetFileSize(temp_file, &file_size));
  EXPECT_EQ(test_data.size(), file_size);

  EXPECT_EQ(installer::LOGFILE_UNTOUCHED,
            installer::TruncateLogFileIfNeeded(temp_file));
  EXPECT_TRUE(base::PathExists(temp_file));
  EXPECT_TRUE(file_util::GetFileSize(temp_file, &file_size));
  EXPECT_EQ(test_data.size(), file_size);
}

TEST(LoggingInstallerTest, TestInUseNeedsTruncation) {
  const std::string test_data(installer::kMaxInstallerLogFileSize + 1, 'a');

  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  base::FilePath temp_file = temp_dir.path().Append(L"temp");
  EXPECT_EQ(test_data.size(),
            file_util::WriteFile(temp_file, &test_data[0], test_data.size()));
  ASSERT_TRUE(base::PathExists(temp_file));
  int64 file_size = 0;
  EXPECT_TRUE(file_util::GetFileSize(temp_file, &file_size));
  EXPECT_EQ(test_data.size(), file_size);

  // Prevent the log file from being moved or deleted.
  const int file_flags = base::PLATFORM_FILE_OPEN |
                         base::PLATFORM_FILE_READ |
                         base::PLATFORM_FILE_EXCLUSIVE_READ;
  base::win::ScopedHandle temp_platform_file(
      base::CreatePlatformFile(temp_file, file_flags, NULL, NULL));
  ASSERT_TRUE(temp_platform_file.IsValid());

  EXPECT_EQ(installer::LOGFILE_UNTOUCHED,
            installer::TruncateLogFileIfNeeded(temp_file));
  EXPECT_TRUE(base::PathExists(temp_file));
  EXPECT_TRUE(file_util::GetFileSize(temp_file, &file_size));
  EXPECT_EQ(test_data.size(), file_size);
}

TEST(LoggingInstallerTest, TestMoveFailsNeedsTruncation) {
  const std::string test_data(installer::kMaxInstallerLogFileSize + 1, 'a');

  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  base::FilePath temp_file = temp_dir.path().Append(L"temp");
  EXPECT_EQ(test_data.size(),
            file_util::WriteFile(temp_file, &test_data[0], test_data.size()));
  ASSERT_TRUE(base::PathExists(temp_file));
  int64 file_size = 0;
  EXPECT_TRUE(file_util::GetFileSize(temp_file, &file_size));
  EXPECT_EQ(test_data.size(), file_size);

  // Create an inconvenient, non-deletable file in the location that
  // TruncateLogFileIfNeeded would like to move the log file to.
  const int file_flags = base::PLATFORM_FILE_CREATE |
                         base::PLATFORM_FILE_READ |
                         base::PLATFORM_FILE_EXCLUSIVE_READ;
  base::FilePath temp_file_move_dest(
      temp_file.value() + FILE_PATH_LITERAL(".tmp"));
  base::win::ScopedHandle temp_move_destination_file(
      base::CreatePlatformFile(temp_file_move_dest, file_flags, NULL, NULL));
  ASSERT_TRUE(temp_move_destination_file.IsValid());

  EXPECT_EQ(installer::LOGFILE_DELETED,
            installer::TruncateLogFileIfNeeded(temp_file));
  EXPECT_FALSE(base::PathExists(temp_file));
}
