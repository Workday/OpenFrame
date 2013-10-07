// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/diagnostics/diagnostics_controller.h"

#include "base/command_line.h"
#include "base/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/scoped_ptr.h"
#include "base/path_service.h"
#include "chrome/browser/diagnostics/diagnostics_model.h"
#include "chrome/browser/diagnostics/diagnostics_writer.h"
#include "chrome/browser/diagnostics/sqlite_diagnostics.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chromeos/chromeos_constants.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace diagnostics {

// Basic harness to acquire and release the required temporary environment to
// run a test in.
class DiagnosticsControllerTest : public testing::Test {
 protected:
  DiagnosticsControllerTest() : cmdline_(CommandLine::NO_PROGRAM) {}

  virtual ~DiagnosticsControllerTest() {}

  virtual void SetUp() {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base::FilePath test_data;
    PathService::Get(chrome::DIR_TEST_DATA, &test_data);
    test_data = test_data.Append(FILE_PATH_LITERAL("diagnostics"));
    test_data = test_data.Append(FILE_PATH_LITERAL("user"));
    base::CopyDirectory(test_data, temp_dir_.path(), true);
    profile_dir_ = temp_dir_.path().Append(FILE_PATH_LITERAL("user"));

#if defined(OS_CHROMEOS)
    // Redirect the home dir to the profile directory. We have to do this
    // because NSS uses the HOME directory to find where to store it's database,
    // so that's where the diagnostics and recovery code looks for it.

    // Preserve existing home directory setting, if any.
    const char* old_home_dir = ::getenv("HOME");
    if (old_home_dir)
      old_home_dir_ = old_home_dir;
    else
      old_home_dir_.clear();
    ::setenv("HOME", profile_dir_.value().c_str(), 1);
#endif

    cmdline_ = CommandLine(CommandLine::NO_PROGRAM);
    cmdline_.AppendSwitchPath(switches::kUserDataDir, profile_dir_);
    cmdline_.AppendSwitch(switches::kDiagnostics);
    cmdline_.AppendSwitch(switches::kDiagnosticsRecovery);
    writer_.reset();
    // Use this writer instead, if you want to see the diagnostics output.
    // writer_.reset(new DiagnosticsWriter(DiagnosticsWriter::MACHINE));
  }

  virtual void TearDown() {
    DiagnosticsController::GetInstance()->ClearResults();
#if defined(OS_CHROMEOS)
    if (!old_home_dir_.empty()) {
      ::setenv("HOME", old_home_dir_.c_str(), 1);
    } else {
      ::unsetenv("HOME");
    }
    old_home_dir_.clear();
#endif
  }

  void CorruptDataFile(const base::FilePath& path) {
    // Just write some random characters into the file tInvaludUsero "corrupt"
    // it.
    const char bogus_data[] = "wwZ2uNYNuyUVzFbDm3DL";
    file_util::WriteFile(path, bogus_data, arraysize(bogus_data));
  }

  scoped_ptr<DiagnosticsModel> model_;
  CommandLine cmdline_;
  base::ScopedTempDir temp_dir_;
  scoped_ptr<DiagnosticsWriter> writer_;
  base::FilePath profile_dir_;

#if defined(OS_CHROMEOS)
  std::string old_home_dir_;
#endif

  DISALLOW_COPY_AND_ASSIGN(DiagnosticsControllerTest);
};

TEST_F(DiagnosticsControllerTest, Diagnostics) {
  DiagnosticsController::GetInstance()->Run(cmdline_, writer_.get());
  EXPECT_TRUE(DiagnosticsController::GetInstance()->HasResults());
  const DiagnosticsModel& results =
      DiagnosticsController::GetInstance()->GetResults();
  EXPECT_EQ(results.GetTestRunCount(), results.GetTestAvailableCount());
  EXPECT_EQ(DiagnosticsModel::kDiagnosticsTestCount, results.GetTestRunCount());
  for (int i = 0; i < results.GetTestRunCount(); ++i) {
    const DiagnosticsModel::TestInfo& info(results.GetTest(i));
    EXPECT_EQ(DiagnosticsModel::TEST_OK, info.GetResult()) << "Test: "
                                                           << info.GetId();
  }
}

