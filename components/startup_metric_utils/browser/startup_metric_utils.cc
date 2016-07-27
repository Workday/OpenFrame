// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/startup_metric_utils/browser/startup_metric_utils.h"

#include "base/containers/hash_tables.h"
#include "base/environment.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/string_number_conversions.h"
#include "base/sys_info.h"
#include "base/threading/platform_thread.h"
#include "base/trace_event/trace_event.h"

#if defined(OS_WIN)
#include <winternl.h>
#include "base/win/windows_version.h"
#endif

namespace startup_metric_utils {

namespace {

// Mark as volatile to defensively make sure usage is thread-safe.
// Note that at the time of this writing, access is only on the UI thread.
volatile bool g_non_browser_ui_displayed = false;

base::LazyInstance<base::TimeTicks>::Leaky g_process_creation_ticks =
    LAZY_INSTANCE_INITIALIZER;

base::LazyInstance<base::TimeTicks>::Leaky g_browser_main_entry_point_ticks =
    LAZY_INSTANCE_INITIALIZER;

base::LazyInstance<base::TimeTicks>::Leaky g_renderer_main_entry_point_ticks =
    LAZY_INSTANCE_INITIALIZER;

// Only used by RecordMainEntryTimeHistogram(), should go away with it (do not
// add new uses of this), see crbug.com/317481 for discussion on why it was kept
// as-is for now.
base::LazyInstance<base::Time>::Leaky g_browser_main_entry_point_time =
    LAZY_INSTANCE_INITIALIZER;

StartupTemperature g_startup_temperature = UNCERTAIN_STARTUP_TEMPERATURE;

#if defined(OS_WIN)

// These values are taken from the Startup.BrowserMessageLoopStartHardFaultCount
// histogram. If the cold start histogram starts looking strongly bimodal it may
// be because the binary/resource sizes have grown significantly larger than
// when these values were set. In this case the new values need to be chosen
// from the original histogram.
//
// Maximum number of hard faults tolerated for a startup to be classified as a
// warm start. Set at roughly the 40th percentile of the HardFaultCount
// histogram.
const uint32_t WARM_START_HARD_FAULT_COUNT_THRESHOLD = 5;
// Minimum number of hard faults expected for a startup to be classified as a
// cold start. Set at roughly the 60th percentile of the HardFaultCount
// histogram.
const uint32_t COLD_START_HARD_FAULT_COUNT_THRESHOLD = 1200;

// The struct used to return system process information via the NT internal
// QuerySystemInformation call. This is partially documented at
// http://goo.gl/Ja9MrH and fully documented at http://goo.gl/QJ70rn
// This structure is laid out in the same format on both 32-bit and 64-bit
// systems, but has a different size due to the various pointer-sized fields.
struct SYSTEM_PROCESS_INFORMATION_EX {
  ULONG NextEntryOffset;
  ULONG NumberOfThreads;
  LARGE_INTEGER WorkingSetPrivateSize;
  ULONG HardFaultCount;
  BYTE Reserved1[36];
  PVOID Reserved2[3];
  // This is labeled a handle so that it expands to the correct size for 32-bit
  // and 64-bit operating systems. However, under the hood it's a 32-bit DWORD
  // containing the process ID.
  HANDLE UniqueProcessId;
  PVOID Reserved3;
  ULONG HandleCount;
  BYTE Reserved4[4];
  PVOID Reserved5[11];
  SIZE_T PeakPagefileUsage;
  SIZE_T PrivatePageCount;
  LARGE_INTEGER Reserved6[6];
  // Array of SYSTEM_THREAD_INFORMATION structs follows.
};

// The signature of the NtQuerySystemInformation function.
typedef NTSTATUS (WINAPI *NtQuerySystemInformationPtr)(
    SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG);

// Gets the hard fault count of the current process, returning it via
// |hard_fault_count|. Returns true on success, false otherwise. Also returns
// whether or not the system call was even possible for the current OS version
// via |has_os_support|.
bool GetHardFaultCountForCurrentProcess(uint32_t* hard_fault_count,
                                        bool* has_os_support) {
  DCHECK(hard_fault_count);
  DCHECK(has_os_support);

  if (base::win::GetVersion() < base::win::VERSION_WIN7) {
    *has_os_support = false;
    return false;
  }
  // At this point the OS supports the required system call.
  *has_os_support = true;

  // Get the function pointer.
  NtQuerySystemInformationPtr query_sys_info =
      reinterpret_cast<NtQuerySystemInformationPtr>(
          ::GetProcAddress(GetModuleHandle(L"ntdll.dll"),
                           "NtQuerySystemInformation"));
  if (query_sys_info == nullptr)
    return false;

  // The output of this system call depends on the number of threads and
  // processes on the entire system, and this can change between calls. Retry
  // a small handful of times growing the buffer along the way.
  // NOTE: The actual required size depends entirely on the number of processes
  //       and threads running on the system. The initial guess suffices for
  //       ~100s of processes and ~1000s of threads.
  std::vector<uint8_t> buffer(32 * 1024);
  for (size_t tries = 0; tries < 3; ++tries) {
    ULONG return_length = 0;
    NTSTATUS status = query_sys_info(
        SystemProcessInformation,
        buffer.data(),
        static_cast<ULONG>(buffer.size()),
        &return_length);
    // Insufficient space in the buffer.
    if (return_length > buffer.size()) {
      buffer.resize(return_length);
      continue;
    }
    if (NT_SUCCESS(status) && return_length <= buffer.size())
      break;
    return false;
  }

  // Look for the struct housing information for the current process.
  DWORD proc_id = ::GetCurrentProcessId();
  size_t index = 0;
  while (index < buffer.size()) {
    DCHECK_LE(index + sizeof(SYSTEM_PROCESS_INFORMATION_EX), buffer.size());
    SYSTEM_PROCESS_INFORMATION_EX* proc_info =
        reinterpret_cast<SYSTEM_PROCESS_INFORMATION_EX*>(buffer.data() + index);
    if (reinterpret_cast<DWORD>(proc_info->UniqueProcessId) == proc_id) {
      *hard_fault_count = proc_info->HardFaultCount;
      return true;
    }
    // The list ends when NextEntryOffset is zero. This also prevents busy
    // looping if the data is in fact invalid.
    if (proc_info->NextEntryOffset <= 0)
      return false;
    index += proc_info->NextEntryOffset;
  }

  return false;
}

#endif  // defined(OS_WIN)


// Helper macro for splitting out an UMA histogram based on cold or warm start.
// |type| is the histogram type, and corresponds to an UMA macro like
// UMA_HISTOGRAM_LONG_TIMES. It must be itself be a macro that only takes two
// parameters.
// |basename| is the basename of the histogram. A histogram of this name will
// always be recorded to. If the startup is either cold or warm then a value
// will also be recorded to the histogram with name |basename| and suffix
// ".ColdStart" or ".WarmStart", as appropriate.
// |value_expr| is an expression evaluating to the value to be recorded. This
// will be evaluated exactly once and cached, so side effects are not an issue.
// A metric logged using this macro must have an affected-histogram entry in the
// definition of the StartupTemperature suffix in histograms.xml.
#define UMA_HISTOGRAM_WITH_STARTUP_TEMPERATURE(type, basename, value_expr) \
  {                                                                        \
    const auto kValue = value_expr;                                        \
    /* Always record to the base histogram. */                             \
    type(basename, kValue);                                                \
    /* Record to the cold/warm suffixed histogram as appropriate. */       \
    if (g_startup_temperature == COLD_STARTUP_TEMPERATURE) {               \
      type(basename ".ColdStartup", kValue);                               \
    } else if (g_startup_temperature == WARM_STARTUP_TEMPERATURE) {        \
      type(basename ".WarmStartup", kValue);                               \
    }                                                                      \
  }

#define UMA_HISTOGRAM_AND_TRACE_WITH_STARTUP_TEMPERATURE(                     \
    type, basename, begin_ticks, end_ticks)                                   \
  {                                                                           \
    UMA_HISTOGRAM_WITH_STARTUP_TEMPERATURE(type, basename,                    \
                                           end_ticks - begin_ticks)           \
    TRACE_EVENT_ASYNC_BEGIN_WITH_TIMESTAMP1(                                  \
        "startup", basename, 0, begin_ticks.ToInternalValue(), "Temperature", \
        g_startup_temperature);                                               \
    TRACE_EVENT_ASYNC_END_WITH_TIMESTAMP1(                                    \
        "startup", basename, 0, end_ticks.ToInternalValue(), "Temperature",   \
        g_startup_temperature);                                               \
  }

// On Windows, records the number of hard-faults that have occurred in the
// current chrome.exe process since it was started. This is a nop on other
// platforms.
// crbug.com/476923
// TODO(chrisha): If this proves useful, use it to split startup stats in two.
void RecordHardFaultHistogram(bool is_first_run) {
#if defined(OS_WIN)
  uint32_t hard_fault_count = 0;
  bool has_os_support = false;
  bool success = GetHardFaultCountForCurrentProcess(
      &hard_fault_count, &has_os_support);

  // Log whether or not the system call was successful, assuming the OS was
  // detected to support it.
  if (has_os_support) {
    UMA_HISTOGRAM_BOOLEAN(
        "Startup.BrowserMessageLoopStartHardFaultCount.Success",
        success);
  }

  // Don't log a histogram value if unable to get the hard fault count.
  if (!success)
    return;

  // Hard fault counts are expected to be in the thousands range,
  // corresponding to faulting in ~10s of MBs of code ~10s of KBs at a time.
  // (Observed to vary from 1000 to 10000 on various test machines and
  // platforms.)
  if (is_first_run) {
    UMA_HISTOGRAM_CUSTOM_COUNTS(
        "Startup.BrowserMessageLoopStartHardFaultCount.FirstRun",
        hard_fault_count,
        0, 40000, 50);
  } else {
    UMA_HISTOGRAM_CUSTOM_COUNTS(
        "Startup.BrowserMessageLoopStartHardFaultCount",
        hard_fault_count,
        0, 40000, 50);
  }

  // Determine the startup type based on the number of observed hard faults.
  DCHECK_EQ(UNCERTAIN_STARTUP_TEMPERATURE, g_startup_temperature);
  if (hard_fault_count < WARM_START_HARD_FAULT_COUNT_THRESHOLD) {
    g_startup_temperature = WARM_STARTUP_TEMPERATURE;
  } else if (hard_fault_count >= COLD_START_HARD_FAULT_COUNT_THRESHOLD) {
    g_startup_temperature = COLD_STARTUP_TEMPERATURE;
  }

  // Record the startup 'temperature'.
  UMA_HISTOGRAM_ENUMERATION(
      "Startup.Temperature", g_startup_temperature, STARTUP_TEMPERATURE_MAX);
#endif  // defined(OS_WIN)
}

// Converts a base::Time value to a base::TimeTicks value. The conversion isn't
// exact, but by capturing Time::Now() as early as possible, the likelihood of a
// clock change between it and process start is as low as possible. There is
// also the time taken to synchronously resolve base::Time::Now() and
// base::TimeTicks::Now() at play, but in practice it is pretty much instant
// compared to multi-seconds startup timings.
base::TimeTicks StartupTimeToTimeTicks(const base::Time& time) {
  // First get a base which represents the same point in time in both units.
  // Bump the priority of this thread while doing this as the wall clock time it
  // takes to resolve these two calls affects the precision of this method and
  // bumping the priority reduces the likelihood of a context switch interfering
  // with this computation.

// platform_thread_mac.mm unfortunately doesn't properly support base's
// thread priority APIs (crbug.com/554651).
#if !defined(OS_MACOSX)
  static bool statics_initialized = false;

  base::ThreadPriority previous_priority = base::ThreadPriority::NORMAL;
  if (!statics_initialized) {
    previous_priority = base::PlatformThread::GetCurrentThreadPriority();
    base::PlatformThread::SetCurrentThreadPriority(
        base::ThreadPriority::DISPLAY);
  }
#endif

  static const base::Time time_base = base::Time::Now();
  static const base::TimeTicks trace_ticks_base = base::TimeTicks::Now();

#if !defined(OS_MACOSX)
  if (!statics_initialized) {
    base::PlatformThread::SetCurrentThreadPriority(previous_priority);
  }

  statics_initialized = true;
#endif

  // Then use the TimeDelta common ground between the two units to make the
  // conversion.
  const base::TimeDelta delta_since_base = time_base - time;
  return trace_ticks_base - delta_since_base;
}

// Record time of main entry so it can be read from Telemetry performance tests.
// TODO(jeremy): Remove once crbug.com/317481 is fixed.
void RecordMainEntryTimeHistogram() {
  const int kLowWordMask = 0xFFFFFFFF;
  const int kLower31BitsMask = 0x7FFFFFFF;
  DCHECK(!g_browser_main_entry_point_time.Get().is_null());
  const base::TimeDelta browser_main_entry_time_absolute =
      g_browser_main_entry_point_time.Get() - base::Time::UnixEpoch();

  const uint64 browser_main_entry_time_raw_ms =
      browser_main_entry_time_absolute.InMilliseconds();

  const base::TimeDelta browser_main_entry_time_raw_ms_high_word =
      base::TimeDelta::FromMilliseconds(
          (browser_main_entry_time_raw_ms >> 32) & kLowWordMask);
  // Shift by one because histograms only support non-negative values.
  const base::TimeDelta browser_main_entry_time_raw_ms_low_word =
      base::TimeDelta::FromMilliseconds(
          (browser_main_entry_time_raw_ms >> 1) & kLower31BitsMask);

  // A timestamp is a 64 bit value, yet histograms can only store 32 bits.
  // TODO(gabadie): Once startup_with_url.* benchmarks are replaced by
  //    startup_with_url2.*, remove this dirty hack (crbug.com/539287).
  LOCAL_HISTOGRAM_TIMES("Startup.BrowserMainEntryTimeAbsoluteHighWord",
      browser_main_entry_time_raw_ms_high_word);
  LOCAL_HISTOGRAM_TIMES("Startup.BrowserMainEntryTimeAbsoluteLowWord",
      browser_main_entry_time_raw_ms_low_word);
}

// Record renderer main entry time histogram.
void RecordRendererMainEntryHistogram() {
  const base::TimeTicks& browser_main_entry_point_ticks =
      g_browser_main_entry_point_ticks.Get();
  const base::TimeTicks& renderer_main_entry_point_ticks =
      g_renderer_main_entry_point_ticks.Get();

  if (!browser_main_entry_point_ticks.is_null() &&
      !renderer_main_entry_point_ticks.is_null()) {
    UMA_HISTOGRAM_AND_TRACE_WITH_STARTUP_TEMPERATURE(
        UMA_HISTOGRAM_LONG_TIMES_100, "Startup.BrowserMainToRendererMain",
        browser_main_entry_point_ticks, renderer_main_entry_point_ticks);
  }
}

// Environment variable that stores the timestamp when the executable's main()
// function was entered in TimeTicks. This is required because chrome.exe and
// chrome.dll don't share the same static storage.
const char kChromeMainTicksEnvVar[] = "CHROME_MAIN_TICKS";

// Returns the time of main entry recorded from RecordExeMainEntryTime.
base::TimeTicks ExeMainEntryPointTicks() {
  scoped_ptr<base::Environment> env(base::Environment::Create());
  std::string ticks_string;
  int64 time_int = 0;
  if (env->GetVar(kChromeMainTicksEnvVar, &ticks_string) &&
      base::StringToInt64(ticks_string, &time_int)) {
    return base::TimeTicks::FromInternalValue(time_int);
  }
  return base::TimeTicks();
}

void AddStartupEventsForTelemetry()
{
  DCHECK(!g_browser_main_entry_point_ticks.Get().is_null());

  TRACE_EVENT_INSTANT_WITH_TIMESTAMP0(
      "startup", "Startup.BrowserMainEntryPoint", 0,
      g_browser_main_entry_point_ticks.Get().ToInternalValue());

  if (!g_process_creation_ticks.Get().is_null())
  {
    TRACE_EVENT_INSTANT_WITH_TIMESTAMP0(
        "startup", "Startup.BrowserProcessCreation", 0,
        g_process_creation_ticks.Get().ToInternalValue());
  }
}

}  // namespace

