// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/installer/util/delete_tree_work_item.h"

#include <algorithm>
#include <limits>

#include "base/file_util.h"
#include "base/logging.h"

namespace {

// Casts a value of an unsigned type to a signed type of the same size provided
// that there is no overflow.
template<typename L, typename R>
bool SafeCast(L left, R* right) {
  DCHECK(right);
  COMPILE_ASSERT(sizeof(left) == sizeof(right),
                 must_add_support_for_crazy_data_types);
  if (left > static_cast<L>(std::numeric_limits<R>::max()))
    return false;
  *right = static_cast<L>(left);
  return true;
}

}  // namespace

DeleteTreeWorkItem::DeleteTreeWorkItem(
    const base::FilePath& root_path,
    const base::FilePath& temp_path,
    const std::vector<base::FilePath>& key_paths)
    : root_path_(root_path),
      temp_path_(temp_path),
      copied_to_backup_(false) {
  if (!SafeCast(key_paths.size(), &num_key_files_)) {
    NOTREACHED() << "Impossibly large key_paths collection";
  } else if (num_key_files_ != 0) {
    key_paths_.reset(new base::FilePath[num_key_files_]);
    key_backup_paths_.reset(new base::ScopedTempDir[num_key_files_]);
    std::copy(key_paths.begin(), key_paths.end(), &key_paths_[0]);
  }
}

DeleteTreeWorkItem::~DeleteTreeWorkItem() {
}

// We first try to move key_path_ to backup_path. If it succeeds, we go ahead
// and move the rest.
bool DeleteTreeWorkItem::Do() {
  // Go through all the key files and see if we can open them exclusively
  // with only the FILE_SHARE_DELETE flag.  Once we know we have all of them,
  // we can delete them.
  std::vector<HANDLE> opened_key_files;
  opened_key_files.reserve(num_key_files_);
  bool abort = false;
  for (ptrdiff_t i = 0; !abort && i != num_key_files_; ++i) {
    base::FilePath& key_file = key_paths_[i];
    base::ScopedTempDir& backup = key_backup_paths_[i];
    if (!ignore_failure_) {
      if (!backup.CreateUniqueTempDirUnderPath(temp_path_)) {
        PLOG(ERROR) << "Could not create temp dir in " << temp_path_.value();
        abort = true;
      } else if (!base::CopyFile(key_file,
                     backup.path().Append(key_file.BaseName()))) {
        PLOG(ERROR) << "Could not back up " << key_file.value()
                    << " to directory " << backup.path().value();
        abort = true;
        backup.Delete();
      }
    }
    if (!abort) {
      HANDLE file = ::CreateFile(key_file.value().c_str(), FILE_ALL_ACCESS,
                                 FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0,
                                 NULL);
      if (file != INVALID_HANDLE_VALUE) {
        VLOG(1) << "Acquired exclusive lock for key file: " << key_file.value();
        opened_key_files.push_back(file);
      } else {
        if (::GetLastError() != ERROR_FILE_NOT_FOUND)
          abort = true;
        PLOG(INFO) << "Failed to open " << key_file.value();
      }
    }
  }

  if (!abort) {
    // We now hold exclusive locks with "share delete" permissions for each
    // of the key files and also have created backups of those files.
    // We can safely delete the key files now.
    for (ptrdiff_t i = 0; !abort && i != num_key_files_; ++i) {
      base::FilePath& key_file = key_paths_[i];
      if (!base::DeleteFile(key_file, true)) {
        // This should not really be possible because of the above.
        PLOG(DFATAL) << "Unexpectedly could not delete " << key_file.value();
        abort = true;
      }
    }
  }

  std::for_each(opened_key_files.begin(), opened_key_files.end(), CloseHandle);
  opened_key_files.clear();

  if (abort) {
    LOG(ERROR) << "Could not exclusively hold all key files.";
    return ignore_failure_;
  }

  // Now that we've taken care of the key files, take care of the rest.
  if (!root_path_.empty() && base::PathExists(root_path_)) {
    if (!ignore_failure_) {
      if (!backup_path_.CreateUniqueTempDirUnderPath(temp_path_)) {
        PLOG(ERROR) << "Failed to get backup path in folder "
                    << temp_path_.value();
        return false;
      } else {
        base::FilePath backup =
            backup_path_.path().Append(root_path_.BaseName());
        if (!base::CopyDirectory(root_path_, backup, true)) {
          LOG(ERROR) << "can not copy " << root_path_.value()
                     << " to backup path " << backup.value();
          return false;
        } else {
          copied_to_backup_ = true;
        }
      }
    }
    if (!base::DeleteFile(root_path_, true)) {
      LOG(ERROR) << "can not delete " << root_path_.value();
      return ignore_failure_;
    }
  }

  return true;
}

// If there are files in backup paths move them back.
void DeleteTreeWorkItem::Rollback() {
  if (ignore_failure_)
    return;

  if (copied_to_backup_) {
    DCHECK(!backup_path_.path().empty());
    base::FilePath backup = backup_path_.path().Append(root_path_.BaseName());
    if (base::PathExists(backup))
      base::Move(backup, root_path_);
  }

  for (ptrdiff_t i = 0; i != num_key_files_; ++i) {
    base::ScopedTempDir& backup_dir = key_backup_paths_[i];
    if (!backup_dir.path().empty()) {
      base::FilePath& key_file = key_paths_[i];
      base::FilePath backup_file =
          backup_dir.path().Append(key_file.BaseName());
      if (base::PathExists(backup_file) &&
          !base::Move(backup_file, key_file)) {
        // This could happen if we could not delete the key file to begin with.
        PLOG(WARNING) << "Rollback: Failed to move backup file back in place: "
                      << backup_file.value() << " to " << key_file.value();
      }
    }
  }
}
