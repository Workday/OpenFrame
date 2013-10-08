// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_MEDIA_GALLERIES_LINUX_SNAPSHOT_FILE_DETAILS_H_
#define CHROME_BROWSER_MEDIA_GALLERIES_LINUX_SNAPSHOT_FILE_DETAILS_H_

#include <string>

#include "base/basictypes.h"
#include "base/callback.h"
#include "base/files/file_path.h"
#include "base/platform_file.h"
#include "chrome/browser/media_galleries/fileapi/mtp_device_async_delegate.h"

namespace chrome {

// Used to represent snapshot file request params.
struct SnapshotRequestInfo {
  SnapshotRequestInfo(
      const std::string& device_file_path,
      const base::FilePath& snapshot_file_path,
      const MTPDeviceAsyncDelegate::CreateSnapshotFileSuccessCallback&
          success_callback,
      const MTPDeviceAsyncDelegate::ErrorCallback& error_callback);
  ~SnapshotRequestInfo();

  // MTP device file path.
  const std::string device_file_path;

  // Local platform path of the snapshot file.
  const base::FilePath snapshot_file_path;

  // A callback to be called when CreateSnapshotFile() succeeds.
  const MTPDeviceAsyncDelegate::CreateSnapshotFileSuccessCallback
      success_callback;

  // A callback to be called when CreateSnapshotFile() fails.
  const MTPDeviceAsyncDelegate::ErrorCallback error_callback;
};

// SnapshotFileDetails tracks the current state of the snapshot file (e.g how
// many bytes written to the snapshot file, source file details, snapshot file
// metadata information, etc).
class SnapshotFileDetails {
 public:
  SnapshotFileDetails(const SnapshotRequestInfo& request_info,
                      const base::PlatformFileInfo& file_info);

  ~SnapshotFileDetails();

  std::string device_file_path() const {
    return request_info_.device_file_path;
  }

  base::FilePath snapshot_file_path() const {
    return request_info_.snapshot_file_path;
  }

  uint32 bytes_written() const {
    return bytes_written_;
  }

  const base::PlatformFileInfo file_info() const {
    return file_info_;
  }

  const MTPDeviceAsyncDelegate::CreateSnapshotFileSuccessCallback
      success_callback() const {
    return request_info_.success_callback;
  }

  const MTPDeviceAsyncDelegate::ErrorCallback error_callback() const {
    return request_info_.error_callback;
  }

  bool error_occurred() const {
    return error_occurred_;
  }

  void set_error_occurred(bool error);

  // Adds |bytes_written| to |bytes_written_|.
  // |bytes_written| specifies the total number of bytes transferred during the
  // last write operation.
  // If |bytes_written| is valid, returns true and adds |bytes_written| to
  // |bytes_written_|.
  // If |bytes_written| is invalid, returns false and does not add
  // |bytes_written| to |bytes_written_|.
  bool AddBytesWritten(uint32 bytes_written);

  // Returns true if the snapshot file is created successfully (no more write
  // operation is required to complete the snapshot file).
  bool IsSnapshotFileWriteComplete() const;

  uint32 BytesToRead() const;

 private:
  // Snapshot file request params.
  const SnapshotRequestInfo request_info_;

  // Metadata of the snapshot file (such as name, size, type, etc).
  const base::PlatformFileInfo file_info_;

  // Number of bytes written into the snapshot file.
  uint32 bytes_written_;

  // Whether an error occurred during file transfer.
  bool error_occurred_;

  DISALLOW_COPY_AND_ASSIGN(SnapshotFileDetails);
};

}  // namespace chrome

#endif  // CHROME_BROWSER_MEDIA_GALLERIES_LINUX_SNAPSHOT_FILE_DETAILS_H_
