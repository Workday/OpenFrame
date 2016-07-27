// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_CRASH_CONTENT_APP_CRASHPAD_H_
#define COMPONENTS_CRASH_CONTENT_APP_CRASHPAD_H_

#include <time.h>

#include <string>
#include <vector>

#include "base/files/file_path.h"

namespace crash_reporter {

// Initializes Crashpad in a way that is appropriate for initial_client and
// process_type.
//
// If initial_client is true, this starts crashpad_handler and sets it as the
// exception handler. Child processes will inherit this exception handler, and
// should specify false for this parameter. Although they inherit the exception
// handler, child processes still need to call this function to perform
// additional initialization.
//
// If process_type is empty, initialization will be done for the browser
// process. The browser process performs additional initialization of the crash
// report database. The browser process is also the only process type that is
// eligible to have its crashes forwarded to the system crash report handler (in
// release mode only). Note that when process_type is empty, initial_client must
// be true.
//
// On Mac, process_type may be non-empty with initial_client set to true. This
// indicates that an exception handler has been inherited but should be
// discarded in favor of a new Crashpad handler. This configuration should be
// used infrequently. It is provided to allow an install-from-.dmg relauncher
// process to disassociate from an old Crashpad handler so that after performing
// an installation from a disk image, the relauncher process may unmount the
// disk image that contains its inherited crashpad_handler. This is only
// supported when initial_client is true and process_type is "relauncher".
void InitializeCrashpad(bool initial_client, const std::string& process_type);

// Enables or disables crash report upload. This is a property of the Crashpad
// database. In a newly-created database, uploads will be disabled. This
// function only has an effect when called in the browser process. Its effect is
// immediate and applies to all other process types, including processes that
// are already running.
void SetUploadsEnabled(bool enabled);

// Determines whether uploads are enabled or disabled. This information is only
// available in the browser process.
bool GetUploadsEnabled();

struct UploadedReport {
  std::string local_id;
  std::string remote_id;
  time_t creation_time;
};

// Obtains a list of reports uploaded to the collection server. This function
// only operates when called in the browser process. All reports in the Crashpad
// database that have been successfully uploaded will be included in this list.
// The list will be sorted in descending order by report creation time (newest
// reports first).
//
// TODO(mark): The about:crashes UI expects to show only uploaded reports. If it
// is ever enhanced to work well with un-uploaded reports, those should be
// returned as well. Un-uploaded reports may have a pending upload, may have
// experienced upload failure, or may have been collected while uploads were
// disabled.
void GetUploadedReports(std::vector<UploadedReport>* uploaded_reports);

namespace internal {

// The platform-specific portion of InitializeCrashpad().
// Returns the database path, if initializing in the browser process.
base::FilePath PlatformCrashpadInitialization(bool initial_client,
                                              bool browser_process);

}  // namespace internal

}  // namespace crash_reporter

#endif  // COMPONENTS_CRASH_CONTENT_APP_CRASHPAD_H_
