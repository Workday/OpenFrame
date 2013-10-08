// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// A fake implementation of the SyncScheduler. If needed, we should add default
// logic needed for tests (invoking callbacks, etc) here rather than in higher
// level test classes.

#ifndef SYNC_TEST_ENGINE_FAKE_SYNC_SCHEDULER_H_
#define SYNC_TEST_ENGINE_FAKE_SYNC_SCHEDULER_H_

#include "base/message_loop/message_loop.h"
#include "sync/engine/sync_scheduler.h"

namespace syncer {

class FakeSyncScheduler : public SyncScheduler {
 public:
  FakeSyncScheduler();
  virtual ~FakeSyncScheduler();

  virtual void Start(Mode mode) OVERRIDE;
  virtual void RequestStop() OVERRIDE;
  virtual void ScheduleLocalNudge(
      const base::TimeDelta& desired_delay,
      ModelTypeSet types,
      const tracked_objects::Location& nudge_location) OVERRIDE;
  virtual void ScheduleLocalRefreshRequest(
      const base::TimeDelta& desired_delay,
      ModelTypeSet types,
      const tracked_objects::Location& nudge_location) OVERRIDE;
  virtual void ScheduleInvalidationNudge(
      const base::TimeDelta& desired_delay,
      const ModelTypeInvalidationMap& invalidation_map,
      const tracked_objects::Location& nudge_location) OVERRIDE;
  virtual bool ScheduleConfiguration(
      const ConfigurationParams& params) OVERRIDE;
  virtual void SetNotificationsEnabled(bool notifications_enabled) OVERRIDE;

  virtual base::TimeDelta GetSessionsCommitDelay() const OVERRIDE;
  virtual void OnCredentialsUpdated() OVERRIDE;
  virtual void OnConnectionStatusChange() OVERRIDE;

  // SyncSession::Delegate implementation.
  virtual void OnThrottled(
      const base::TimeDelta& throttle_duration) OVERRIDE;
  virtual void OnTypesThrottled(
      ModelTypeSet types,
      const base::TimeDelta& throttle_duration) OVERRIDE;
  virtual bool IsCurrentlyThrottled() OVERRIDE;
  virtual void OnReceivedShortPollIntervalUpdate(
      const base::TimeDelta& new_interval) OVERRIDE;
  virtual void OnReceivedLongPollIntervalUpdate(
      const base::TimeDelta& new_interval) OVERRIDE;
  virtual void OnReceivedSessionsCommitDelay(
      const base::TimeDelta& new_delay) OVERRIDE;
  virtual void OnReceivedClientInvalidationHintBufferSize(int size) OVERRIDE;
  virtual void OnShouldStopSyncingPermanently() OVERRIDE;
  virtual void OnSyncProtocolError(
      const sessions::SyncSessionSnapshot& snapshot) OVERRIDE;
};

}  // namespace syncer

#endif  // SYNC_TEST_ENGINE_FAKE_SYNC_SCHEDULER_H_
