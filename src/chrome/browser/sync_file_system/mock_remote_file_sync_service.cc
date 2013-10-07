// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync_file_system/mock_remote_file_sync_service.h"

#include <string>

#include "base/bind.h"
#include "base/location.h"
#include "base/message_loop/message_loop_proxy.h"
#include "url/gurl.h"
#include "webkit/browser/fileapi/file_system_url.h"

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

namespace sync_file_system {

MockRemoteFileSyncService::MockRemoteFileSyncService()
    : conflict_resolution_policy_(CONFLICT_RESOLUTION_POLICY_MANUAL) {
  typedef MockRemoteFileSyncService self;
  ON_CALL(*this, AddServiceObserver(_))
      .WillByDefault(Invoke(this, &self::AddServiceObserverStub));
  ON_CALL(*this, AddFileStatusObserver(_))
      .WillByDefault(Invoke(this, &self::AddFileStatusObserverStub));
  ON_CALL(*this, RegisterOriginForTrackingChanges(_, _))
      .WillByDefault(Invoke(this, &self::RegisterOriginForTrackingChangesStub));
  ON_CALL(*this, UnregisterOriginForTrackingChanges(_, _))
      .WillByDefault(
          Invoke(this, &self::UnregisterOriginForTrackingChangesStub));
  ON_CALL(*this, UninstallOrigin(_, _))
      .WillByDefault(
          Invoke(this, &self::DeleteOriginDirectoryStub));
  ON_CALL(*this, ProcessRemoteChange(_))
      .WillByDefault(Invoke(this, &self::ProcessRemoteChangeStub));
  ON_CALL(*this, GetLocalChangeProcessor())
      .WillByDefault(Return(&mock_local_change_processor_));
  ON_CALL(*this, IsConflicting(_))
      .WillByDefault(Return(false));
  ON_CALL(*this, GetCurrentState())
      .WillByDefault(Return(REMOTE_SERVICE_OK));
  ON_CALL(*this, SetConflictResolutionPolicy(_))
      .WillByDefault(Invoke(this, &self::SetConflictResolutionPolicyStub));
  ON_CALL(*this, GetConflictResolutionPolicy())
      .WillByDefault(Invoke(this, &self::GetConflictResolutionPolicyStub));
}

MockRemoteFileSyncService::~MockRemoteFileSyncService() {
}

scoped_ptr<base::ListValue> MockRemoteFileSyncService::DumpFiles(
    const GURL& origin) {
  return scoped_ptr<base::ListValue>();
}

void MockRemoteFileSyncService::NotifyRemoteChangeQueueUpdated(
    int64 pending_changes) {
  FOR_EACH_OBSERVER(Observer, service_observers_,
                    OnRemoteChangeQueueUpdated(pending_changes));
}

void MockRemoteFileSyncService::NotifyRemoteServiceStateUpdated(
    RemoteServiceState state,
    const std::string& description) {
  FOR_EACH_OBSERVER(Observer, service_observers_,
                    OnRemoteServiceStateUpdated(state, description));
}

void MockRemoteFileSyncService::NotifyFileStatusChanged(
    const fileapi::FileSystemURL& url,
    SyncFileStatus sync_status,
    SyncAction action_taken,
    SyncDirection direction) {
  FOR_EACH_OBSERVER(FileStatusObserver, file_status_observers_,
                    OnFileStatusChanged(url, sync_status,
                                        action_taken, direction));
}

void MockRemoteFileSyncService::AddServiceObserverStub(Observer* observer) {
  service_observers_.AddObserver(observer);
}

void MockRemoteFileSyncService::AddFileStatusObserverStub(
    FileStatusObserver* observer) {
  file_status_observers_.AddObserver(observer);
}

void MockRemoteFileSyncService::RegisterOriginForTrackingChangesStub(
    const GURL& origin,
    const SyncStatusCallback& callback) {
  base::MessageLoopProxy::current()->PostTask(
      FROM_HERE,
      base::Bind(callback, SYNC_STATUS_OK));
}

void MockRemoteFileSyncService::UnregisterOriginForTrackingChangesStub(
    const GURL& origin,
    const SyncStatusCallback& callback) {
  base::MessageLoopProxy::current()->PostTask(
      FROM_HERE,
      base::Bind(callback, SYNC_STATUS_OK));
}

void MockRemoteFileSyncService::DeleteOriginDirectoryStub(
    const GURL& origin,
    const SyncStatusCallback& callback) {
  base::MessageLoopProxy::current()->PostTask(
      FROM_HERE,
      base::Bind(callback, SYNC_STATUS_OK));
}

void MockRemoteFileSyncService::ProcessRemoteChangeStub(
    const SyncFileCallback& callback) {
  base::MessageLoopProxy::current()->PostTask(
      FROM_HERE,
      base::Bind(callback, SYNC_STATUS_NO_CHANGE_TO_SYNC,
                 fileapi::FileSystemURL()));
}

SyncStatusCode MockRemoteFileSyncService::SetConflictResolutionPolicyStub(
    ConflictResolutionPolicy policy) {
  conflict_resolution_policy_ = policy;
  return SYNC_STATUS_OK;
}

ConflictResolutionPolicy
MockRemoteFileSyncService::GetConflictResolutionPolicyStub() const {
  return conflict_resolution_policy_;
}

}  // namespace sync_file_system
