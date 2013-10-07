// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/app/chrome_main_delegate.h"

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/lazy_instance.h"
#include "base/message_loop/message_loop.h"
#include "base/metrics/stats_counters.h"
#include "base/path_service.h"
#include "base/process/memory.h"
#include "base/process/process_handle.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/browser/chrome_content_browser_client.h"
#include "chrome/browser/defaults.h"
#include "chrome/browser/policy/policy_path_parser.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_content_client.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_paths_internal.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/chrome_version_info.h"
#include "chrome/common/logging_chrome.h"
#include "chrome/common/profiling.h"
#include "chrome/common/startup_metric_utils.h"
#include "chrome/common/url_constants.h"
#include "chrome/plugin/chrome_content_plugin_client.h"
#include "chrome/renderer/chrome_content_renderer_client.h"
#include "chrome/utility/chrome_content_utility_client.h"
#include "components/nacl/common/nacl_switches.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_paths.h"
#include "ui/base/ui_base_switches.h"

#if defined(OS_WIN)
#include <algorithm>
#include <atlbase.h>
#include <malloc.h>
#include "base/strings/string_util.h"
#include "sandbox/win/src/sandbox.h"
#include "tools/memory_watcher/memory_watcher.h"
#include "ui/base/resource/resource_bundle_win.h"
#endif

#if defined(OS_MACOSX)
#include "base/mac/mac_util.h"
#include "base/mac/os_crash_dumps.h"
#include "chrome/app/breakpad_mac.h"
#include "chrome/app/chrome_main_mac.h"
#include "chrome/browser/mac/relauncher.h"
#include "chrome/common/chrome_paths_internal.h"
#include "chrome/common/mac/cfbundle_blocker.h"
#include "chrome/common/mac/objc_zombie.h"
#include "grit/chromium_strings.h"
#include "ui/base/l10n/l10n_util_mac.h"
#endif

#if defined(OS_POSIX)
#include <locale.h>
#include <signal.h>
#include "chrome/app/chrome_breakpad_client.h"
#include "components/breakpad/breakpad_client.h"
#endif

#if !defined(DISABLE_NACL) && defined(OS_LINUX)
#include "components/nacl/common/nacl_paths.h"
#include "components/nacl/zygote/nacl_fork_delegate_linux.h"
#endif

#if defined(OS_CHROMEOS)
#include "base/sys_info.h"
#include "chrome/browser/chromeos/boot_times_loader.h"
#include "chromeos/chromeos_paths.h"
#include "chromeos/chromeos_switches.h"
#endif

#if defined(OS_ANDROID)
#include "chrome/common/descriptors_android.h"
#else
// Diagnostics is only available on non-android platforms.
#include "chrome/browser/diagnostics/diagnostics_controller.h"
#include "chrome/browser/diagnostics/diagnostics_writer.h"
#endif

#if defined(USE_X11)
#include <stdlib.h>
#include <string.h>
#include "ui/base/x/x11_util.h"
#endif

#if defined(OS_POSIX) && !defined(OS_MACOSX)
#include "chrome/app/breakpad_linux.h"
#endif

#if !defined(CHROME_MULTIPLE_DLL_CHILD)
base::LazyInstance<chrome::ChromeContentBrowserClient>
    g_chrome_content_browser_client = LAZY_INSTANCE_INITIALIZER;
#endif

#if !defined(CHROME_MULTIPLE_DLL_BROWSER)
base::LazyInstance<chrome::ChromeContentRendererClient>
    g_chrome_content_renderer_client = LAZY_INSTANCE_INITIALIZER;
base::LazyInstance<chrome::ChromeContentUtilityClient>
    g_chrome_content_utility_client = LAZY_INSTANCE_INITIALIZER;
base::LazyInstance<chrome::ChromeContentPluginClient>
    g_chrome_content_plugin_client = LAZY_INSTANCE_INITIALIZER;
#endif

#if defined(OS_POSIX)
base::LazyInstance<chrome::ChromeBreakpadClient>::Leaky
    g_chrome_breakpad_client = LAZY_INSTANCE_INITIALIZER;
