// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <windows.h>  // NOLINT
#include <shlwapi.h>  // NOLINT
#include <userenv.h>  // NOLINT

#include "chrome/app/main_dll_loader_win.h"

#include "base/base_paths.h"
#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/environment.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/path_service.h"
#include "base/strings/string16.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/platform_thread.h"
#include "base/trace_event/trace_event.h"
#include "base/win/metro.h"
#include "base/win/scoped_handle.h"
#include "base/win/windows_version.h"
#include "chrome/app/chrome_crash_reporter_client.h"
#include "chrome/app/chrome_watcher_client_win.h"
#include "chrome/app/chrome_watcher_command_line_win.h"
#include "chrome/app/file_pre_reader_win.h"
#include "chrome/app/kasko_client.h"
#include "chrome/chrome_watcher/chrome_watcher_main_api.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_result_codes.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/env_vars.h"
#include "chrome/installer/util/browser_distribution.h"
#include "chrome/installer/util/google_update_constants.h"
#include "chrome/installer/util/google_update_settings.h"
#include "chrome/installer/util/install_util.h"
#include "chrome/installer/util/module_util_win.h"
#include "chrome/installer/util/util_constants.h"
#include "components/crash/content/app/crash_keys_win.h"
#include "components/crash/content/app/crash_reporter_client.h"
#include "components/crash/content/app/crashpad.h"
#include "components/startup_metric_utils/browser/pre_read_field_trial_utils_win.h"
#include "content/public/app/sandbox_helper_win.h"
#include "content/public/common/content_switches.h"
#include "sandbox/win/src/sandbox.h"

namespace {
// The entry point signature of chrome.dll.
typedef int (*DLL_MAIN)(HINSTANCE, sandbox::SandboxInterfaceInfo*);

typedef void (*RelaunchChromeBrowserWithNewCommandLineIfNeededFunc)();

// Loads |module| after setting the CWD to |module|'s directory. Returns a
// reference to the loaded module on success, or null on error.
HMODULE LoadModuleWithDirectory(const base::FilePath& module, bool pre_read) {
  ::SetCurrentDirectoryW(module.DirName().value().c_str());

  // Get pre-read options from the PreRead field trial.
  bool trial_should_pre_read = true;
  bool trial_should_pre_read_high_priority = false;
  startup_metric_utils::GetPreReadOptions(
      BrowserDistribution::GetDistribution()->GetRegistryPath(),
      &trial_should_pre_read, &trial_should_pre_read_high_priority);

  if (pre_read && trial_should_pre_read) {
    base::ThreadPriority previous_priority = base::ThreadPriority::NORMAL;
    if (trial_should_pre_read_high_priority) {
      previous_priority = base::PlatformThread::GetCurrentThreadPriority();
      base::PlatformThread::SetCurrentThreadPriority(
          base::ThreadPriority::DISPLAY);
    }

    // We pre-read the binary to warm the memory caches (fewer hard faults to
    // page parts of the binary in).
    const size_t kStepSize = 1024 * 1024;
    PreReadFile(module, kStepSize);

    if (trial_should_pre_read_high_priority)
      base::PlatformThread::SetCurrentThreadPriority(previous_priority);
  }

  return ::LoadLibraryExW(module.value().c_str(), nullptr,
                          LOAD_WITH_ALTERED_SEARCH_PATH);
}

void RecordDidRun(const base::FilePath& dll_path) {
  bool system_level = !InstallUtil::IsPerUserInstall(dll_path);
  GoogleUpdateSettings::UpdateDidRunState(true, system_level);
}

void ClearDidRun(const base::FilePath& dll_path) {
  bool system_level = !InstallUtil::IsPerUserInstall(dll_path);
  GoogleUpdateSettings::UpdateDidRunState(false, system_level);
}

typedef int (*InitMetro)();

#if defined(KASKO)

// Returns a string containing a list of all modifiers for the loaded profile.
std::wstring GetProfileType() {
  std::wstring profile_type;
  DWORD profile_bits = 0;
  if (::GetProfileType(&profile_bits)) {
    static const struct {
      DWORD bit;
      const wchar_t* name;
    } kBitNames[] = {
      { PT_MANDATORY, L"mandatory" },
      { PT_ROAMING, L"roaming" },
      { PT_TEMPORARY, L"temporary" },
    };
    for (size_t i = 0; i < arraysize(kBitNames); ++i) {
      const DWORD this_bit = kBitNames[i].bit;
      if ((profile_bits & this_bit) != 0) {
        profile_type.append(kBitNames[i].name);
        profile_bits &= ~this_bit;
        if (profile_bits != 0)
          profile_type.append(L", ");
      }
    }
  } else {
    DWORD last_error = ::GetLastError();
    base::SStringPrintf(&profile_type, L"error %u", last_error);
  }
  return profile_type;
}

#endif  // KASKO

}  // namespace

//=============================================================================

MainDllLoader::MainDllLoader()
    : dll_(nullptr), metro_mode_(base::win::IsMetroProcess()) {
}

