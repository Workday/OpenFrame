// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of a work item that replaces the contents of one registry key
// with that of another (i.e., the destination is erased prior to the copy).

#include "chrome/installer/util/copy_reg_key_work_item.h"

#include <shlwapi.h>

#include "base/logging.h"
#include "base/win/registry.h"

using base::win::RegKey;

CopyRegKeyWorkItem::~CopyRegKeyWorkItem() {
}

CopyRegKeyWorkItem::CopyRegKeyWorkItem(HKEY predefined_root,
                                       const std::wstring& source_key_path,
                                       const std::wstring& dest_key_path,
                                       CopyOverWriteOption overwrite_option)
    : predefined_root_(predefined_root),
      source_key_path_(source_key_path),
      dest_key_path_(dest_key_path),
      overwrite_option_(overwrite_option),
      cleared_destination_(false) {
  DCHECK(predefined_root);
  // It's a safe bet that we don't want to copy or overwrite one of the root
  // trees.
  DCHECK(!source_key_path.empty());
  DCHECK(!dest_key_path.empty());
  DCHECK(overwrite_option == ALWAYS || overwrite_option == IF_NOT_PRESENT);
}

bool CopyRegKeyWorkItem::Do() {
  if (source_key_path_.empty() || dest_key_path_.empty())
    return false;

  // Leave immediately if we're not supposed to overwrite an existing key and
  // one is there.
  if (overwrite_option_ == IF_NOT_PRESENT &&
      RegKey(predefined_root_, dest_key_path_.c_str(),
             KEY_QUERY_VALUE).Valid()) {
    return true;
  }

  RegistryKeyBackup backup;
  RegKey dest_key;

  // Only try to make a backup if we're not configured to ignore failures.
  if (!ignore_failure_) {
    if (!backup.Initialize(predefined_root_, dest_key_path_.c_str())) {
      LOG(ERROR) << "Failed to backup destination for registry key copy.";
      return false;
    }
  }

  // Delete the destination before attempting to copy.
  LONG result = SHDeleteKey(predefined_root_, dest_key_path_.c_str());
  if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) {
    LOG(ERROR) << "Failed to delete key at " << dest_key_path_ << ", result: "
               << result;
  } else {
    cleared_destination_ = true;
    // We've just modified the registry, so remember any backup we may have
    // made so that Rollback can take us back where we started.
    backup_.swap(backup);
    // Make the copy.
    result = dest_key.Create(predefined_root_, dest_key_path_.c_str(),
                             KEY_WRITE);
    if (result != ERROR_SUCCESS) {
      LOG(ERROR) << "Failed to open destination key at " << dest_key_path_
                 << ", result: " << result;
    } else {
      result = SHCopyKey(predefined_root_, source_key_path_.c_str(),
                         dest_key.Handle(), 0);
      switch (result) {
        case ERROR_FILE_NOT_FOUND:
          // The source didn't exist, so neither should the destination.
          dest_key.Close();
          SHDeleteKey(predefined_root_, dest_key_path_.c_str());
          // Handle like a success.
          result = ERROR_SUCCESS;
          // -- Fall through to success case. --
        case ERROR_SUCCESS:
          break;
        default:
          LOG(ERROR) << "Failed to copy key from " << source_key_path_ << " to "
                     << dest_key_path_ << ", result: " << result;
          break;
      }
    }
  }

  return ignore_failure_ ? true : (result == ERROR_SUCCESS);
}

void CopyRegKeyWorkItem::Rollback() {
  if (ignore_failure_)
    return;

  if (cleared_destination_) {
    // Delete anything in the key before restoring the backup in case new data
    // was written after Do().
    LONG result = SHDeleteKey(predefined_root_, dest_key_path_.c_str());
    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) {
      LOG(ERROR) << "Failed to delete key at " << dest_key_path_
                 << " in rollback, result: " << result;
    }

    // Restore the old contents.  The restoration takes on its default security
    // attributes; any custom attributes are lost.
    if (!backup_.WriteTo(predefined_root_, dest_key_path_.c_str()))
      LOG(ERROR) << "Failed to restore key in rollback.";
  }
}