#endif

extern int NaClMain(const content::MainFunctionParams&);
extern int ServiceProcessMain(const content::MainFunctionParams&);

namespace {

#if defined(OS_WIN)
const wchar_t kProfilingDll[] = L"memory_watcher.dll";

// Load the memory profiling DLL.  All it needs to be activated
// is to be loaded.  Return true on success, false otherwise.
bool LoadMemoryProfiler() {
  HMODULE prof_module = LoadLibrary(kProfilingDll);
  return prof_module != NULL;
}

// Early versions of Chrome incorrectly registered a chromehtml: URL handler,
// which gives us nothing but trouble. Avoid launching chrome this way since
// some apps fail to properly escape arguments.
bool HasDeprecatedArguments(const std::wstring& command_line) {
  const wchar_t kChromeHtml[] = L"chromehtml:";
  std::wstring command_line_lower = command_line;
  // We are only searching for ASCII characters so this is OK.
  StringToLowerASCII(&command_line_lower);
  std::wstring::size_type pos = command_line_lower.find(kChromeHtml);
  return (pos != std::wstring::npos);
}

// If we try to access a path that is not currently available, we want the call
// to fail rather than show an error dialog.
void SuppressWindowsErrorDialogs() {
  UINT new_flags = SEM_FAILCRITICALERRORS |
                   SEM_NOOPENFILEERRORBOX;

  // Preserve existing error mode.
  UINT existing_flags = SetErrorMode(new_flags);
  SetErrorMode(existing_flags | new_flags);
}

#endif  // defined(OS_WIN)

#if defined(OS_LINUX)
static void AdjustLinuxOOMScore(const std::string& process_type) {
  // Browsers and zygotes should still be killable, but killed last.
  const int kZygoteScore = 0;
  // The minimum amount to bump a score by.  This is large enough that
  // even if it's translated into the old values, it will still go up
  // by at least one.
  const int kScoreBump = 100;
  // This is the lowest score that renderers and extensions start with
  // in the OomPriorityManager.
  const int kRendererScore = chrome::kLowestRendererOomScore;
  // For "miscellaneous" things, we want them after renderers,
  // but before plugins.
  const int kMiscScore = kRendererScore - kScoreBump;
  // We want plugins to die after the renderers.
  const int kPluginScore = kMiscScore - kScoreBump;
  int score = -1;

  DCHECK(kMiscScore > 0);
  DCHECK(kPluginScore > 0);

  if (process_type == switches::kPluginProcess ||
      process_type == switches::kPpapiPluginProcess) {
    score = kPluginScore;
  } else if (process_type == switches::kPpapiBrokerProcess) {
    // The broker should be killed before the PPAPI plugin.
    score = kPluginScore + kScoreBump;
  } else if (process_type == switches::kUtilityProcess ||
             process_type == switches::kWorkerProcess ||
             process_type == switches::kGpuProcess ||
             process_type == switches::kServiceProcess) {
    score = kMiscScore;
#ifndef DISABLE_NACL
  } else if (process_type == switches::kNaClLoaderProcess) {
    score = kPluginScore;
#endif
  } else if (process_type == switches::kZygoteProcess ||
             process_type.empty()) {
    // For zygotes and unlabeled process types, we want to still make
    // them killable by the OOM killer.
    score = kZygoteScore;
  } else if (process_type == switches::kRendererProcess) {
    LOG(WARNING) << "process type 'renderer' "
                 << "should be created through the zygote.";
    // When debugging, this process type can end up being run directly, but
    // this isn't the typical path for assigning the OOM score for it.  Still,
    // we want to assign a score that is somewhat representative for debugging.
    score = kRendererScore;
  } else {
    NOTREACHED() << "Unknown process type";
  }
  if (score > -1)
    base::AdjustOOMScore(base::GetCurrentProcId(), score);
}
#endif  // defined(OS_LINUX)

// Enable the heap profiler if the appropriate command-line switch is
// present, bailing out of the app we can't.
void EnableHeapProfiler(const CommandLine& command_line) {
#if defined(OS_WIN)
  if (command_line.HasSwitch(switches::kMemoryProfiling))
    if (!LoadMemoryProfiler())
      exit(-1);
#endif
}

// Returns true if this subprocess type needs the ResourceBundle initialized
// and resources loaded.
bool SubprocessNeedsResourceBundle(const std::string& process_type) {
  return
#if defined(OS_WIN) || defined(OS_MACOSX)
      // Windows needs resources for the default/null plugin.
      // Mac needs them for the plugin process name.
      process_type == switches::kPluginProcess ||
#endif
#if defined(OS_POSIX) && !defined(OS_MACOSX)
      // The zygote process opens the resources for the renderers.
      process_type == switches::kZygoteProcess ||
#endif
#if defined(OS_MACOSX)
      // Mac needs them too for scrollbar related images and for sandbox
      // profiles.
      process_type == switches::kWorkerProcess ||
      process_type == switches::kNaClLoaderProcess ||
      process_type == switches::kPpapiPluginProcess ||
      process_type == switches::kPpapiBrokerProcess ||
      process_type == switches::kGpuProcess ||
#endif
      process_type == switches::kRendererProcess ||
      process_type == switches::kUtilityProcess;
}

#if defined(OS_MACOSX)
// Update the name shown in Activity Monitor so users are less likely to ask
// why Chrome has so many processes.
void SetMacProcessName(const CommandLine& command_line) {
  std::string process_type =
      command_line.GetSwitchValueASCII(switches::kProcessType);
  // Don't worry about the browser process, its gets the stock name.
  int name_id = 0;
  if (command_line.HasSwitch(switches::kExtensionProcess)) {
    name_id = IDS_WORKER_APP_NAME;
  } else if (process_type == switches::kRendererProcess) {
    name_id = IDS_RENDERER_APP_NAME;
  } else if (process_type == switches::kPluginProcess ||
             process_type == switches::kPpapiPluginProcess) {
    name_id = IDS_PLUGIN_APP_NAME;
  } else if (process_type == switches::kUtilityProcess) {
    name_id = IDS_UTILITY_APP_NAME;
  }
  if (name_id) {
    NSString* app_name = l10n_util::GetNSString(name_id);
    base::mac::SetProcessName(base::mac::NSToCFCast(app_name));
  }
}

#endif  // defined(OS_MACOSX)

#if defined(OS_POSIX)
// Check for --version and --product-version; return true if we encountered
// one of these switches and should exit now.
bool HandleVersionSwitches(const CommandLine& command_line) {
  const chrome::VersionInfo version_info;

#if !defined(OS_MACOSX)
  if (command_line.HasSwitch(switches::kProductVersion)) {
    printf("%s\n", version_info.Version().c_str());
    return true;
  }
#endif

  if (command_line.HasSwitch(switches::kVersion)) {
    printf("%s %s %s\n",
           version_info.Name().c_str(),
           version_info.Version().c_str(),
           chrome::VersionInfo::GetVersionStringModifier().c_str());
    return true;
  }

  return false;
}

#if !defined(OS_MACOSX) && !defined(OS_CHROMEOS)
// Show the man page if --help or -h is on the command line.
void HandleHelpSwitches(const CommandLine& command_line) {
  if (command_line.HasSwitch(switches::kHelp) ||
      command_line.HasSwitch(switches::kHelpShort)) {
    base::FilePath binary(command_line.argv()[0]);
    execlp("man", "man", binary.BaseName().value().c_str(), NULL);
    PLOG(FATAL) << "execlp failed";
  }
}
#endif

#if !defined(OS_MACOSX)
void SIGTERMProfilingShutdown(int signal) {
  Profiling::Stop();
  struct sigaction sigact;
  memset(&sigact, 0, sizeof(sigact));
  sigact.sa_handler = SIG_DFL;
  CHECK(sigaction(SIGTERM, &sigact, NULL) == 0);
  raise(signal);
}

void SetUpProfilingShutdownHandler() {
  struct sigaction sigact;
  sigact.sa_handler = SIGTERMProfilingShutdown;
  sigact.sa_flags = SA_RESETHAND;
  sigemptyset(&sigact.sa_mask);
  CHECK(sigaction(SIGTERM, &sigact, NULL) == 0);
}
#endif

#endif  // OS_POSIX

struct MainFunction {
  const char* name;
  int (*function)(const content::MainFunctionParams&);
};

}  // namespace