MainDllLoader::~MainDllLoader() {
}

// Loading chrome is an interesting affair. First we try loading from the
// current directory to support run-what-you-compile and other development
// scenarios.
// If that fails then we look at the version resource in the current
// module. This is the expected path for chrome.exe browser instances in an
// installed build.
HMODULE MainDllLoader::Load(base::string16* version, base::FilePath* module) {
  const base::char16* dll_name = nullptr;
  if (metro_mode_) {
    dll_name = installer::kChromeMetroDll;
  } else if (process_type_ == "service" || process_type_.empty()) {
    dll_name = installer::kChromeDll;
  } else if (process_type_ == "watcher") {
    dll_name = kChromeWatcherDll;
  } else {
#if defined(CHROME_MULTIPLE_DLL)
    dll_name = installer::kChromeChildDll;
#else
    dll_name = installer::kChromeDll;
#endif
  }

  *module = installer::GetModulePath(dll_name, version);
  if (module->empty()) {
    PLOG(ERROR) << "Cannot find module " << dll_name;
    return nullptr;
  }
  const bool pre_read = !metro_mode_;
  HMODULE dll = LoadModuleWithDirectory(*module, pre_read);
  if (!dll) {
    PLOG(ERROR) << "Failed to load Chrome DLL from " << module->value();
    return nullptr;
  }

  DCHECK(dll);
  return dll;
}

// Launching is a matter of loading the right dll, setting the CHROME_VERSION
// environment variable and just calling the entry point. Derived classes can
// add custom code in the OnBeforeLaunch callback.
int MainDllLoader::Launch(HINSTANCE instance) {
  const base::CommandLine& cmd_line = *base::CommandLine::ForCurrentProcess();
  process_type_ = cmd_line.GetSwitchValueASCII(switches::kProcessType);

  base::string16 version;
  base::FilePath file;

  if (metro_mode_) {
    HMODULE metro_dll = Load(&version, &file);
    if (!metro_dll)
      return chrome::RESULT_CODE_MISSING_DATA;

    InitMetro chrome_metro_main =
        reinterpret_cast<InitMetro>(::GetProcAddress(metro_dll, "InitMetro"));
    return chrome_metro_main();
  }

  if (process_type_ == "watcher") {
    chrome::RegisterPathProvider();

    base::win::ScopedHandle parent_process;
    base::win::ScopedHandle on_initialized_event;
    DWORD main_thread_id = 0;
    if (!InterpretChromeWatcherCommandLine(cmd_line, &parent_process,
                                           &main_thread_id,
                                           &on_initialized_event)) {
      return chrome::RESULT_CODE_UNSUPPORTED_PARAM;
    }

    base::FilePath default_user_data_directory;
    if (!PathService::Get(chrome::DIR_USER_DATA, &default_user_data_directory))
      return chrome::RESULT_CODE_MISSING_DATA;
    // The actual user data directory may differ from the default according to
    // policy and command-line arguments evaluated in the browser process.
    // The hang monitor will simply be disabled if a window with this name is
    // never instantiated by the browser process. Since this should be
    // exceptionally rare it should not impact stability efforts.
    base::string16 message_window_name = default_user_data_directory.value();

    base::FilePath watcher_data_directory;
    if (!PathService::Get(chrome::DIR_WATCHER_DATA, &watcher_data_directory))
      return chrome::RESULT_CODE_MISSING_DATA;

    base::string16 channel_name = GoogleUpdateSettings::GetChromeChannel(
        !InstallUtil::IsPerUserInstall(cmd_line.GetProgram()));

    // Intentionally leaked.
    HMODULE watcher_dll = Load(&version, &file);
    if (!watcher_dll)
      return chrome::RESULT_CODE_MISSING_DATA;

    ChromeWatcherMainFunction watcher_main =
        reinterpret_cast<ChromeWatcherMainFunction>(
            ::GetProcAddress(watcher_dll, kChromeWatcherDLLEntrypoint));
    return watcher_main(chrome::kBrowserExitCodesRegistryPath,
                        parent_process.Take(), main_thread_id,
                        on_initialized_event.Take(),
                        watcher_data_directory.value().c_str(),
                        message_window_name.c_str(), channel_name.c_str());
  }

  // Initialize the sandbox services.
  sandbox::SandboxInterfaceInfo sandbox_info = {0};
  content::InitializeSandboxInfo(&sandbox_info);

  dll_ = Load(&version, &file);
  if (!dll_)
    return chrome::RESULT_CODE_MISSING_DATA;

  scoped_ptr<base::Environment> env(base::Environment::Create());
  env->SetVar(chrome::kChromeVersionEnvVar, base::WideToUTF8(version));

  OnBeforeLaunch(process_type_, file);
  DLL_MAIN chrome_main =
      reinterpret_cast<DLL_MAIN>(::GetProcAddress(dll_, "ChromeMain"));
  int rc = chrome_main(instance, &sandbox_info);
  rc = OnBeforeExit(rc, file);
  return rc;
}

