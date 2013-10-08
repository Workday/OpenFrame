// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/base/test_launcher_utils.h"

#include "base/command_line.h"
#include "base/environment.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"

#if defined(USE_AURA)
#include "ui/views/corewm/corewm_switches.h"
#endif

namespace test_launcher_utils {

void PrepareBrowserCommandLineForTests(CommandLine* command_line) {
  // Turn off tip loading for tests; see http://crbug.com/17725.
  command_line->AppendSwitch(switches::kDisableWebResources);

  // Turn off preconnects because they break the brittle python webserver;
  // see http://crbug.com/60035.
  command_line->AppendSwitch(switches::kDisablePreconnect);

  // Don't show the first run ui.
  command_line->AppendSwitch(switches::kNoFirstRun);

  // No default browser check, it would create an info-bar (if we are not the
  // default browser) that could conflicts with some tests expectations.
  command_line->AppendSwitch(switches::kNoDefaultBrowserCheck);

  // Enable info level logging to stderr by default so that we can see when
  // bad stuff happens, but honor the flags specified from the command line.
  if (!command_line->HasSwitch(switches::kEnableLogging))
    command_line->AppendSwitchASCII(switches::kEnableLogging, "stderr");
  if (!command_line->HasSwitch(switches::kLoggingLevel))
    command_line->AppendSwitchASCII(switches::kLoggingLevel, "0");  // info

  // Disable safebrowsing autoupdate.
  command_line->AppendSwitch(switches::kSbDisableAutoUpdate);

  // Don't install default apps.
  command_line->AppendSwitch(switches::kDisableDefaultApps);

  // Don't collect GPU info, load GPU blacklist, or schedule a GPU blacklist
  // auto-update.
  command_line->AppendSwitch(switches::kSkipGpuDataLoading);

#if defined(USE_AURA)
  // Disable window animations under Ash as the animations effect the
  // coordinates returned and result in flake.
  command_line->AppendSwitch(
      views::corewm::switches::kWindowAnimationsDisabled);
#endif

#if defined(OS_POSIX) && !defined(OS_MACOSX) && !defined(OS_CHROMEOS)
  // Don't use the native password stores on Linux since they may
  // prompt for additional UI during tests and cause test failures or
  // timeouts.  Win, Mac and ChromeOS don't look at the kPasswordStore
  // switch.
  if (!command_line->HasSwitch(switches::kPasswordStore))
    command_line->AppendSwitchASCII(switches::kPasswordStore, "basic");
#endif

#if defined(OS_MACOSX)
  // Use mock keychain on mac to prevent blocking permissions dialogs.
  command_line->AppendSwitch(switches::kUseMockKeychain);
#endif

  command_line->AppendSwitch(switches::kDisableComponentUpdate);
}

bool OverrideUserDataDir(const base::FilePath& user_data_dir) {
  bool success = true;

  // PathService::Override() is the best way to change the user data directory.
  // This matches what is done in ChromeMain().
  success = PathService::Override(chrome::DIR_USER_DATA, user_data_dir);

#if defined(OS_POSIX) && !defined(OS_MACOSX)
  // Make sure the cache directory is inside our clear profile. Otherwise
  // the cache may contain data from earlier tests that could break the
  // current test.
  //
  // Note: we use an environment variable here, because we have to pass the
  // value to the child process. This is the simplest way to do it.
  scoped_ptr<base::Environment> env(base::Environment::Create());
  success = success && env->SetVar("XDG_CACHE_HOME", user_data_dir.value());
#endif

  return success;
}

}  // namespace test_launcher_utils