ChromeMainDelegate::ChromeMainDelegate() {
  startup_metric_utils::RecordMainEntryPointTime();
}

ChromeMainDelegate::~ChromeMainDelegate() {
}

bool ChromeMainDelegate::BasicStartupComplete(int* exit_code) {
#if defined(OS_CHROMEOS)
  chromeos::BootTimesLoader::Get()->SaveChromeMainStats();
#endif

  const CommandLine& command_line = *CommandLine::ForCurrentProcess();

#if defined(OS_MACOSX)
  // Give the browser process a longer treadmill, since crashes
  // there have more impact.
  const bool is_browser = !command_line.HasSwitch(switches::kProcessType);
  ObjcEvilDoers::ZombieEnable(true, is_browser ? 10000 : 1000);

  SetUpBundleOverrides();
  chrome::common::mac::EnableCFBundleBlocker();
#endif

  Profiling::ProcessStarted();

#if defined(OS_POSIX)
  if (HandleVersionSwitches(command_line)) {
    *exit_code = 0;
    return true;  // Got a --version switch; exit with a success error code.
  }
#if !defined(OS_MACOSX) && !defined(OS_CHROMEOS)
  // This will directly exit if the user asked for help.
  HandleHelpSwitches(command_line);
#endif
#endif  // OS_POSIX

#if defined(OS_WIN)
  // Must do this before any other usage of command line!
  if (HasDeprecatedArguments(command_line.GetCommandLineString())) {
    *exit_code = 1;
    return true;
  }
#endif

  chrome::RegisterPathProvider();
#if defined(OS_CHROMEOS)
  chromeos::RegisterPathProvider();
#endif
#if !defined(DISABLE_NACL) && defined(OS_LINUX)
  nacl::RegisterPathProvider();
#endif

// No support for ANDROID yet as DiagnosticsController needs wchar support.
// TODO(gspencer): That's not true anymore, or at least there are no w-string
// references anymore. Not sure if that means this can be enabled on Android or
// not though: it still uses string16. As there is no easily accessible command
// line on Android, I'm not sure this is a big deal, at least for purposes of
// troubleshooting with a customer.
#if !defined(OS_ANDROID) && !defined(CHROME_MULTIPLE_DLL_CHILD)
  // If we are in diagnostics mode this is the end of the line: after the
  // diagnostics are run the process will invariably exit.
  if (command_line.HasSwitch(switches::kDiagnostics)) {
    diagnostics::DiagnosticsWriter::FormatType format =
        diagnostics::DiagnosticsWriter::HUMAN;
    if (command_line.HasSwitch(switches::kDiagnosticsFormat)) {
      std::string format_str =
          command_line.GetSwitchValueASCII(switches::kDiagnosticsFormat);
      if (format_str == "machine") {
        format = diagnostics::DiagnosticsWriter::MACHINE;
      } else if (format_str == "log") {
        format = diagnostics::DiagnosticsWriter::LOG;
      } else {
        DCHECK_EQ("human", format_str);
      }
    }

    diagnostics::DiagnosticsWriter writer(format);
    *exit_code = diagnostics::DiagnosticsController::GetInstance()->Run(
        command_line, &writer);
    diagnostics::DiagnosticsController::GetInstance()->ClearResults();
    return true;
  }
#endif

#if defined(OS_CHROMEOS)
  // If we are recovering from a crash on ChromeOS, then we will do some
  // recovery using the diagnostics module, and then continue on. We fake up a
  // command line to tell it that we want it to recover, and to preserve the
  // original command line.
  if (command_line.HasSwitch(chromeos::switches::kLoginUser) ||
      command_line.HasSwitch(switches::kDiagnosticsRecovery)) {
    CommandLine interim_command_line(command_line.GetProgram());
    if (command_line.HasSwitch(switches::kUserDataDir)) {
      interim_command_line.AppendSwitchPath(
          switches::kUserDataDir,
          command_line.GetSwitchValuePath(switches::kUserDataDir));
    }
    interim_command_line.AppendSwitch(switches::kDiagnostics);
    interim_command_line.AppendSwitch(switches::kDiagnosticsRecovery);

    diagnostics::DiagnosticsWriter::FormatType format =
        diagnostics::DiagnosticsWriter::LOG;
    if (command_line.HasSwitch(switches::kDiagnosticsFormat)) {
      std::string format_str =
          command_line.GetSwitchValueASCII(switches::kDiagnosticsFormat);
      if (format_str == "machine") {
        format = diagnostics::DiagnosticsWriter::MACHINE;
      } else if (format_str == "human") {
        format = diagnostics::DiagnosticsWriter::HUMAN;
      } else {
        DCHECK_EQ("log", format_str);
      }
    }

    diagnostics::DiagnosticsWriter writer(format);
    int diagnostics_exit_code =
        diagnostics::DiagnosticsController::GetInstance()->Run(command_line,
                                                               &writer);
    if (diagnostics_exit_code) {
      // Diagnostics has failed somehow, so we exit.
      *exit_code = diagnostics_exit_code;
      return true;
    }

    // Now we run the actual recovery tasks.
    int recovery_exit_code =
        diagnostics::DiagnosticsController::GetInstance()->RunRecovery(
            command_line, &writer);

    if (recovery_exit_code) {
      // Recovery has failed somehow, so we exit.
      *exit_code = recovery_exit_code;
      return true;
    }
  }
#endif

  content::SetContentClient(&chrome_content_client_);

  return false;
}

