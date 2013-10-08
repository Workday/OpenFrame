// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/glue/data_type_manager.h"

namespace browser_sync {

DataTypeManager::ConfigureResult::ConfigureResult()
    : status(UNKNOWN) {
}

DataTypeManager::ConfigureResult::ConfigureResult(ConfigureStatus status,
                                                  syncer::ModelTypeSet
                                                      requested_types)
    : status(status),
      requested_types(requested_types) {
  DCHECK_EQ(OK, status);
}

DataTypeManager::ConfigureResult::ConfigureResult(
    ConfigureStatus status,
    syncer::ModelTypeSet requested_types,
    std::map<syncer::ModelType, syncer::SyncError> failed_data_types,
    syncer::ModelTypeSet waiting_to_start,
    syncer::ModelTypeSet needs_crypto)
    : status(status),
      requested_types(requested_types),
      failed_data_types(failed_data_types),
      waiting_to_start(waiting_to_start),
      needs_crypto(needs_crypto) {
  if (!failed_data_types.empty() || !needs_crypto.Empty()) {
    DCHECK_NE(OK, status);
  }
}

DataTypeManager::ConfigureResult::~ConfigureResult() {
}

// Static.
std::string DataTypeManager::ConfigureStatusToString(ConfigureStatus status) {
  switch (status) {
    case OK:
      return "Ok";
    case ABORTED:
      return "Aborted";
    case UNRECOVERABLE_ERROR:
      return "Unrecoverable Error";
    case PARTIAL_SUCCESS:
      return "Partial Success";
    default:
      NOTREACHED();
      return std::string();
  }
}

}  // namespace browser_sync
