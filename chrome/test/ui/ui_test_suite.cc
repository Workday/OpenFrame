// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/ui/ui_test_suite.h"

#include <string>

#include "base/command_line.h"
#include "base/environment.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/path_service.h"
#include "base/process/kill.h"
#include "base/process/launch.h"
#include "base/process/process_iterator.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/env_vars.h"
#include "chrome/test/base/chrome_unit_test_suite.h"

UITestSuite::UITestSuite(int argc, char** argv) : ChromeTestSuite(argc, argv) {
#if defined(OS_WIN)
  crash_service_ = NULL;
#endif
}

void UITestSuite::Initialize() {
  ChromeTestSuite::Initialize();
  ChromeUnitTestSuite::InitializeProviders();
  ChromeUnitTestSuite::InitializeResourceBundle();
#if defined(OS_WIN)
  LoadCrashService();
#endif
}

void UITestSuite::Shutdown() {
#if defined(OS_WIN)
  if (crash_service_)
    base::KillProcess(crash_service_, 0, false);
  job_handle_.Close();
#endif
  ChromeTestSuite::Shutdown();
}

#if defined(OS_WIN)
void UITestSuite::LoadCrashService() {
  scoped_ptr<base::Environment> env(base::Environment::Create());
  if (env->HasVar(env_vars::kHeadless))
    return;

  if (base::GetProcessCount(L"crash_service.exe", NULL))
    return;

  job_handle_.Set(CreateJobObject(NULL, NULL));
  if (!job_handle_.IsValid()) {
    LOG(ERROR) << "Could not create JobObject.";
    return;
  }

  if (!base::SetJobObjectLimitFlags(job_handle_.Get(),
                                    JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE)) {
    LOG(ERROR) << "Could not SetJobObjectLimitFlags.";
    return;
  }

  base::FilePath exe_dir;
  if (!PathService::Get(base::DIR_EXE, &exe_dir)) {
    LOG(ERROR) << "Failed to get path to DIR_EXE, "
               << "not starting crash_service.exe!";
    return;
  }

  base::LaunchOptions launch_options;
  launch_options.job_handle = job_handle_.Get();
  base::FilePath crash_service = exe_dir.Append(L"crash_service.exe");
  base::win::ScopedHandle crash_service_handle;
  if (!base::LaunchProcess(crash_service.value(), base::LaunchOptions(),
                           &crash_service_handle)) {
    LOG(ERROR) << "Couldn't start crash_service.exe, so this ui_tests run "
               << "won't tell you if any test crashes!";
    return;
  }
  crash_service_ = crash_service_handle.Take();
}
#endif