#if defined(OS_MACOSX)
void ChromeMainDelegate::InitMacCrashReporter(const CommandLine& command_line,
                                              const std::string& process_type) {
  // TODO(mark): Right now, InitCrashReporter() needs to be called after
  // CommandLine::Init() and chrome::RegisterPathProvider().  Ideally,
  // Breakpad initialization could occur sooner, preferably even before the
  // framework dylib is even loaded, to catch potential early crashes.
  InitCrashReporter();

#if defined(NDEBUG)
  bool is_debug_build = false;
#else
  bool is_debug_build = true;
#endif

  // Details on when we enable Apple's Crash reporter.
  //
  // Motivation:
  //    In debug mode it takes Apple's crash reporter eons to generate a crash
  // dump.
  //
  // What we do:
  // * We only pass crashes for foreground processes to Apple's Crash
  //    reporter. At the time of this writing, that means just the Browser
  //    process.
  // * If Breakpad is enabled, it will pass browser crashes to Crash Reporter
  //    itself.
  // * If Breakpad is disabled, we only turn on Crash Reporter for the
  //    Browser process in release mode.
  if (!command_line.HasSwitch(switches::kDisableBreakpad)) {
    bool disable_apple_crash_reporter = is_debug_build ||
        base::mac::IsBackgroundOnlyProcess();
    if (!IsCrashReporterEnabled() && disable_apple_crash_reporter) {
      base::mac::DisableOSCrashDumps();
    }
  }

  // Mac Chrome is packaged with a main app bundle and a helper app bundle.
  // The main app bundle should only be used for the browser process, so it
  // should never see a --type switch (switches::kProcessType).  Likewise,
  // the helper should always have a --type switch.
  //
  // This check is done this late so there is already a call to
  // base::mac::IsBackgroundOnlyProcess(), so there is no change in
  // startup/initialization order.

  // The helper's Info.plist marks it as a background only app.
  if (base::mac::IsBackgroundOnlyProcess()) {
    CHECK(command_line.HasSwitch(switches::kProcessType) &&
          !process_type.empty())
        << "Helper application requires --type.";

    // In addition, some helper flavors only work with certain process types.
    base::FilePath executable;
    if (PathService::Get(base::FILE_EXE, &executable) &&
        executable.value().size() >= 3) {
      std::string last_three =
          executable.value().substr(executable.value().size() - 3);

      if (last_three == " EH") {
        CHECK(process_type == switches::kPluginProcess ||
              process_type == switches::kUtilityProcess)
            << "Executable-heap process requires --type="
            << switches::kPluginProcess << " or "
            << switches::kUtilityProcess << ", saw " << process_type;
      } else if (last_three == " NP") {
        CHECK_EQ(switches::kNaClLoaderProcess, process_type)
            << "Non-PIE process requires --type="
            << switches::kNaClLoaderProcess << ", saw " << process_type;
      } else {
        CHECK(process_type != switches::kPluginProcess &&
              process_type != switches::kNaClLoaderProcess)
            << "Non-executable-heap PIE process is intolerant of --type="
            << switches::kPluginProcess << " and "
            << switches::kNaClLoaderProcess << ", saw " << process_type;
      }
    }
  } else {
    CHECK(!command_line.HasSwitch(switches::kProcessType) &&
          process_type.empty())
        << "Main application forbids --type, saw " << process_type;
  }

  if (IsCrashReporterEnabled())
    InitCrashProcessInfo();
}
#endif  // defined(OS_MACOSX)

