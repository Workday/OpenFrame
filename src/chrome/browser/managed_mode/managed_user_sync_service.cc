// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/managed_mode/managed_user_sync_service.h"

#include "base/bind.h"
#include "base/values.h"
#include "chrome/browser/prefs/scoped_user_pref_update.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "components/user_prefs/pref_registry_syncable.h"
#include "sync/api/sync_change.h"
#include "sync/api/sync_data.h"
#include "sync/api/sync_error.h"
#include "sync/api/sync_error_factory.h"
#include "sync/api/sync_merge_result.h"
#include "sync/protocol/sync.pb.h"

using base::DictionaryValue;
using user_prefs::PrefRegistrySyncable;
using syncer::MANAGED_USERS;
using syncer::ModelType;
using syncer::SyncChange;
using syncer::SyncChangeList;
using syncer::SyncChangeProcessor;
using syncer::SyncData;
using syncer::SyncDataList;
using syncer::SyncError;
using syncer::SyncErrorFactory;
using syncer::SyncMergeResult;
using sync_pb::ManagedUserSpecifics;

namespace {

const char kAcknowledged[] = "acknowledged";
const char kName[] = "name";
const char kMasterKey[] = "masterKey";

SyncData CreateLocalSyncData(const std::string& id,
                             const std::string& name,
                             bool acknowledged,
                             const std::string& master_key) {
  ::sync_pb::EntitySpecifics specifics;
  specifics.mutable_managed_user()->set_id(id);
  specifics.mutable_managed_user()->set_name(name);
  if (!master_key.empty())
    specifics.mutable_managed_user()->set_master_key(master_key);
  if (acknowledged)
    specifics.mutable_managed_user()->set_acknowledged(true);
  return SyncData::CreateLocalData(id, name, specifics);
}

}  // namespace

ManagedUserSyncService::ManagedUserSyncService(PrefService* prefs)
    : prefs_(prefs) {
  pref_change_registrar_.Init(prefs_);
  pref_change_registrar_.Add(
      prefs::kGoogleServicesLastUsername,
      base::Bind(&ManagedUserSyncService::OnLastSignedInUsernameChange,
                 base::Unretained(this)));
}

ManagedUserSyncService::~ManagedUserSyncService() {
}

// static
void ManagedUserSyncService::RegisterProfilePrefs(
    PrefRegistrySyncable* registry) {
  registry->RegisterDictionaryPref(prefs::kManagedUsers,
                                   PrefRegistrySyncable::UNSYNCABLE_PREF);
}

void ManagedUserSyncService::AddObserver(
    ManagedUserSyncServiceObserver* observer) {
  observers_.AddObserver(observer);
}

void ManagedUserSyncService::RemoveObserver(
    ManagedUserSyncServiceObserver* observer) {
  observers_.RemoveObserver(observer);
}

void ManagedUserSyncService::AddManagedUser(const std::string& id,
                                            const std::string& name,
                                            const std::string& master_key) {
  DictionaryPrefUpdate update(prefs_, prefs::kManagedUsers);
  DictionaryValue* dict = update.Get();
  DictionaryValue* value = new DictionaryValue;
  value->SetString(kName, name);
  value->SetString(kMasterKey, master_key);
  DCHECK(!dict->HasKey(id));
  dict->SetWithoutPathExpansion(id, value);

  if (!sync_processor_)
    return;

  // If we're already syncing, create a new change and upload it.
  SyncChangeList change_list;
  change_list.push_back(SyncChange(
      FROM_HERE,
      SyncChange::ACTION_ADD,
      CreateLocalSyncData(id, name, false, master_key)));
  SyncError error =
      sync_processor_->ProcessSyncChanges(FROM_HERE, change_list);
  DCHECK(!error.IsSet()) << error.ToString();
}

void ManagedUserSyncService::DeleteManagedUser(const std::string& id) {
  if (!sync_processor_)
    return;

  SyncChangeList change_list;
  change_list.push_back(SyncChange(
      FROM_HERE,
      SyncChange::ACTION_DELETE,
      SyncData::CreateLocalDelete(id, MANAGED_USERS)));
  SyncError sync_error =
      sync_processor_->ProcessSyncChanges(FROM_HERE, change_list);
  DCHECK(!sync_error.IsSet());
}

void ManagedUserSyncService::Shutdown() {
  NotifyManagedUsersSyncingStopped();
}

