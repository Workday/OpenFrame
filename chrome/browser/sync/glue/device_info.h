// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_GLUE_DEVICE_INFO_H_
#define CHROME_BROWSER_SYNC_GLUE_DEVICE_INFO_H_

#include <string>

#include "base/basictypes.h"
#include "base/bind.h"
#include "sync/protocol/sync.pb.h"

namespace base {
class DictionaryValue;
}

namespace chrome {
class VersionInfo;
}

namespace browser_sync {

// A class that holds information regarding the properties of a device.
class DeviceInfo {
 public:
  DeviceInfo(const std::string& guid,
             const std::string& client_name,
             const std::string& chrome_version,
             const std::string& sync_user_agent,
             const sync_pb::SyncEnums::DeviceType device_type);
  ~DeviceInfo();

  // Sync specific unique identifier for the device. Note if a device
  // is wiped and sync is set up again this id WILL be different.
  // The same device might have more than 1 guid if the device has multiple
  // accounts syncing.
  const std::string& guid() const;

  // The host name for the client.
  const std::string& client_name() const;

  // Chrome version string.
  const std::string& chrome_version() const;

  // The user agent is the combination of OS type, chrome version and which
  // channel of chrome(stable or beta). For more information see
  // |DeviceInfo::MakeUserAgentForSyncApi|.
  const std::string& sync_user_agent() const;

  // Third party visible id for the device. See |public_id_| for more details.
  const std::string& public_id() const;

  // Device Type.
  sync_pb::SyncEnums::DeviceType device_type() const;

  // Compares this object's fields with another's.
  bool Equals(const DeviceInfo& other) const;

  // Apps can set ids for a device that is meaningful to them but
  // not unique enough so the user can be tracked. Exposing |guid|
  // would lead to a stable unique id for a device which can potentially
  // be used for tracking.
  void set_public_id(std::string id);

  // Converts the |DeviceInfo| values to a JS friendly DictionaryValue,
  // which extension APIs can expose to third party apps.
  base::DictionaryValue* ToValue();

  static sync_pb::SyncEnums::DeviceType GetLocalDeviceType();

  // Creates a |DeviceInfo| object representing the local device and passes
  // it as parameter to the callback.
  static void CreateLocalDeviceInfo(
      const std::string& guid,
      base::Callback<void(const DeviceInfo& local_info)> callback);

  // Gets the local device name and passes it as a parameter to callback.
  static void GetClientName(
      base::Callback<void(const std::string& local_info)> callback);

  // Helper to construct a user agent string (ASCII) suitable for use by
  // the syncapi for any HTTP communication. This string is used by the sync
  // backend for classifying client types when calculating statistics.
  static std::string MakeUserAgentForSyncApi(
      const chrome::VersionInfo& version_info);

 private:
  static void GetClientNameContinuation(
      base::Callback<void(const std::string& local_info)> callback,
      const std::string& session_name);

  static void CreateLocalDeviceInfoContinuation(
      const std::string& guid,
      base::Callback<void(const DeviceInfo& local_info)> callback,
      const std::string& session_name);

  const std::string guid_;

  const std::string client_name_;

  const std::string chrome_version_;

  const std::string sync_user_agent_;

  const sync_pb::SyncEnums::DeviceType device_type_;

  // Exposing |guid| would lead to a stable unique id for a device which
  // can potentially be used for tracking. Public ids are privacy safe
  // ids in that the same device will have different id for different apps
  // and they are also reset when app/extension is uninstalled.
  std::string public_id_;

  DISALLOW_COPY_AND_ASSIGN(DeviceInfo);
};

}  // namespace browser_sync

#endif  // CHROME_BROWSER_SYNC_GLUE_DEVICE_INFO_H_