bool WasNonBrowserUIDisplayed() {
  return g_non_browser_ui_displayed;
}

void SetNonBrowserUIDisplayed() {
  g_non_browser_ui_displayed = true;
}

void RecordStartupProcessCreationTime(const base::Time& time) {
  DCHECK(g_process_creation_ticks.Get().is_null());
  g_process_creation_ticks.Get() = StartupTimeToTimeTicks(time);
  DCHECK(!g_process_creation_ticks.Get().is_null());
}

void RecordMainEntryPointTime(const base::Time& time) {
  DCHECK(g_browser_main_entry_point_ticks.Get().is_null());
  g_browser_main_entry_point_ticks.Get() = StartupTimeToTimeTicks(time);
  DCHECK(!g_browser_main_entry_point_ticks.Get().is_null());

  // TODO(jeremy): Remove this with RecordMainEntryTimeHistogram() when
  // resolving crbug.com/317481.
  DCHECK(g_browser_main_entry_point_time.Get().is_null());
  g_browser_main_entry_point_time.Get() = time;
  DCHECK(!g_browser_main_entry_point_time.Get().is_null());
}

void RecordExeMainEntryPointTime(const base::Time& time) {
  const std::string exe_load_ticks =
      base::Int64ToString(StartupTimeToTimeTicks(time).ToInternalValue());
  scoped_ptr<base::Environment> env(base::Environment::Create());
  env->SetVar(kChromeMainTicksEnvVar, exe_load_ticks);
}

