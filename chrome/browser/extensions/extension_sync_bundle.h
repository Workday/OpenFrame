// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_SYNC_BUNDLE_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_SYNC_BUNDLE_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/extensions/extension_sync_data.h"
#include "sync/api/syncable_service.h"

class ExtensionService;
class ExtensionSet;

namespace syncer {
class SyncChangeProcessor;
class SyncErrorFactory;
}

namespace extensions {

class Extension;

// Bundle of extension specific sync stuff.
class ExtensionSyncBundle {
 public:
  explicit ExtensionSyncBundle(ExtensionService* extension_service);
  ~ExtensionSyncBundle();

  // Setup this bundle to be sync extension data.
  void SetupSync(syncer::SyncChangeProcessor* sync_processor,
                 syncer::SyncErrorFactory* sync_error_factory,
                 const syncer::SyncDataList& initial_sync_data);

  // Resets this class back to it default values, which will disable all syncing
  // until a new sync processor is set.
  void Reset();

  // Returns a syncer::SyncChange that will delete the given extension.
  syncer::SyncChange CreateSyncChangeToDelete(const Extension* extension) const;

  // Process the sync deletion of the given extension.
  void ProcessDeletion(
      std::string extension_id, const syncer::SyncChange& sync_change);

  // Create a sync change based on |sync_data|.
  syncer::SyncChange CreateSyncChange(const syncer::SyncData& sync_data);

  // Get all the sync data contained in this bundle.
  syncer::SyncDataList GetAllSyncData() const;

  // Process the given sync change and apply it.
  void ProcessSyncChange(ExtensionSyncData extension_sync_data);

  // Sync a newly-installed extension or change an existing one.
  void SyncChangeIfNeeded(const Extension& extension);

  // Process the list of sync changes.
  void ProcessSyncChangeList(syncer::SyncChangeList sync_change_list);

  // Check to see if the given |id| is either synced or pending to be synced.
  bool HasExtensionId(const std::string& id) const;
  bool HasPendingExtensionId(const std::string& id) const;

  // Add a pending extension to be synced.
  void AddPendingExtension(const std::string& id,
                           const ExtensionSyncData& extension_sync_data);

  // Returns a vector of all the pending sync data.
  std::vector<ExtensionSyncData> GetPendingData() const;

  // Appends sync data objects for every extension in |extensions|.
  void GetExtensionSyncDataListHelper(
      const ExtensionSet& extensions,
      std::vector<extensions::ExtensionSyncData>* sync_data_list) const;

  // Returns true if SetupSync has been called, false otherwise.
  bool IsSyncing() const;

 private:
  // Add a synced extension.
  void AddExtension(const std::string& id);

  // Remove a synced extension.
  void RemoveExtension(const std::string& id);

  // Change an extension from being pending to synced.
  void MarkPendingExtensionSynced(const std::string& id);

  ExtensionService* extension_service_;  // Owns us.
  scoped_ptr<syncer::SyncChangeProcessor> sync_processor_;
  scoped_ptr<syncer::SyncErrorFactory> sync_error_factory_;

  std::set<std::string> synced_extensions_;
  std::map<std::string, ExtensionSyncData> pending_sync_data_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionSyncBundle);
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_SYNC_BUNDLE_H_
