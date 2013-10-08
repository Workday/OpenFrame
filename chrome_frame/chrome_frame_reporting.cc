// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of wrapper around common crash reporting.

#include "chrome_frame/chrome_frame_reporting.h"

#include "base/basictypes.h"
#include "base/file_util.h"
#include "base/file_version_info.h"
#include "base/win/win_util.h"
#include "chrome/installer/util/google_update_settings.h"
#include "chrome/installer/util/install_util.h"
#include "chrome_frame/crash_reporting/crash_report.h"
#include "chrome_frame/exception_barrier.h"
#include "chrome_frame/utils.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {

// Well known SID for the system principal.
const wchar_t kSystemPrincipalSid[] = L"S-1-5-18";
const wchar_t kChromePipeName[] = L"\\\\.\\pipe\\ChromeCrashServices";

// Returns the custom info structure based on the dll in parameter
google_breakpad::CustomClientInfo* GetCustomInfo(const wchar_t* dll_path) {
  std::wstring product;
  std::wstring version;
  scoped_ptr<FileVersionInfo> version_info(
      FileVersionInfo::CreateFileVersionInfo(base::FilePath(dll_path)));
  if (version_info.get()) {
    version = version_info->product_version();
    product = version_info->product_short_name();
  }

  if (version.empty())
    version = L"0.1.0.0";

  if (product.empty())
    product = L"ChromeFrame";

  static google_breakpad::CustomInfoEntry ver_entry(L"ver", version.c_str());
  static google_breakpad::CustomInfoEntry prod_entry(L"prod", product.c_str());
  static google_breakpad::CustomInfoEntry plat_entry(L"plat", L"Win32");
  static google_breakpad::CustomInfoEntry type_entry(L"ptype", L"chrome_frame");
  static google_breakpad::CustomInfoEntry entries[] = {
      ver_entry, prod_entry, plat_entry, type_entry };
  static google_breakpad::CustomClientInfo custom_info = {
      entries, arraysize(entries) };
  return &custom_info;
}

bool InitializeCrashReporting() {
  // In headless mode we want crashes to be reported back.
  bool always_take_dump = IsHeadlessMode();
  // We want to use the Google Update crash reporting. We need to check if the
  // user allows it first.
  if (!always_take_dump && !GoogleUpdateSettings::GetCollectStatsConsent())
    return true;

  // If we got here, we want to report crashes, so make sure all
  // ExceptionBarrierBase instances do so.
  ExceptionBarrierConfig::set_enabled(true);

  // Get the alternate dump directory. We use the temp path.
  base::FilePath temp_directory;
  if (!file_util::GetTempDir(&temp_directory) || temp_directory.empty())
    return false;

  wchar_t dll_path[MAX_PATH * 2] = {0};
  GetModuleFileName(reinterpret_cast<HMODULE>(&__ImageBase), dll_path,
                    arraysize(dll_path));

  if (always_take_dump) {
    return InitializeVectoredCrashReportingWithPipeName(
        true, kChromePipeName, temp_directory.value(), GetCustomInfo(dll_path));
  }

  // Build the pipe name. It can be either:
  // System-wide install: "NamedPipe\GoogleCrashServices\S-1-5-18"
  // Per-user install: "NamedPipe\GoogleCrashServices\<user SID>"
  std::wstring user_sid;
  if (InstallUtil::IsPerUserInstall(dll_path)) {
    if (!base::win::GetUserSidString(&user_sid))
      return false;
  } else {
    user_sid = kSystemPrincipalSid;
  }

  return InitializeVectoredCrashReporting(
      false, user_sid.c_str(), temp_directory.value(), GetCustomInfo(dll_path));
}

bool ShutdownCrashReporting() {
  ExceptionBarrierConfig::set_enabled(false);
  return ShutdownVectoredCrashReporting();
}

}  // namespace

namespace chrome_frame {

void CrashReportingTraits::Initialize() {
  InitializeCrashReporting();
}

void CrashReportingTraits::Shutdown() {
  ShutdownCrashReporting();
}

}  // namespace chrome_frame