void RecordBrowserMainMessageLoopStart(const base::TimeTicks& ticks,
                                       bool is_first_run) {
  AddStartupEventsForTelemetry();
  RecordHardFaultHistogram(is_first_run);
  RecordMainEntryTimeHistogram();

  const base::TimeTicks& process_creation_ticks =
      g_process_creation_ticks.Get();
  if (!is_first_run && !process_creation_ticks.is_null()) {
    UMA_HISTOGRAM_AND_TRACE_WITH_STARTUP_TEMPERATURE(
        UMA_HISTOGRAM_LONG_TIMES_100, "Startup.BrowserMessageLoopStartTime",
        process_creation_ticks, ticks);
  }

  // Bail if uptime < 7 minutes, to filter out cases where Chrome may have been
  // autostarted and the machine is under io pressure.
  if (base::SysInfo::Uptime() < base::TimeDelta::FromMinutes(7))
    return;

  // The Startup.BrowserMessageLoopStartTime histogram exhibits instability in
  // the field which limits its usefulness in all scenarios except when we have
  // a very large sample size. Attempt to mitigate this with a new metric:
  // * Measure time from main entry rather than the OS' notion of process start.
  // * Only measure launches that occur 7 minutes after boot to try to avoid
  //   cases where Chrome is auto-started and IO is heavily loaded.
  if (is_first_run) {
    UMA_HISTOGRAM_AND_TRACE_WITH_STARTUP_TEMPERATURE(
        UMA_HISTOGRAM_LONG_TIMES,
        "Startup.BrowserMessageLoopStartTimeFromMainEntry.FirstRun",
        g_browser_main_entry_point_ticks.Get(), ticks);
  } else {
    UMA_HISTOGRAM_AND_TRACE_WITH_STARTUP_TEMPERATURE(
        UMA_HISTOGRAM_LONG_TIMES,
        "Startup.BrowserMessageLoopStartTimeFromMainEntry",
        g_browser_main_entry_point_ticks.Get(), ticks);
  }

  // Record timings between process creation, the main() in the executable being
  // reached and the main() in the shared library being reached.
  if (!process_creation_ticks.is_null()) {
    const base::TimeTicks exe_main_ticks = ExeMainEntryPointTicks();
    if (!exe_main_ticks.is_null()) {
      // Process create to chrome.exe:main().
      UMA_HISTOGRAM_AND_TRACE_WITH_STARTUP_TEMPERATURE(
          UMA_HISTOGRAM_LONG_TIMES, "Startup.LoadTime.ProcessCreateToExeMain",
          process_creation_ticks, exe_main_ticks);

      // chrome.exe:main() to chrome.dll:main().
      UMA_HISTOGRAM_AND_TRACE_WITH_STARTUP_TEMPERATURE(
          UMA_HISTOGRAM_LONG_TIMES, "Startup.LoadTime.ExeMainToDllMain",
          exe_main_ticks, g_browser_main_entry_point_ticks.Get());

      // Process create to chrome.dll:main(). Reported as a histogram only as
      // the other two events above are sufficient for tracing purposes.
      UMA_HISTOGRAM_WITH_STARTUP_TEMPERATURE(
          UMA_HISTOGRAM_LONG_TIMES, "Startup.LoadTime.ProcessCreateToDllMain",
          g_browser_main_entry_point_ticks.Get() - process_creation_ticks);
    }
  }
}