void ChromeMainDelegate::PreSandboxStartup() {
  const CommandLine& command_line = *CommandLine::ForCurrentProcess();
  std::string process_type =
      command_line.GetSwitchValueASCII(switches::kProcessType);

#if defined(OS_POSIX)
  breakpad::SetBreakpadClient(g_chrome_breakpad_client.Pointer());
#endif

#if defined(OS_MACOSX)
  // On the Mac, the child executable lives at a predefined location within
  // the app bundle's versioned directory.
  PathService::Override(content::CHILD_PROCESS_EXE,
                        chrome::GetVersionedDirectory().
                        Append(chrome::kHelperProcessExecutablePath));

  InitMacCrashReporter(command_line, process_type);
#endif

  // Notice a user data directory override if any
  base::FilePath user_data_dir =
      command_line.GetSwitchValuePath(switches::kUserDataDir);
#if defined(OS_MACOSX) || defined(OS_WIN)
  policy::path_parser::CheckUserDataDirPolicy(&user_data_dir);
#endif
  if (!user_data_dir.empty()) {
    CHECK(PathService::OverrideAndCreateIfNeeded(
        chrome::DIR_USER_DATA,
        user_data_dir,
        chrome::ProcessNeedsProfileDir(process_type)));
  }

  stats_counter_timer_.reset(new base::StatsCounterTimer("Chrome.Init"));
  startup_timer_.reset(new base::StatsScope<base::StatsCounterTimer>
                       (*stats_counter_timer_));

  // Enable the heap profiler as early as possible!
  EnableHeapProfiler(command_line);

  // Enable Message Loop related state asap.
  if (command_line.HasSwitch(switches::kMessageLoopHistogrammer))
    base::MessageLoop::EnableHistogrammer(true);

#if !defined(OS_ANDROID)
  // Android does InitLogging when library is loaded. Skip here.
  logging::OldFileDeletionState file_state =
      logging::APPEND_TO_OLD_LOG_FILE;
  if (process_type.empty()) {
    file_state = logging::DELETE_OLD_LOG_FILE;
  }
  logging::InitChromeLogging(command_line, file_state);
#endif

#if defined(OS_WIN)
  // TODO(darin): Kill this once http://crbug.com/52609 is fixed.
  ui::SetResourcesDataDLL(_AtlBaseModule.GetResourceInstance());
#endif

  if (SubprocessNeedsResourceBundle(process_type)) {
    // Initialize ResourceBundle which handles files loaded from external
    // sources.  The language should have been passed in to us from the
    // browser process as a command line flag.
#if defined(DISABLE_NACL)
    DCHECK(command_line.HasSwitch(switches::kLang) ||
           process_type == switches::kZygoteProcess ||
           process_type == switches::kGpuProcess ||
           process_type == switches::kPpapiBrokerProcess ||
           process_type == switches::kPpapiPluginProcess);
#else
    DCHECK(command_line.HasSwitch(switches::kLang) ||
           process_type == switches::kZygoteProcess ||
           process_type == switches::kGpuProcess ||
           process_type == switches::kNaClLoaderProcess ||
           process_type == switches::kPpapiBrokerProcess ||
           process_type == switches::kPpapiPluginProcess);
#endif

    // TODO(markusheintz): The command line flag --lang is actually processed
    // by the CommandLinePrefStore, and made available through the PrefService
    // via the preference prefs::kApplicationLocale. The browser process uses
    // the --lang flag to pass the value of the PrefService in here. Maybe
    // this value could be passed in a different way.
    const std::string locale =
        command_line.GetSwitchValueASCII(switches::kLang);
#if defined(OS_ANDROID)
    // The renderer sandbox prevents us from accessing our .pak files directly.
    // Therefore file descriptors to the .pak files that we need are passed in
    // at process creation time.
    int locale_pak_fd = base::GlobalDescriptors::GetInstance()->MaybeGet(
        kAndroidLocalePakDescriptor);
    CHECK(locale_pak_fd != -1);
    ResourceBundle::InitSharedInstanceWithPakFile(locale_pak_fd, false);

    int extra_pak_keys[] = {
      kAndroidChromePakDescriptor,
      kAndroidChrome100PercentPakDescriptor,
      kAndroidUIResourcesPakDescriptor,
    };
    for (size_t i = 0; i < arraysize(extra_pak_keys); ++i) {
      int pak_fd =
          base::GlobalDescriptors::GetInstance()->MaybeGet(extra_pak_keys[i]);
      CHECK(pak_fd != -1);
      ResourceBundle::GetSharedInstance().AddDataPackFromFile(
          pak_fd, ui::SCALE_FACTOR_100P);
    }

    const std::string loaded_locale = locale;
#else
    const std::string loaded_locale =
        ResourceBundle::InitSharedInstanceWithLocale(locale, NULL);

    base::FilePath resources_pack_path;
    PathService::Get(chrome::FILE_RESOURCES_PACK, &resources_pack_path);
    ResourceBundle::GetSharedInstance().AddDataPackFromPath(
        resources_pack_path, ui::SCALE_FACTOR_NONE);
#endif
    CHECK(!loaded_locale.empty()) << "Locale could not be found for " <<
        locale;

#if defined(OS_MACOSX)
    // Update the process name (need resources to get the strings, so
    // only do this when ResourcesBundle has been initialized).
    SetMacProcessName(command_line);
#endif  // defined(OS_MACOSX)

#if !defined(CHROME_MULTIPLE_DLL_BROWSER)
    if (process_type == switches::kUtilityProcess)
      chrome::ChromeContentUtilityClient::PreSandboxStartup();
#endif
  }

#if defined(OS_POSIX) && !defined(OS_MACOSX)
  // Needs to be called after we have chrome::DIR_USER_DATA.  BrowserMain
  // sets this up for the browser process in a different manner. Zygotes
  // need to call InitCrashReporter() in RunZygote().
  if (!process_type.empty() && process_type != switches::kZygoteProcess) {
#if defined(OS_ANDROID)
    InitNonBrowserCrashReporterForAndroid();
#else
    InitCrashReporter();
#endif
  }
#endif

#if defined(OS_CHROMEOS)
  // Read and cache ChromeOS version from file,
  // to be used from inside the sandbox.
  int32 major_version, minor_version, bugfix_version;
  base::SysInfo::OperatingSystemVersionNumbers(
      &major_version, &minor_version, &bugfix_version);
#endif
}

