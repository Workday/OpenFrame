// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_FILE_SYSTEM_DRIVE_BACKEND_DRIVE_FILE_SYNC_UTIL_H_
#define CHROME_BROWSER_SYNC_FILE_SYSTEM_DRIVE_BACKEND_DRIVE_FILE_SYNC_UTIL_H_

#include "chrome/browser/google_apis/gdata_errorcode.h"
#include "chrome/browser/sync_file_system/sync_status_code.h"

namespace sync_file_system {

// Translates GDataErrorCode to SyncStatusCode.
SyncStatusCode GDataErrorCodeToSyncStatusCode(
    google_apis::GDataErrorCode error);

// Enables or disables Drive API in Sync FileSystem API.
// TODO(nhiroki): This method should go away when we completely migrate to
// DriveAPI. (http://crbug.com/234557)
void SetDisableDriveAPI(bool flag);

// Returns true if we disable Drive API in Sync FileSystem API.
// It is enabled by default but can be overridden by a command-line switch
// (--disable-drive-api-for-syncfs) or by calling SetDisableDriveAPI().
// TODO(nhiroki): This method should go away when we completely migrate to
// DriveAPI. (http://crbug.com/234557)
bool IsDriveAPIDisabled();

class ScopedDisableDriveAPI {
 public:
  ScopedDisableDriveAPI();
  ~ScopedDisableDriveAPI();

 private:
  bool was_disabled_;

  DISALLOW_COPY_AND_ASSIGN(ScopedDisableDriveAPI);
};

}  // namespace sync_file_system

#endif  // CHROME_BROWSER_SYNC_FILE_SYSTEM_DRIVE_BACKEND_DRIVE_FILE_SYNC_UTIL_H_