void RecordBrowserWindowDisplay(const base::TimeTicks& ticks) {
  static bool is_first_call = true;
  if (!is_first_call || ticks.is_null())
    return;
  is_first_call = false;
  if (WasNonBrowserUIDisplayed() || g_process_creation_ticks.Get().is_null())
    return;

  UMA_HISTOGRAM_AND_TRACE_WITH_STARTUP_TEMPERATURE(
      UMA_HISTOGRAM_LONG_TIMES, "Startup.BrowserWindowDisplay",
      g_process_creation_ticks.Get(), ticks);
}

void RecordBrowserOpenTabsDelta(const base::TimeDelta& delta) {
  static bool is_first_call = true;
  if (!is_first_call)
    return;
  is_first_call = false;

  UMA_HISTOGRAM_WITH_STARTUP_TEMPERATURE(UMA_HISTOGRAM_LONG_TIMES_100,
                                         "Startup.BrowserOpenTabs", delta);
}

void RecordRendererMainEntryTime(const base::TimeTicks& ticks) {
  // Record the renderer main entry time, but don't log the UMA metric
  // immediately because the startup temperature is not known yet.
  if (g_renderer_main_entry_point_ticks.Get().is_null())
    g_renderer_main_entry_point_ticks.Get() = ticks;
}

