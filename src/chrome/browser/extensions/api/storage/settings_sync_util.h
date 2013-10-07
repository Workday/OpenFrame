// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_STORAGE_SETTINGS_SYNC_UTIL_H_
#define CHROME_BROWSER_EXTENSIONS_API_STORAGE_SETTINGS_SYNC_UTIL_H_


#include "sync/api/sync_change.h"
#include "sync/api/sync_data.h"

namespace base {
class Value;
}  // namespace base

namespace extensions {

namespace settings_sync_util {

// Creates a syncer::SyncData object for an extension or app setting.
syncer::SyncData CreateData(
    const std::string& extension_id,
    const std::string& key,
    const base::Value& value,
    syncer::ModelType type);

// Creates an "add" sync change for an extension or app setting.
syncer::SyncChange CreateAdd(
    const std::string& extension_id,
    const std::string& key,
    const base::Value& value,
    syncer::ModelType type);

// Creates an "update" sync change for an extension or app setting.
syncer::SyncChange CreateUpdate(
    const std::string& extension_id,
    const std::string& key,
    const base::Value& value,
    syncer::ModelType type);

// Creates a "delete" sync change for an extension or app setting.
syncer::SyncChange CreateDelete(
    const std::string& extension_id,
    const std::string& key,
    syncer::ModelType type);

}  // namespace settings_sync_util

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_STORAGE_SETTINGS_SYNC_UTIL_H_
