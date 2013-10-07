// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sync/internal_api/public/sync_manager_factory.h"

#include "sync/internal_api/sync_manager_impl.h"

namespace syncer {

SyncManagerFactory::SyncManagerFactory() {
}

SyncManagerFactory::~SyncManagerFactory() {
}

scoped_ptr<SyncManager> SyncManagerFactory::CreateSyncManager(
      const std::string name) {
  return scoped_ptr<SyncManager>(new SyncManagerImpl(name));
}

}  // namespace syncer