SyncMergeResult ManagedUserSyncService::MergeDataAndStartSyncing(
    ModelType type,
    const SyncDataList& initial_sync_data,
    scoped_ptr<SyncChangeProcessor> sync_processor,
    scoped_ptr<SyncErrorFactory> error_handler) {
  DCHECK_EQ(MANAGED_USERS, type);
  sync_processor_ = sync_processor.Pass();
  error_handler_ = error_handler.Pass();

  SyncChangeList change_list;
  SyncMergeResult result(MANAGED_USERS);

  DictionaryPrefUpdate update(prefs_, prefs::kManagedUsers);
  DictionaryValue* dict = update.Get();
  result.set_num_items_before_association(dict->size());
  std::set<std::string> seen_ids;
  int num_items_added = 0;
  int num_items_modified = 0;
  for (SyncDataList::const_iterator it = initial_sync_data.begin();
       it != initial_sync_data.end(); ++it) {
    DCHECK_EQ(MANAGED_USERS, it->GetDataType());
    const ManagedUserSpecifics& managed_user =
        it->GetSpecifics().managed_user();
    DictionaryValue* value = new DictionaryValue();
    value->SetString(kName, managed_user.name());
    DCHECK(managed_user.acknowledged());
    value->SetBoolean(kAcknowledged, managed_user.acknowledged());
    value->SetString(kMasterKey, managed_user.master_key());
    if (dict->HasKey(managed_user.id()))
      num_items_modified++;
    else
      num_items_added++;
    dict->SetWithoutPathExpansion(managed_user.id(), value);
    seen_ids.insert(managed_user.id());
  }

  for (DictionaryValue::Iterator it(*dict); !it.IsAtEnd(); it.Advance()) {
    if (seen_ids.find(it.key()) != seen_ids.end())
      continue;

    const DictionaryValue* dict = NULL;
    bool success = it.value().GetAsDictionary(&dict);
    DCHECK(success);
    bool acknowledged = false;
    dict->GetBoolean(kAcknowledged, &acknowledged);
    std::string name;
    dict->GetString(kName, &name);
    std::string master_key;
    dict->GetString(kMasterKey, &master_key);
    DCHECK(!name.empty());
    change_list.push_back(
        SyncChange(FROM_HERE, SyncChange::ACTION_ADD,
            CreateLocalSyncData(it.key(), name, acknowledged, master_key)));
  }
  result.set_error(sync_processor_->ProcessSyncChanges(FROM_HERE, change_list));

  result.set_num_items_modified(num_items_modified);
  result.set_num_items_added(num_items_added);
  result.set_num_items_after_association(dict->size());

  return result;
}

void ManagedUserSyncService::StopSyncing(ModelType type) {
  DCHECK_EQ(MANAGED_USERS, type);
  // The observers may want to change the Sync data, so notify them before
  // resetting the |sync_processor_|.
  NotifyManagedUsersSyncingStopped();
  sync_processor_.reset();
  error_handler_.reset();
}

SyncDataList ManagedUserSyncService::GetAllSyncData(
    ModelType type) const {
  SyncDataList data;
  DictionaryPrefUpdate update(prefs_, prefs::kManagedUsers);
  DictionaryValue* dict = update.Get();
  for (DictionaryValue::Iterator it(*dict); !it.IsAtEnd(); it.Advance()) {
    const DictionaryValue* dict = NULL;
    bool success = it.value().GetAsDictionary(&dict);
    DCHECK(success);
    std::string name;
    dict->GetString(kName, &name);
    std::string master_key;
    dict->GetString(kMasterKey, &master_key);
    bool acknowledged = false;
    dict->GetBoolean(kAcknowledged, &acknowledged);
    data.push_back(
        CreateLocalSyncData(it.key(), name, acknowledged, master_key));
  }
  return data;
}

SyncError ManagedUserSyncService::ProcessSyncChanges(
    const tracked_objects::Location& from_here,
    const SyncChangeList& change_list) {
  SyncError error;
  DictionaryPrefUpdate update(prefs_, prefs::kManagedUsers);
  DictionaryValue* dict = update.Get();
  for (SyncChangeList::const_iterator it = change_list.begin();
       it != change_list.end(); ++it) {
    SyncData data = it->sync_data();
    DCHECK_EQ(MANAGED_USERS, data.GetDataType());
    const ManagedUserSpecifics& managed_user =
        data.GetSpecifics().managed_user();
    switch (it->change_type()) {
      case SyncChange::ACTION_ADD:
      case SyncChange::ACTION_UPDATE: {
        // Every item we get from the server should be acknowledged.
        DCHECK(managed_user.acknowledged());
        const DictionaryValue* old_value = NULL;
        dict->GetDictionaryWithoutPathExpansion(managed_user.id(), &old_value);

        // For an update action, the managed user should already exist, for an
        // add action, it should not.
        DCHECK_EQ(
            old_value ? SyncChange::ACTION_UPDATE : SyncChange::ACTION_ADD,
            it->change_type());

        // If the managed user switched from unacknowledged to acknowledged,
        // we might need to continue with a registration.
        if (old_value && !old_value->HasKey(kAcknowledged))
          NotifyManagedUserAcknowledged(managed_user.id());

        DictionaryValue* value = new DictionaryValue;
        value->SetString(kName, managed_user.name());
        value->SetBoolean(kAcknowledged, managed_user.acknowledged());
        value->SetString(kMasterKey, managed_user.master_key());
        dict->SetWithoutPathExpansion(managed_user.id(), value);
        break;
      }
      case SyncChange::ACTION_DELETE: {
        DCHECK(dict->HasKey(managed_user.id())) << managed_user.id();
        dict->RemoveWithoutPathExpansion(managed_user.id(), NULL);
        break;
      }
      case SyncChange::ACTION_INVALID: {
        NOTREACHED();
        break;
      }
    }
  }
  return error;
}

void ManagedUserSyncService::OnLastSignedInUsernameChange() {
  DCHECK(!sync_processor_);

  // If the last signed in user changes, we clear all data, to avoid managed
  // users from one custodian appearing in another one's profile.
  prefs_->ClearPref(prefs::kManagedUsers);
}

void ManagedUserSyncService::NotifyManagedUserAcknowledged(
    const std::string& managed_user_id) {
  FOR_EACH_OBSERVER(ManagedUserSyncServiceObserver, observers_,
                    OnManagedUserAcknowledged(managed_user_id));
}

void ManagedUserSyncService::NotifyManagedUsersSyncingStopped() {
  FOR_EACH_OBSERVER(ManagedUserSyncServiceObserver, observers_,
                    OnManagedUsersSyncingStopped());
}