void ChromeMainDelegate::SandboxInitialized(const std::string& process_type) {
  startup_timer_->Stop();  // End of Startup Time Measurement.

  // Note: If you are adding a new process type below, be sure to adjust the
  // AdjustLinuxOOMScore function too.
#if defined(OS_LINUX)
  AdjustLinuxOOMScore(process_type);
#endif
#if defined(OS_WIN)
  SuppressWindowsErrorDialogs();
#endif
}

int ChromeMainDelegate::RunProcess(
    const std::string& process_type,
    const content::MainFunctionParams& main_function_params) {
  // ANDROID doesn't support "service", so no ServiceProcessMain, and arraysize
  // doesn't support empty array. So we comment out the block for Android.
#if !defined(OS_ANDROID) && \
    (!defined(CHROME_MULTIPLE_DLL) || defined(CHROME_MULTIPLE_DLL_BROWSER))
  static const MainFunction kMainFunctions[] = {
#if defined(ENABLE_FULL_PRINTING)
    { switches::kServiceProcess,     ServiceProcessMain },
#endif

#if defined(OS_MACOSX)
    { switches::kRelauncherProcess,
      mac_relauncher::internal::RelauncherMain },
#endif

#if !defined(DISABLE_NACL) && \
    (!defined(CHROME_MULTIPLE_DLL) || defined(CHROME_MULTIPLE_DLL_CHILD))
    { switches::kNaClLoaderProcess, NaClMain },
#endif  // DISABLE_NACL
  };

  for (size_t i = 0; i < arraysize(kMainFunctions); ++i) {
    if (process_type == kMainFunctions[i].name)
      return kMainFunctions[i].function(main_function_params);
  }
#endif

  return -1;
}