void RecordFirstWebContentsMainFrameLoad(const base::TimeTicks& ticks) {
  static bool is_first_call = true;
  if (!is_first_call || ticks.is_null())
    return;
  is_first_call = false;
  if (WasNonBrowserUIDisplayed() || g_process_creation_ticks.Get().is_null())
    return;

  UMA_HISTOGRAM_AND_TRACE_WITH_STARTUP_TEMPERATURE(
      UMA_HISTOGRAM_LONG_TIMES_100, "Startup.FirstWebContents.MainFrameLoad2",
      g_process_creation_ticks.Get(), ticks);
}

void RecordFirstWebContentsNonEmptyPaint(const base::TimeTicks& ticks) {
  static bool is_first_call = true;
  if (!is_first_call || ticks.is_null())
    return;
  is_first_call = false;

  // Log Startup.BrowserMainToRendererMain now that the first renderer main
  // entry time and the startup temperature are known.
  RecordRendererMainEntryHistogram();

  if (WasNonBrowserUIDisplayed() || g_process_creation_ticks.Get().is_null())
    return;

  UMA_HISTOGRAM_AND_TRACE_WITH_STARTUP_TEMPERATURE(
      UMA_HISTOGRAM_LONG_TIMES_100, "Startup.FirstWebContents.NonEmptyPaint2",
      g_process_creation_ticks.Get(), ticks);
}

