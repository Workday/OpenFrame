// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/first_run/first_run_internal.h"

#include <windows.h>
#include <shellapi.h>

#include "base/base_paths.h"
#include "base/callback.h"
#include "base/command_line.h"
#include "base/file_util.h"
#include "base/files/file_path.h"
#include "base/path_service.h"
#include "base/process/kill.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/threading/sequenced_worker_pool.h"
#include "base/time/time.h"
#include "base/win/metro.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/installer/util/google_update_settings.h"
#include "chrome/installer/util/install_util.h"
#include "chrome/installer/util/master_preferences.h"
#include "chrome/installer/util/master_preferences_constants.h"
#include "chrome/installer/util/util_constants.h"
#include "content/public/browser/browser_thread.h"
#include "grit/locale_settings.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/win/shell.h"

namespace {

// Launches the setup exe with the given parameter/value on the command-line.
// For non-metro Windows, it waits for its termination, returns its exit code
// in |*ret_code|, and returns true if the exit code is valid.
// For metro Windows, it launches setup via ShellExecuteEx and returns in order
// to bounce the user back to the desktop, then returns immediately.
bool LaunchSetupForEula(const base::FilePath::StringType& value,
                        int* ret_code) {
  base::FilePath exe_dir;
  if (!PathService::Get(base::DIR_MODULE, &exe_dir))
    return false;
  exe_dir = exe_dir.Append(installer::kInstallerDir);
  base::FilePath exe_path = exe_dir.Append(installer::kSetupExe);
  base::ProcessHandle ph;

  CommandLine cl(CommandLine::NO_PROGRAM);
  cl.AppendSwitchNative(installer::switches::kShowEula, value);

  CommandLine* browser_command_line = CommandLine::ForCurrentProcess();
  if (browser_command_line->HasSwitch(switches::kChromeFrame)) {
    cl.AppendSwitch(switches::kChromeFrame);
  }

  if (base::win::IsMetroProcess()) {
    cl.AppendSwitch(installer::switches::kShowEulaForMetro);

    // This obscure use of the 'log usage' mask for windows 8 is documented here
    // http://go.microsoft.com/fwlink/?LinkID=243079. It causes the desktop
    // process to receive focus. Pass SEE_MASK_FLAG_NO_UI to avoid hangs if an
    // error occurs since the UI can't be shown from a metro process.
    ui::win::OpenAnyViaShell(exe_path.value(),
                             exe_dir.value(),
                             cl.GetCommandLineString(),
                             SEE_MASK_FLAG_LOG_USAGE | SEE_MASK_FLAG_NO_UI);
    return false;
  } else {
    CommandLine setup_path(exe_path);
    setup_path.AppendArguments(cl, false);

    int exit_code = 0;
    if (!base::LaunchProcess(setup_path, base::LaunchOptions(), &ph) ||
        !base::WaitForExitCode(ph, &exit_code)) {
      return false;
    }

    *ret_code = exit_code;
    return true;
  }
}

// Populates |path| with the path to |file| in the sentinel directory. This is
// the application directory for user-level installs, and the default user data
// dir for system-level installs. Returns false on error.
bool GetSentinelFilePath(const wchar_t* file, base::FilePath* path) {
  base::FilePath exe_path;
  if (!PathService::Get(base::DIR_EXE, &exe_path))
    return false;
  if (InstallUtil::IsPerUserInstall(exe_path.value().c_str()))
    *path = exe_path;
  else if (!PathService::Get(chrome::DIR_USER_DATA, path))
    return false;
  *path = path->Append(file);
  return true;
}

bool GetEULASentinelFilePath(base::FilePath* path) {
  return GetSentinelFilePath(installer::kEULASentinelFile, path);
}

// Returns true if the EULA is required but has not been accepted by this user.
// The EULA is considered having been accepted if the user has gotten past
// first run in the "other" environment (desktop or metro).
bool IsEULANotAccepted(installer::MasterPreferences* install_prefs) {
  bool value = false;
  if (install_prefs->GetBool(installer::master_preferences::kRequireEula,
          &value) && value) {
    base::FilePath eula_sentinel;
    // Be conservative and show the EULA if the path to the sentinel can't be
    // determined.
    if (!GetEULASentinelFilePath(&eula_sentinel) ||
        !base::PathExists(eula_sentinel)) {
      return true;
    }
  }
  return false;
}

// Writes the EULA to a temporary file, returned in |*eula_path|, and returns
// true if successful.
bool WriteEULAtoTempFile(base::FilePath* eula_path) {
  std::string terms = l10n_util::GetStringUTF8(IDS_TERMS_HTML);
  return (!terms.empty() &&
          file_util::CreateTemporaryFile(eula_path) &&
          file_util::WriteFile(*eula_path, terms.data(), terms.size()) != -1);
}

// Creates the sentinel indicating that the EULA was required and has been
// accepted.
bool CreateEULASentinel() {
  base::FilePath eula_sentinel;
  if (!GetEULASentinelFilePath(&eula_sentinel))
    return false;

  return (file_util::CreateDirectory(eula_sentinel.DirName()) &&
          file_util::WriteFile(eula_sentinel, "", 0) != -1);
}

}  // namespace

