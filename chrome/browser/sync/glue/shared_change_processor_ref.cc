// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/glue/shared_change_processor_ref.h"

namespace browser_sync {

SharedChangeProcessorRef::SharedChangeProcessorRef(
    const scoped_refptr<SharedChangeProcessor>& change_processor)
    : change_processor_(change_processor) {
  DCHECK(change_processor_.get());
}

SharedChangeProcessorRef::~SharedChangeProcessorRef() {}

syncer::SyncError SharedChangeProcessorRef::ProcessSyncChanges(
    const tracked_objects::Location& from_here,
    const syncer::SyncChangeList& change_list) {
  return change_processor_->ProcessSyncChanges(from_here, change_list);
}

syncer::SyncError SharedChangeProcessorRef::CreateAndUploadError(
    const tracked_objects::Location& from_here,
    const std::string& message) {
  return change_processor_->CreateAndUploadError(from_here, message);
}

}  // namespace browser_sync
