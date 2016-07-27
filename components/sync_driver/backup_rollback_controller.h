// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SYNC_DRIVER_BACKUP_ROLLBACK_CONTROLLER_H_
#define COMPONENTS_SYNC_DRIVER_BACKUP_ROLLBACK_CONTROLLER_H_

#include "base/callback.h"
#include "base/memory/weak_ptr.h"

class SigninManagerWrapper;

namespace sync_driver {

class SyncPrefs;

// BackupRollbackController takes two closures for starting backup/rollback
// process. It calls the closures according to user's signin status or
// received rollback command. Backup is not run when user signed in, even when
// sync is not running.
class BackupRollbackController {
 public:
  BackupRollbackController(sync_driver::SyncPrefs* sync_prefs,
                           const SigninManagerWrapper* signin,
                           base::Closure start_backup,
                           base::Closure start_rollback);
  ~BackupRollbackController();

  // Post task to run |start_backup_| if conditions are met. Return true if
  // task is posted, false otherwise.
  bool StartBackup();

  // Post task to run |start_rollback_| if conditions are met. Return true if
  // task is posted, false otherwise.
  bool StartRollback();

  // Update rollback preference to indicate rollback is needed.
  void OnRollbackReceived();

  // Update rollback preference to indicate rollback is finished.
  void OnRollbackDone();

  // Return true if platform supports backup and backup is enabled.
  static bool IsBackupEnabled();

 private:
  sync_driver::SyncPrefs* sync_prefs_;

  // Use SigninManagerWrapper instead of SigninManagerBase so that
  // supervised users are treated like regular signed-in users.
  const SigninManagerWrapper* signin_;

  base::Closure start_backup_;
  base::Closure start_rollback_;

  DISALLOW_COPY_AND_ASSIGN(BackupRollbackController);
};

}  // namespace sync_driver

#endif  // COMPONENTS_SYNC_DRIVER_BACKUP_ROLLBACK_CONTROLLER_H_