void ChromeMainDelegate::ProcessExiting(const std::string& process_type) {
  if (SubprocessNeedsResourceBundle(process_type))
    ResourceBundle::CleanupSharedInstance();
#if !defined(OS_ANDROID)
  logging::CleanupChromeLogging();
#else
  // Android doesn't use InitChromeLogging, so we close the log file manually.
  logging::CloseLogFile();
#endif  // !defined(OS_ANDROID)
}

#if defined(OS_MACOSX)
bool ChromeMainDelegate::ProcessRegistersWithSystemProcess(
    const std::string& process_type) {
  return process_type == switches::kNaClLoaderProcess;
}

bool ChromeMainDelegate::ShouldSendMachPort(const std::string& process_type) {
  return process_type != switches::kRelauncherProcess &&
      process_type != switches::kServiceProcess;
}

bool ChromeMainDelegate::DelaySandboxInitialization(
    const std::string& process_type) {
  // NaClLoader does this in NaClMainPlatformDelegate::EnableSandbox().
  // No sandbox needed for relauncher.
  return process_type == switches::kNaClLoaderProcess ||
      process_type == switches::kRelauncherProcess;
}
#elif defined(OS_POSIX) && !defined(OS_ANDROID)
content::ZygoteForkDelegate* ChromeMainDelegate::ZygoteStarting() {
#if defined(DISABLE_NACL)
  return NULL;
#else
  return new NaClForkDelegate();
#endif
}