TEST_F(DiagnosticsControllerTest, RecoverAllOK) {
  DiagnosticsController::GetInstance()->Run(cmdline_, writer_.get());
  DiagnosticsController::GetInstance()->RunRecovery(cmdline_, writer_.get());
  EXPECT_TRUE(DiagnosticsController::GetInstance()->HasResults());
  const DiagnosticsModel& results =
      DiagnosticsController::GetInstance()->GetResults();
  EXPECT_EQ(results.GetTestRunCount(), results.GetTestAvailableCount());
  EXPECT_EQ(DiagnosticsModel::kDiagnosticsTestCount, results.GetTestRunCount());
  for (int i = 0; i < results.GetTestRunCount(); ++i) {
    const DiagnosticsModel::TestInfo& info(results.GetTest(i));
    EXPECT_EQ(DiagnosticsModel::RECOVERY_OK, info.GetResult()) << "Test: "
                                                               << info.GetId();
  }
}

#if defined(OS_CHROMEOS)
TEST_F(DiagnosticsControllerTest, RecoverFromNssCertDbFailure) {
  base::FilePath db_path = profile_dir_.Append(chromeos::kNssCertDbPath);
  EXPECT_TRUE(base::PathExists(db_path));
  CorruptDataFile(db_path);
  DiagnosticsController::GetInstance()->Run(cmdline_, writer_.get());
  ASSERT_TRUE(DiagnosticsController::GetInstance()->HasResults());
  const DiagnosticsModel& results =
      DiagnosticsController::GetInstance()->GetResults();
  EXPECT_EQ(results.GetTestRunCount(), results.GetTestAvailableCount());
  EXPECT_EQ(DiagnosticsModel::kDiagnosticsTestCount, results.GetTestRunCount());

  const DiagnosticsModel::TestInfo* info = NULL;
  EXPECT_TRUE(results.GetTestInfo(kSQLiteIntegrityNSSCertTest, &info));
  EXPECT_EQ(DiagnosticsModel::TEST_FAIL_CONTINUE, info->GetResult());
  EXPECT_EQ(DIAG_SQLITE_ERROR_HANDLER_CALLED, info->GetOutcomeCode());

  DiagnosticsController::GetInstance()->RunRecovery(cmdline_, writer_.get());
  EXPECT_EQ(DiagnosticsModel::RECOVERY_OK, info->GetResult());
  EXPECT_FALSE(base::PathExists(db_path));
}

TEST_F(DiagnosticsControllerTest, RecoverFromNssKeyDbFailure) {
  base::FilePath db_path = profile_dir_.Append(chromeos::kNssKeyDbPath);
  EXPECT_TRUE(base::PathExists(db_path));
  CorruptDataFile(db_path);
  DiagnosticsController::GetInstance()->Run(cmdline_, writer_.get());
  ASSERT_TRUE(DiagnosticsController::GetInstance()->HasResults());
  const DiagnosticsModel& results =
      DiagnosticsController::GetInstance()->GetResults();
  EXPECT_EQ(results.GetTestRunCount(), results.GetTestAvailableCount());
  EXPECT_EQ(DiagnosticsModel::kDiagnosticsTestCount, results.GetTestRunCount());

  const DiagnosticsModel::TestInfo* info = NULL;
  EXPECT_TRUE(results.GetTestInfo(kSQLiteIntegrityNSSKeyTest, &info));
  EXPECT_EQ(DiagnosticsModel::TEST_FAIL_CONTINUE, info->GetResult());
  EXPECT_EQ(DIAG_SQLITE_ERROR_HANDLER_CALLED, info->GetOutcomeCode());

  DiagnosticsController::GetInstance()->RunRecovery(cmdline_, writer_.get());
  EXPECT_EQ(DiagnosticsModel::RECOVERY_OK, info->GetResult());
  EXPECT_FALSE(base::PathExists(db_path));
}
#endif

}  // namespace diagnostics