void MainDllLoader::RelaunchChromeBrowserWithNewCommandLineIfNeeded() {
  if (!dll_)
    return;

  RelaunchChromeBrowserWithNewCommandLineIfNeededFunc relaunch_function =
      reinterpret_cast<RelaunchChromeBrowserWithNewCommandLineIfNeededFunc>(
          ::GetProcAddress(dll_,
                           "RelaunchChromeBrowserWithNewCommandLineIfNeeded"));
  if (!relaunch_function) {
    LOG(ERROR) << "Could not find exported function "
               << "RelaunchChromeBrowserWithNewCommandLineIfNeeded";
  } else {
    relaunch_function();
  }
}

//=============================================================================

class ChromeDllLoader : public MainDllLoader {
 protected:
  // MainDllLoader implementation.
  void OnBeforeLaunch(const std::string& process_type,
                      const base::FilePath& dll_path) override;
  int OnBeforeExit(int return_code, const base::FilePath& dll_path) override;

 private:
  scoped_ptr<ChromeWatcherClient> chrome_watcher_client_;
#if defined(KASKO)
  scoped_ptr<KaskoClient> kasko_client_;
#endif  // KASKO
};

void ChromeDllLoader::OnBeforeLaunch(const std::string& process_type,
                                     const base::FilePath& dll_path) {
  if (process_type.empty()) {
    RecordDidRun(dll_path);

    // Launch the watcher process if stats collection consent has been granted.
#if defined(GOOGLE_CHROME_BUILD)
    const bool stats_collection_consent =
        GoogleUpdateSettings::GetCollectStatsConsent();
#else
    const bool stats_collection_consent = false;
#endif
    if (stats_collection_consent) {
      base::FilePath exe_path;
      if (PathService::Get(base::FILE_EXE, &exe_path)) {
        chrome_watcher_client_.reset(new ChromeWatcherClient(
            base::Bind(&GenerateChromeWatcherCommandLine, exe_path)));
        if (chrome_watcher_client_->LaunchWatcher()) {
#if defined(KASKO)
          kasko::api::MinidumpType minidump_type = kasko::api::SMALL_DUMP_TYPE;
          if (base::CommandLine::ForCurrentProcess()->HasSwitch(
                  switches::kFullMemoryCrashReport)) {
            minidump_type = kasko::api::FULL_DUMP_TYPE;
          } else {
            // TODO(scottmg): Point this at the common global one when it's
            // moved back into the .exe. http://crbug.com/546288.
            ChromeCrashReporterClient chrome_crash_client;
            bool is_per_user_install = chrome_crash_client.GetIsPerUserInstall(
                base::FilePath(exe_path));
            if (chrome_crash_client.GetShouldDumpLargerDumps(
                    is_per_user_install)) {
              minidump_type = kasko::api::LARGER_DUMP_TYPE;
            }

            // TODO(scottmg): http://crbug.com/564329 Breakpad is no longer
            // initialized. For now, initialize the CustomInfoEntries here so
            // Kasko can pull them out.
            static breakpad::CrashKeysWin crash_keys_win;
            crash_keys_win.GetCustomInfo(
                exe_path.value(), base::UTF8ToUTF16(process_type),
                GetProfileType(), base::CommandLine::ForCurrentProcess(),
                &chrome_crash_client);
          }

          kasko_client_.reset(
              new KaskoClient(chrome_watcher_client_.get(), minidump_type));
#endif  // KASKO
        }
      }
    }
  } else {
    // Set non-browser processes up to be killed by the system after the browser
    // goes away. The browser uses the default shutdown order, which is 0x280.
    // Note that lower numbers here denote "kill later" and higher numbers mean
    // "kill sooner".
    // This gets rid of most of those unsighly sad tabs on logout and shutdown.
    ::SetProcessShutdownParameters(0x280 - 1, SHUTDOWN_NORETRY);
  }
}

int ChromeDllLoader::OnBeforeExit(int return_code,
                                  const base::FilePath& dll_path) {
  // NORMAL_EXIT_CANCEL is used for experiments when the user cancels
  // so we need to reset the did_run signal so omaha does not count
  // this run as active usage.
  if (chrome::RESULT_CODE_NORMAL_EXIT_CANCEL == return_code) {
    ClearDidRun(dll_path);
  }

#if defined(KASKO)
  kasko_client_.reset();
#endif  // KASKO
  chrome_watcher_client_.reset();

  return return_code;
}

//=============================================================================

class ChromiumDllLoader : public MainDllLoader {
 protected:
  void OnBeforeLaunch(const std::string& process_type,
                      const base::FilePath& dll_path) override {}
  int OnBeforeExit(int return_code, const base::FilePath& dll_path) override {
    return return_code;
  }
};

MainDllLoader* MakeMainDllLoader() {
#if defined(GOOGLE_CHROME_BUILD)
  return new ChromeDllLoader();
#else
  return new ChromiumDllLoader();
#endif
}
