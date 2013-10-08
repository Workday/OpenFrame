// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// TestPortableDeviceWatcherWin implementation.

#include "chrome/browser/storage_monitor/test_portable_device_watcher_win.h"

#include <vector>

#include "base/strings/utf_string_conversions.h"

namespace chrome {
namespace test {
namespace {

// Sample MTP device storage information.
const char16 kMTPDeviceFriendlyName[] = L"Camera V1.1";
const char16 kStorageLabelA[] = L"Camera V1.1 (s10001)";
const char16 kStorageLabelB[] = L"Camera V1.1 (s20001)";
const char16 kStorageObjectIdA[] = L"s10001";
const char16 kStorageObjectIdB[] = L"s20001";
const char kStorageUniqueIdB[] =
    "mtp:StorageSerial:SID-{s20001, S, 2238}:123123";

// Returns the storage name of the device specified by |pnp_device_id|.
// |storage_object_id| specifies the string ID that uniquely identifies the
// object on the device.
string16 GetMTPStorageName(const string16& pnp_device_id,
                           const string16& storage_object_id) {
  if (pnp_device_id == TestPortableDeviceWatcherWin::kMTPDeviceWithInvalidInfo)
    return string16();

  if (storage_object_id == kStorageObjectIdA)
    return kStorageLabelA;
  return (storage_object_id == kStorageObjectIdB) ?
      kStorageLabelB : string16();
}

}  // namespace

// TestPortableDeviceWatcherWin ------------------------------------------------

// static
const char16 TestPortableDeviceWatcherWin::kMTPDeviceWithMultipleStorages[] =
    L"\\?\\usb#vid_ff&pid_18#32&2&1#{ab33-1de4-f22e-1882-9724})";
const char16 TestPortableDeviceWatcherWin::kMTPDeviceWithInvalidInfo[] =
    L"\\?\\usb#vid_00&pid_00#0&2&1#{0000-0000-0000-0000-0000})";
const char16 TestPortableDeviceWatcherWin::kMTPDeviceWithValidInfo[] =
    L"\\?\\usb#vid_ff&pid_000f#32&2&1#{abcd-1234-ffde-1112-9172})";
const char TestPortableDeviceWatcherWin::kStorageUniqueIdA[] =
    "mtp:StorageSerial:SID-{s10001, D, 12378}:123123";

TestPortableDeviceWatcherWin::TestPortableDeviceWatcherWin()
    : use_dummy_mtp_storage_info_(false) {
}

TestPortableDeviceWatcherWin::~TestPortableDeviceWatcherWin() {
}

// static
std::string TestPortableDeviceWatcherWin::GetMTPStorageUniqueId(
    const string16& pnp_device_id,
    const string16& storage_object_id) {
  if (storage_object_id == kStorageObjectIdA)
    return TestPortableDeviceWatcherWin::kStorageUniqueIdA;
  return (storage_object_id == kStorageObjectIdB) ?
      kStorageUniqueIdB : std::string();
}

// static
PortableDeviceWatcherWin::StorageObjectIDs
TestPortableDeviceWatcherWin::GetMTPStorageObjectIds(
    const string16& pnp_device_id) {
  PortableDeviceWatcherWin::StorageObjectIDs storage_object_ids;
  storage_object_ids.push_back(kStorageObjectIdA);
  if (pnp_device_id == kMTPDeviceWithMultipleStorages)
    storage_object_ids.push_back(kStorageObjectIdB);
  return storage_object_ids;
}

// static
void TestPortableDeviceWatcherWin::GetMTPStorageDetails(
    const string16& pnp_device_id,
    const string16& storage_object_id,
    string16* device_location,
    std::string* unique_id,
    string16* name) {
  std::string storage_unique_id = GetMTPStorageUniqueId(pnp_device_id,
                                                        storage_object_id);
  if (device_location)
    *device_location = UTF8ToUTF16("\\\\" + storage_unique_id);

  if (unique_id)
    *unique_id = storage_unique_id;

  if (name)
    *name = GetMTPStorageName(pnp_device_id, storage_object_id);
}

// static
PortableDeviceWatcherWin::StorageObjects
TestPortableDeviceWatcherWin::GetDeviceStorageObjects(
    const string16& pnp_device_id) {
  PortableDeviceWatcherWin::StorageObjects storage_objects;
  PortableDeviceWatcherWin::StorageObjectIDs storage_object_ids =
      GetMTPStorageObjectIds(pnp_device_id);
  for (PortableDeviceWatcherWin::StorageObjectIDs::const_iterator it =
           storage_object_ids.begin();
       it != storage_object_ids.end(); ++it) {
    storage_objects.push_back(DeviceStorageObject(
        *it, GetMTPStorageUniqueId(pnp_device_id, *it)));
  }
  return storage_objects;
}

void TestPortableDeviceWatcherWin::EnumerateAttachedDevices() {
}

void TestPortableDeviceWatcherWin::HandleDeviceAttachEvent(
    const string16& pnp_device_id) {
  DeviceDetails device_details = {
      (pnp_device_id != kMTPDeviceWithInvalidInfo) ?
           kMTPDeviceFriendlyName : string16(),
      pnp_device_id,
      GetDeviceStorageObjects(pnp_device_id)
  };
  OnDidHandleDeviceAttachEvent(&device_details, true);
}

bool TestPortableDeviceWatcherWin::GetMTPStorageInfoFromDeviceId(
    const std::string& storage_device_id,
    string16* device_location,
    string16* storage_object_id) const {
  DCHECK(!storage_device_id.empty());
  if (use_dummy_mtp_storage_info_) {
    if (storage_device_id == TestPortableDeviceWatcherWin::kStorageUniqueIdA) {
      *device_location = kMTPDeviceWithValidInfo;
      *storage_object_id = kStorageObjectIdA;
      return true;
    }
    return false;
  }
  return PortableDeviceWatcherWin::GetMTPStorageInfoFromDeviceId(
      storage_device_id, device_location, storage_object_id);
}

}  // namespace test
}  // namespace chrome
