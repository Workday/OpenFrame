// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sync/test/engine/fake_sync_scheduler.h"

namespace syncer {

FakeSyncScheduler::FakeSyncScheduler() {}

FakeSyncScheduler::~FakeSyncScheduler() {}

void FakeSyncScheduler::Start(Mode mode) {
}

void FakeSyncScheduler::RequestStop() {
}

void FakeSyncScheduler::ScheduleLocalNudge(
    const base::TimeDelta& desired_delay,
    ModelTypeSet types,
    const tracked_objects::Location& nudge_location) {
}

void FakeSyncScheduler::ScheduleLocalRefreshRequest(
    const base::TimeDelta& desired_delay,
    ModelTypeSet types,
    const tracked_objects::Location& nudge_location) {
}

void FakeSyncScheduler::ScheduleInvalidationNudge(
    const base::TimeDelta& desired_delay,
    const ModelTypeInvalidationMap& invalidation_map,
    const tracked_objects::Location& nudge_location) {
}

bool FakeSyncScheduler::ScheduleConfiguration(
     const ConfigurationParams& params) {
  params.ready_task.Run();
  return true;
}

void FakeSyncScheduler::SetNotificationsEnabled(bool notifications_enabled) {
}

base::TimeDelta FakeSyncScheduler::GetSessionsCommitDelay() const {
  return base::TimeDelta();
}

void FakeSyncScheduler::OnCredentialsUpdated() {

}

void FakeSyncScheduler::OnConnectionStatusChange() {

}

void FakeSyncScheduler::OnThrottled(
    const base::TimeDelta& throttle_duration) {
}

void FakeSyncScheduler::OnTypesThrottled(
    ModelTypeSet types,
    const base::TimeDelta& throttle_duration) {
}

bool FakeSyncScheduler::IsCurrentlyThrottled() {
  return false;
}

void FakeSyncScheduler::OnReceivedShortPollIntervalUpdate(
     const base::TimeDelta& new_interval) {
}

void FakeSyncScheduler::OnReceivedLongPollIntervalUpdate(
     const base::TimeDelta& new_interval) {
}

void FakeSyncScheduler::OnReceivedSessionsCommitDelay(
     const base::TimeDelta& new_delay) {
}

void FakeSyncScheduler::OnReceivedClientInvalidationHintBufferSize(int size) {
}

void FakeSyncScheduler::OnShouldStopSyncingPermanently() {
}

void FakeSyncScheduler::OnSyncProtocolError(
     const sessions::SyncSessionSnapshot& snapshot) {
}

}  // namespace syncer
