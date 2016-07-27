// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sync_driver/backup_rollback_controller.h"

#include <string>

#include "base/command_line.h"
#include "base/location.h"
#include "base/metrics/field_trial.h"
#include "base/single_thread_task_runner.h"
#include "base/thread_task_runner_handle.h"
#include "components/sync_driver/signin_manager_wrapper.h"
#include "components/sync_driver/sync_driver_features.h"
#include "components/sync_driver/sync_driver_switches.h"
#include "components/sync_driver/sync_prefs.h"

namespace sync_driver {

#if BUILDFLAG(ENABLE_PRE_SYNC_BACKUP)
// Number of rollback attempts to try before giving up.
static const int kRollbackLimits = 3;

// Finch experiment name and group.
static char kSyncBackupFinchName[] = "SyncBackup";
static char kSyncBackupFinchDisabled[] = "disabled";
#endif

BackupRollbackController::BackupRollbackController(
    sync_driver::SyncPrefs* sync_prefs,
    const SigninManagerWrapper* signin,
    base::Closure start_backup,
    base::Closure start_rollback)
    : sync_prefs_(sync_prefs),
      signin_(signin),
      start_backup_(start_backup),
      start_rollback_(start_rollback) {}

BackupRollbackController::~BackupRollbackController() {}

bool BackupRollbackController::StartBackup() {
  if (!IsBackupEnabled())
    return false;

  // Disable rollback to previous backup DB because it will be overwritten by
  // new backup.
  sync_prefs_->SetRemainingRollbackTries(0);
  base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE, start_backup_);
  return true;
}

bool BackupRollbackController::StartRollback() {
  if (!IsBackupEnabled())
    return false;

  // Don't roll back if disabled or user is signed in.
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kSyncDisableRollback) ||
      !signin_->GetEffectiveUsername().empty()) {
    sync_prefs_->SetRemainingRollbackTries(0);
    return false;
  }

  int rollback_tries = sync_prefs_->GetRemainingRollbackTries();
  if (rollback_tries <= 0)
    return false;  // No pending rollback.

  sync_prefs_->SetRemainingRollbackTries(rollback_tries - 1);
  base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE, start_rollback_);
  return true;
}

void BackupRollbackController::OnRollbackReceived() {
#if BUILDFLAG(ENABLE_PRE_SYNC_BACKUP)
  sync_prefs_->SetRemainingRollbackTries(kRollbackLimits);
#endif
}

void BackupRollbackController::OnRollbackDone() {
#if BUILDFLAG(ENABLE_PRE_SYNC_BACKUP)
  sync_prefs_->SetRemainingRollbackTries(0);
#endif
}

// static
bool BackupRollbackController::IsBackupEnabled() {
#if BUILDFLAG(ENABLE_PRE_SYNC_BACKUP)
  const std::string group_name =
      base::FieldTrialList::FindFullName(kSyncBackupFinchName);

  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kSyncDisableBackup) ||
      group_name == kSyncBackupFinchDisabled) {
    return false;
  }
  return true;
#else
  return false;
#endif
}

}  // namespace sync_driver
