// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <windows.h>

#include "apps/app_host/binaries_installer.h"
#include "apps/app_host/update.h"
#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/process/launch.h"
#include "chrome/installer/launcher_support/chrome_launcher_support.h"

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, wchar_t*, int) {
  base::AtExitManager exit_manager;

  // Initialize the commandline singleton from the environment.
  CommandLine::Init(0, NULL);

  base::FilePath chrome_exe(chrome_launcher_support::GetAnyChromePath());
  if (chrome_exe.empty()) {
    LOG(INFO) << "No Chrome executable could be found. Let's install it.";
    HRESULT hr = app_host::InstallBinaries();
    if (FAILED(hr)) {
      LOG(ERROR) << "Failed to install the Chrome Binaries. Error: " << hr;
      return 1;
    } else {
      chrome_exe = chrome_launcher_support::GetAnyChromePath();
      if (chrome_exe.empty()) {
        LOG(ERROR) << "Failed to find the Chrome Binaries despite a "
                   << "'successful' installation.";
        return 1;
      }
    }
  }

  CommandLine chrome_exe_command_line(chrome_exe);
  chrome_exe_command_line.AppendArguments(
      *CommandLine::ForCurrentProcess(), false);
  // Launch Chrome before checking for update, for faster user experience.
  bool launch_result = base::LaunchProcess(chrome_exe_command_line,
                                           base::LaunchOptions(), NULL);
  if (launch_result)
    LOG(INFO) << "Delegated to Chrome executable at " << chrome_exe.value();
  else
    LOG(INFO) << "Failed to launch Chrome executable at " << chrome_exe.value();

  app_host::EnsureAppHostUpToDate();

  return !launch_result;
}
