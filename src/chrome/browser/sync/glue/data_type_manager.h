// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_GLUE_DATA_TYPE_MANAGER_H__
#define CHROME_BROWSER_SYNC_GLUE_DATA_TYPE_MANAGER_H__

#include <list>
#include <set>
#include <string>

#include "chrome/browser/sync/glue/data_type_controller.h"
#include "sync/api/sync_error.h"
#include "sync/internal_api/public/base/model_type.h"
#include "sync/internal_api/public/configure_reason.h"

namespace browser_sync {

// This interface is for managing the start up and shut down life cycle
// of many different syncable data types.
class DataTypeManager {
 public:
  enum State {
    STOPPED,           // No data types are currently running.
    DOWNLOAD_PENDING,  // Not implemented yet: Waiting for the syncer to
                       // complete the initial download of new data
                       // types.

    CONFIGURING,       // Data types are being started.
    RETRYING,          // Retrying a pending reconfiguration.

    CONFIGURED,        // All enabled data types are running.
    STOPPING           // Data types are being stopped.
  };

  // Update NotifyDone() in data_type_manager_impl.cc if you update
  // this.
  enum ConfigureStatus {
    UNKNOWN = -1,
    OK,                  // Configuration finished without error.
    PARTIAL_SUCCESS,     // Some data types had an error while starting up.
    ABORTED,             // Start was aborted by calling Stop() before
                         // all types were started.
    UNRECOVERABLE_ERROR  // We got an unrecoverable error during startup.
  };

  // Note: |errors| is only filled when status is not OK.
  struct ConfigureResult {
    ConfigureResult();
    ConfigureResult(ConfigureStatus status,
                    syncer::ModelTypeSet requested_types);
    ConfigureResult(ConfigureStatus status,
                    syncer::ModelTypeSet requested_types,
                    std::map<syncer::ModelType, syncer::SyncError>
                        failed_data_types,
                    syncer::ModelTypeSet waiting_to_start,
                    syncer::ModelTypeSet needs_crypto);
    ~ConfigureResult();
    ConfigureStatus status;
    syncer::ModelTypeSet requested_types;

    // These types encountered a failure in association.
    std::map<syncer::ModelType, syncer::SyncError> failed_data_types;

    // List of types that failed to start association with in our alloted
    // time period(see kDataTypeLoadWaitTimeInSeconds). We move
    // forward here and allow these types to continue loading in the
    // background. When these types are loaded DataTypeManager will
    // be informed and another configured cycle will be started.
    syncer::ModelTypeSet waiting_to_start;

    // Those types that are unable to start due to the cryptographer not being
    // ready.
    syncer::ModelTypeSet needs_crypto;
  };

  virtual ~DataTypeManager() {}

  // Convert a ConfigureStatus to string for debug purposes.
  static std::string ConfigureStatusToString(ConfigureStatus status);

  // Begins asynchronous configuration of data types.  Any currently
  // running data types that are not in the desired_types set will be
  // stopped.  Any stopped data types that are in the desired_types
  // set will be started.  All other data types are left in their
  // current state.  A SYNC_CONFIGURE_START notification will be sent
  // to the UI thread when configuration is started and a
  // SYNC_CONFIGURE_DONE notification will be sent (with a
  // ConfigureResult detail) when configuration is complete.
  //
  // Note that you may call Configure() while configuration is in
  // progress.  Configuration will be complete only when the
  // desired_types supplied in the last call to Configure is achieved.
  virtual void Configure(syncer::ModelTypeSet desired_types,
                         syncer::ConfigureReason reason) = 0;

  virtual void PurgeForMigration(syncer::ModelTypeSet undesired_types,
                                 syncer::ConfigureReason reason) = 0;

  // Synchronously stops all registered data types.  If called after
  // Configure() is called but before it finishes, it will abort the
  // configure and any data types that have been started will be
  // stopped.
  virtual void Stop() = 0;

  // The current state of the data type manager.
  virtual State state() const = 0;
};

}  // namespace browser_sync

#endif  // CHROME_BROWSER_SYNC_GLUE_DATA_TYPE_MANAGER_H__