void RecordFirstWebContentsMainNavigationStart(const base::TimeTicks& ticks) {
  static bool is_first_call = true;
  if (!is_first_call || ticks.is_null())
    return;
  is_first_call = false;
  if (WasNonBrowserUIDisplayed() || g_process_creation_ticks.Get().is_null())
    return;

  UMA_HISTOGRAM_AND_TRACE_WITH_STARTUP_TEMPERATURE(
      UMA_HISTOGRAM_LONG_TIMES_100,
      "Startup.FirstWebContents.MainNavigationStart",
      g_process_creation_ticks.Get(), ticks);
}

void RecordFirstWebContentsMainNavigationFinished(
    const base::TimeTicks& ticks) {
  static bool is_first_call = true;
  if (!is_first_call || ticks.is_null())
    return;
  is_first_call = false;
  if (WasNonBrowserUIDisplayed() || g_process_creation_ticks.Get().is_null())
    return;

  UMA_HISTOGRAM_AND_TRACE_WITH_STARTUP_TEMPERATURE(
      UMA_HISTOGRAM_LONG_TIMES_100,
      "Startup.FirstWebContents.MainNavigationFinished",
      g_process_creation_ticks.Get(), ticks);
}

base::TimeTicks MainEntryPointTicks() {
  return g_browser_main_entry_point_ticks.Get();
}

StartupTemperature GetStartupTemperature() {
  return g_startup_temperature;
}

}  // namespace startup_metric_utils