void ChromeMainDelegate::ZygoteForked() {
  Profiling::ProcessStarted();
  if (Profiling::BeingProfiled()) {
    base::debug::RestartProfilingAfterFork();
    SetUpProfilingShutdownHandler();
  }

#if defined(OS_POSIX) && !defined(OS_MACOSX)
  // Needs to be called after we have chrome::DIR_USER_DATA.  BrowserMain sets
  // this up for the browser process in a different manner.
  InitCrashReporter();
#endif
}

#endif  // OS_MACOSX

content::ContentBrowserClient*
ChromeMainDelegate::CreateContentBrowserClient() {
#if defined(CHROME_MULTIPLE_DLL_CHILD)
  return NULL;
#else
  return &g_chrome_content_browser_client.Get();
#endif
}

content::ContentPluginClient* ChromeMainDelegate::CreateContentPluginClient() {
#if defined(CHROME_MULTIPLE_DLL_BROWSER)
  return NULL;
#else
  return &g_chrome_content_plugin_client.Get();
#endif
}

content::ContentRendererClient*
ChromeMainDelegate::CreateContentRendererClient() {
#if defined(CHROME_MULTIPLE_DLL_BROWSER)
  return NULL;
#else
  return &g_chrome_content_renderer_client.Get();
#endif
}

content::ContentUtilityClient*
ChromeMainDelegate::CreateContentUtilityClient() {
#if defined(CHROME_MULTIPLE_DLL_BROWSER)
  return NULL;
#else
  return &g_chrome_content_utility_client.Get();
#endif
}
