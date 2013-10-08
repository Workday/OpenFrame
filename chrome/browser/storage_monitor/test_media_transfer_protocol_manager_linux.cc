// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/storage_monitor/test_media_transfer_protocol_manager_linux.h"

#include "device/media_transfer_protocol/mtp_file_entry.pb.h"

namespace chrome {

TestMediaTransferProtocolManagerLinux::
TestMediaTransferProtocolManagerLinux() {}

TestMediaTransferProtocolManagerLinux::
~TestMediaTransferProtocolManagerLinux() {}

void TestMediaTransferProtocolManagerLinux::AddObserver(Observer* observer) {}

void TestMediaTransferProtocolManagerLinux::RemoveObserver(
    Observer* observer) {}

const std::vector<std::string>
TestMediaTransferProtocolManagerLinux::GetStorages() const {
    return std::vector<std::string>();
}
const MtpStorageInfo* TestMediaTransferProtocolManagerLinux::GetStorageInfo(
    const std::string& storage_name) const {
  return NULL;
}

void TestMediaTransferProtocolManagerLinux::OpenStorage(
    const std::string& storage_name,
    const std::string& mode,
    const OpenStorageCallback& callback) {
  callback.Run("", true);
}

void TestMediaTransferProtocolManagerLinux::CloseStorage(
    const std::string& storage_handle,
    const CloseStorageCallback& callback) {
  callback.Run(true);
}

void TestMediaTransferProtocolManagerLinux::ReadDirectoryByPath(
    const std::string& storage_handle,
    const std::string& path,
    const ReadDirectoryCallback& callback) {
  callback.Run(std::vector<MtpFileEntry>(), true);
}

void TestMediaTransferProtocolManagerLinux::ReadDirectoryById(
    const std::string& storage_handle,
    uint32 file_id,
    const ReadDirectoryCallback& callback) {
  callback.Run(std::vector<MtpFileEntry>(), true);
}

void TestMediaTransferProtocolManagerLinux::ReadFileChunkByPath(
    const std::string& storage_handle,
    const std::string& path,
    uint32 offset,
    uint32 count,
    const ReadFileCallback& callback) {
  callback.Run(std::string(), true);
}

void TestMediaTransferProtocolManagerLinux::ReadFileChunkById(
    const std::string& storage_handle,
    uint32 file_id,
    uint32 offset,
    uint32 count,
    const ReadFileCallback& callback) {
  callback.Run(std::string(), true);
}

void TestMediaTransferProtocolManagerLinux::GetFileInfoByPath(
    const std::string& storage_handle,
    const std::string& path,
    const GetFileInfoCallback& callback) {
  callback.Run(MtpFileEntry(), true);
}

void TestMediaTransferProtocolManagerLinux::GetFileInfoById(
    const std::string& storage_handle,
    uint32 file_id,
    const GetFileInfoCallback& callback) {
  callback.Run(MtpFileEntry(), true);
}

}  // namespace chrome