namespace first_run {
namespace internal {

void DoPostImportPlatformSpecificTasks(Profile* /* profile */) {
  // Trigger the Active Setup command for system-level Chromes to finish
  // configuring this user's install (e.g. per-user shortcuts).
  // Delay the task slightly to give Chrome launch I/O priority while also
  // making sure shortcuts are created promptly to avoid annoying the user by
  // re-creating shortcuts he previously deleted.
  static const int64 kTiggerActiveSetupDelaySeconds = 5;
  base::FilePath chrome_exe;
  if (!PathService::Get(base::FILE_EXE, &chrome_exe)) {
    NOTREACHED();
  } else if (!InstallUtil::IsPerUserInstall(chrome_exe.value().c_str())) {
    content::BrowserThread::GetBlockingPool()->PostDelayedTask(
        FROM_HERE,
        base::Bind(&InstallUtil::TriggerActiveSetupCommand),
        base::TimeDelta::FromSeconds(kTiggerActiveSetupDelaySeconds));
  }
}

bool GetFirstRunSentinelFilePath(base::FilePath* path) {
  return GetSentinelFilePath(chrome::kFirstRunSentinel, path);
}

bool ShowPostInstallEULAIfNeeded(installer::MasterPreferences* install_prefs) {
  if (IsEULANotAccepted(install_prefs)) {
    // Show the post-installation EULA. This is done by setup.exe and the
    // result determines if we continue or not. We wait here until the user
    // dismisses the dialog.

    // The actual eula text is in a resource in chrome. We extract it to
    // a text file so setup.exe can use it as an inner frame.
    base::FilePath inner_html;
    if (WriteEULAtoTempFile(&inner_html)) {
      int retcode = 0;
      if (!LaunchSetupForEula(inner_html.value(), &retcode) ||
          (retcode != installer::EULA_ACCEPTED &&
           retcode != installer::EULA_ACCEPTED_OPT_IN)) {
        LOG(WARNING) << "EULA flow requires fast exit.";
        return false;
      }
      CreateEULASentinel();

      if (retcode == installer::EULA_ACCEPTED) {
        VLOG(1) << "EULA : no collection";
        GoogleUpdateSettings::SetCollectStatsConsent(false);
      } else if (retcode == installer::EULA_ACCEPTED_OPT_IN) {
        VLOG(1) << "EULA : collection consent";
        GoogleUpdateSettings::SetCollectStatsConsent(true);
      }
    }
  }
  return true;
}

base::FilePath MasterPrefsPath() {
  // The standard location of the master prefs is next to the chrome binary.
  base::FilePath master_prefs;
  if (!PathService::Get(base::DIR_EXE, &master_prefs))
    return base::FilePath();
  return master_prefs.AppendASCII(installer::kDefaultMasterPrefs);
}

}  // namespace internal
}  // namespace first_run
