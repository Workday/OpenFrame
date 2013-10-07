// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_STORAGE_SYNCABLE_SETTINGS_STORAGE_H_
#define CHROME_BROWSER_EXTENSIONS_API_STORAGE_SYNCABLE_SETTINGS_STORAGE_H_

#include "base/compiler_specific.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/observer_list_threadsafe.h"
#include "base/values.h"
#include "chrome/browser/extensions/api/storage/setting_sync_data.h"
#include "chrome/browser/extensions/api/storage/settings_observer.h"
#include "chrome/browser/value_store/value_store.h"
#include "sync/api/sync_change.h"
#include "sync/api/syncable_service.h"

namespace extensions {

class SettingsSyncProcessor;

// Decorates a ValueStore with sync behaviour.
class SyncableSettingsStorage : public ValueStore {
 public:
  SyncableSettingsStorage(
      const scoped_refptr<SettingsObserverList>& observers,
      const std::string& extension_id,
      // Ownership taken.
      ValueStore* delegate);

  virtual ~SyncableSettingsStorage();

  // ValueStore implementation.
  virtual size_t GetBytesInUse(const std::string& key) OVERRIDE;
  virtual size_t GetBytesInUse(const std::vector<std::string>& keys) OVERRIDE;
  virtual size_t GetBytesInUse() OVERRIDE;
  virtual ReadResult Get(const std::string& key) OVERRIDE;
  virtual ReadResult Get(const std::vector<std::string>& keys) OVERRIDE;
  virtual ReadResult Get() OVERRIDE;
  virtual WriteResult Set(
      WriteOptions options,
      const std::string& key,
      const Value& value) OVERRIDE;
  virtual WriteResult Set(
      WriteOptions options, const base::DictionaryValue& values) OVERRIDE;
  virtual WriteResult Remove(const std::string& key) OVERRIDE;
  virtual WriteResult Remove(const std::vector<std::string>& keys) OVERRIDE;
  virtual WriteResult Clear() OVERRIDE;

  // Sync-related methods, analogous to those on SyncableService (handled by
  // ExtensionSettings), but with looser guarantees about when the methods
  // can be called.

  // Must only be called if sync isn't already active.
  syncer::SyncError StartSyncing(
      const base::DictionaryValue& sync_state,
      scoped_ptr<SettingsSyncProcessor> sync_processor);

  // May be called at any time (idempotent).
  void StopSyncing();

  // May be called at any time; changes will be ignored if sync isn't active.
  syncer::SyncError ProcessSyncChanges(const SettingSyncDataList& sync_changes);

 private:
  // Sends the changes from |result| to sync if it's enabled.
  void SyncResultIfEnabled(const ValueStore::WriteResult& result);

  // Sends all local settings to sync (synced settings assumed to be empty).
  syncer::SyncError SendLocalSettingsToSync(
      const base::DictionaryValue& settings);

  // Overwrites local state with sync state.
  syncer::SyncError OverwriteLocalSettingsWithSync(
      const base::DictionaryValue& sync_state,
      const base::DictionaryValue& settings);

  // Called when an Add/Update/Remove comes from sync.  Ownership of Value*s
  // are taken.
  syncer::SyncError OnSyncAdd(
      const std::string& key,
      Value* new_value,
      ValueStoreChangeList* changes);
  syncer::SyncError OnSyncUpdate(
      const std::string& key,
      Value* old_value,
      Value* new_value,
      ValueStoreChangeList* changes);
  syncer::SyncError OnSyncDelete(
      const std::string& key,
      Value* old_value,
      ValueStoreChangeList* changes);

  // List of observers to settings changes.
  const scoped_refptr<SettingsObserverList> observers_;

  // Id of the extension these settings are for.
  std::string const extension_id_;

  // Storage area to sync.
  const scoped_ptr<ValueStore> delegate_;

  // Object which sends changes to sync.
  scoped_ptr<SettingsSyncProcessor> sync_processor_;

  DISALLOW_COPY_AND_ASSIGN(SyncableSettingsStorage);
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_STORAGE_SYNCABLE_SETTINGS_STORAGE_H_
