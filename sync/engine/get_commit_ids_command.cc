// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sync/engine/get_commit_ids_command.h"

#include <set>
#include <utility>
#include <vector>

#include "sync/engine/syncer_util.h"
#include "sync/sessions/nudge_tracker.h"
#include "sync/syncable/entry.h"
#include "sync/syncable/nigori_handler.h"
#include "sync/syncable/nigori_util.h"
#include "sync/syncable/syncable_base_transaction.h"
#include "sync/syncable/syncable_util.h"
#include "sync/util/cryptographer.h"

using std::set;
using std::vector;

namespace syncer {

using sessions::OrderedCommitSet;
using sessions::SyncSession;
using sessions::StatusController;

GetCommitIdsCommand::GetCommitIdsCommand(
    syncable::BaseTransaction* trans,
    ModelTypeSet requested_types,
    const size_t commit_batch_size,
    sessions::OrderedCommitSet* commit_set)
    : trans_(trans),
      requested_types_(requested_types),
      requested_commit_batch_size_(commit_batch_size),
      commit_set_(commit_set) {
}

GetCommitIdsCommand::~GetCommitIdsCommand() {}

SyncerError GetCommitIdsCommand::ExecuteImpl(SyncSession* session) {
  // Gather the full set of unsynced items and store it in the session. They
  // are not in the correct order for commit.
  std::set<int64> ready_unsynced_set;
  syncable::Directory::Metahandles all_unsynced_handles;
  GetUnsyncedEntries(trans_,
                     &all_unsynced_handles);

  ModelTypeSet encrypted_types;
  bool passphrase_missing = false;
  Cryptographer* cryptographer =
      session->context()->
      directory()->GetCryptographer(trans_);
  if (cryptographer) {
    encrypted_types = session->context()->directory()->GetNigoriHandler()->
        GetEncryptedTypes(trans_);
    passphrase_missing = cryptographer->has_pending_keys();
  };

  // We filter out all unready entries from the set of unsynced handles. This
  // new set of ready and unsynced items is then what we use to determine what
  // is a candidate for commit.  The caller of this SyncerCommand is responsible
  // for ensuring that no throttled types are included among the
  // requested_types.
  FilterUnreadyEntries(trans_,
                       requested_types_,
                       encrypted_types,
                       passphrase_missing,
                       all_unsynced_handles,
                       &ready_unsynced_set);

  BuildCommitIds(trans_,
                 session->context()->routing_info(),
                 ready_unsynced_set);

  const vector<syncable::Id>& verified_commit_ids =
      commit_set_->GetAllCommitIds();

  for (size_t i = 0; i < verified_commit_ids.size(); i++)
    DVLOG(1) << "Debug commit batch result:" << verified_commit_ids[i];

  return SYNCER_OK;
}

namespace {

bool IsEntryInConflict(const syncable::Entry& entry) {
  if (entry.Get(syncable::IS_UNSYNCED) &&
      entry.Get(syncable::SERVER_VERSION) > 0 &&
      (entry.Get(syncable::SERVER_VERSION) >
       entry.Get(syncable::BASE_VERSION))) {
    // The local and server versions don't match. The item must be in
    // conflict, so there's no point in attempting to commit.
    DCHECK(entry.Get(syncable::IS_UNAPPLIED_UPDATE));
    DVLOG(1) << "Excluding entry from commit due to version mismatch "
             << entry;
    return true;
  }
  return false;
}

// An entry is not considered ready for commit if any are true:
// 1. It's in conflict.
// 2. It requires encryption (either the type is encrypted but a passphrase
//    is missing from the cryptographer, or the entry itself wasn't properly
//    encrypted).
// 3. It's type is currently throttled.
// 4. It's a delete but has not been committed.
bool IsEntryReadyForCommit(ModelTypeSet requested_types,
                           ModelTypeSet encrypted_types,
                           bool passphrase_missing,
                           const syncable::Entry& entry) {
  DCHECK(entry.Get(syncable::IS_UNSYNCED));
  if (IsEntryInConflict(entry))
    return false;

  const ModelType type = entry.GetModelType();
  // We special case the nigori node because even though it is considered an
  // "encrypted type", not all nigori node changes require valid encryption
  // (ex: sync_tabs).
  if ((type != NIGORI) && encrypted_types.Has(type) &&
      (passphrase_missing ||
       syncable::EntryNeedsEncryption(encrypted_types, entry))) {
    // This entry requires encryption but is not properly encrypted (possibly
    // due to the cryptographer not being initialized or the user hasn't
    // provided the most recent passphrase).
    DVLOG(1) << "Excluding entry from commit due to lack of encryption "
             << entry;
    return false;
  }

  // Ignore it if it's not in our set of requested types.
  if (!requested_types.Has(type))
    return false;

  if (entry.Get(syncable::IS_DEL) && !entry.Get(syncable::ID).ServerKnows()) {
    // New clients (following the resolution of crbug.com/125381) should not
    // create such items.  Old clients may have left some in the database
    // (crbug.com/132905), but we should now be cleaning them on startup.
    NOTREACHED() << "Found deleted and unsynced local item: " << entry;
    return false;
  }

  // Extra validity checks.
  syncable::Id id = entry.Get(syncable::ID);
  if (id == entry.Get(syncable::PARENT_ID)) {
    CHECK(id.IsRoot()) << "Non-root item is self parenting." << entry;
    // If the root becomes unsynced it can cause us problems.
    NOTREACHED() << "Root item became unsynced " << entry;
    return false;
  }

  if (entry.IsRoot()) {
    NOTREACHED() << "Permanent item became unsynced " << entry;
    return false;
  }

  DVLOG(2) << "Entry is ready for commit: " << entry;
  return true;
}

}  // namespace

void GetCommitIdsCommand::FilterUnreadyEntries(
    syncable::BaseTransaction* trans,
    ModelTypeSet requested_types,
    ModelTypeSet encrypted_types,
    bool passphrase_missing,
    const syncable::Directory::Metahandles& unsynced_handles,
    std::set<int64>* ready_unsynced_set) {
  for (syncable::Directory::Metahandles::const_iterator iter =
       unsynced_handles.begin(); iter != unsynced_handles.end(); ++iter) {
    syncable::Entry entry(trans, syncable::GET_BY_HANDLE, *iter);
    if (IsEntryReadyForCommit(requested_types,
                              encrypted_types,
                              passphrase_missing,
                              entry)) {
      ready_unsynced_set->insert(*iter);
    }
  }
}

bool GetCommitIdsCommand::AddUncommittedParentsAndTheirPredecessors(
    syncable::BaseTransaction* trans,
    const ModelSafeRoutingInfo& routes,
    const std::set<int64>& ready_unsynced_set,
    const syncable::Entry& item,
    sessions::OrderedCommitSet* result) const {
  OrderedCommitSet item_dependencies(routes);
  syncable::Id parent_id = item.Get(syncable::PARENT_ID);

  // Climb the tree adding entries leaf -> root.
  while (!parent_id.ServerKnows()) {
    syncable::Entry parent(trans, syncable::GET_BY_ID, parent_id);
    CHECK(parent.good()) << "Bad user-only parent in item path.";
    int64 handle = parent.Get(syncable::META_HANDLE);
    if (commit_set_->HaveCommitItem(handle)) {
      // We've already added this parent (and therefore all of its parents).
      // We can return early.
      break;
    }
    if (IsEntryInConflict(parent)) {
      // We ignore all entries that are children of a conflicing item.  Return
      // false immediately to forget the traversal we've built up so far.
      DVLOG(1) << "Parent was in conflict, omitting " << item;
      return false;
    }
    AddItemThenPredecessors(trans,
                            ready_unsynced_set,
                            parent,
                            &item_dependencies);
    parent_id = parent.Get(syncable::PARENT_ID);
  }

  // Reverse what we added to get the correct order.
  result->AppendReverse(item_dependencies);
  return true;
}

// Adds the given item to the list if it is unsynced and ready for commit.
void GetCommitIdsCommand::TryAddItem(const std::set<int64>& ready_unsynced_set,
                                     const syncable::Entry& item,
                                     OrderedCommitSet* result) const {
  DCHECK(item.Get(syncable::IS_UNSYNCED));
  int64 item_handle = item.Get(syncable::META_HANDLE);
  if (ready_unsynced_set.count(item_handle) != 0) {
    result->AddCommitItem(item_handle, item.Get(syncable::ID),
                          item.GetModelType());
  }
}

// Adds the given item, and all its unsynced predecessors.  The traversal will
// be cut short if any item along the traversal is not IS_UNSYNCED, or if we
// detect that this area of the tree has already been traversed.  Items that are
// not 'ready' for commit (see IsEntryReadyForCommit()) will not be added to the
// list, though they will not stop the traversal.
void GetCommitIdsCommand::AddItemThenPredecessors(
    syncable::BaseTransaction* trans,
    const std::set<int64>& ready_unsynced_set,
    const syncable::Entry& item,
    OrderedCommitSet* result) const {
  int64 item_handle = item.Get(syncable::META_HANDLE);
  if (commit_set_->HaveCommitItem(item_handle)) {
    // We've already added this item to the commit set, and so must have
    // already added the predecessors as well.
    return;
  }
  TryAddItem(ready_unsynced_set, item, result);
  if (item.Get(syncable::IS_DEL))
    return;  // Deleted items have no predecessors.

  syncable::Id prev_id = item.GetPredecessorId();
  while (!prev_id.IsRoot()) {
    syncable::Entry prev(trans, syncable::GET_BY_ID, prev_id);
    CHECK(prev.good()) << "Bad id when walking predecessors.";
    if (!prev.Get(syncable::IS_UNSYNCED)) {
      // We're interested in "runs" of unsynced items.  This item breaks
      // the streak, so we stop traversing.
      return;
    }
    int64 handle = prev.Get(syncable::META_HANDLE);
    if (commit_set_->HaveCommitItem(handle)) {
      // We've already added this item to the commit set, and so must have
      // already added the predecessors as well.
      return;
    }
    TryAddItem(ready_unsynced_set, prev, result);
    prev_id = prev.GetPredecessorId();
  }
}

// Same as AddItemThenPredecessor, but the traversal order will be reversed.
void GetCommitIdsCommand::AddPredecessorsThenItem(
    syncable::BaseTransaction* trans,
    const ModelSafeRoutingInfo& routes,
    const std::set<int64>& ready_unsynced_set,
    const syncable::Entry& item,
    OrderedCommitSet* result) const {
  OrderedCommitSet item_dependencies(routes);
  AddItemThenPredecessors(trans, ready_unsynced_set, item, &item_dependencies);

  // Reverse what we added to get the correct order.
  result->AppendReverse(item_dependencies);
}

bool GetCommitIdsCommand::IsCommitBatchFull() const {
  return commit_set_->Size() >= requested_commit_batch_size_;
}

void GetCommitIdsCommand::AddCreatesAndMoves(
    syncable::BaseTransaction* trans,
    const ModelSafeRoutingInfo& routes,
    const std::set<int64>& ready_unsynced_set) {
  // Add moves and creates, and prepend their uncommitted parents.
  for (std::set<int64>::const_iterator iter = ready_unsynced_set.begin();
       !IsCommitBatchFull() && iter != ready_unsynced_set.end(); ++iter) {
    int64 metahandle = *iter;
    if (commit_set_->HaveCommitItem(metahandle))
      continue;

    syncable::Entry entry(trans,
                          syncable::GET_BY_HANDLE,
                          metahandle);
    if (!entry.Get(syncable::IS_DEL)) {
      // We only commit an item + its dependencies if it and all its
      // dependencies are not in conflict.
      OrderedCommitSet item_dependencies(routes);
      if (AddUncommittedParentsAndTheirPredecessors(
              trans,
              routes,
              ready_unsynced_set,
              entry,
              &item_dependencies)) {
        AddPredecessorsThenItem(trans,
                                routes,
                                ready_unsynced_set,
                                entry,
                                &item_dependencies);
        commit_set_->Append(item_dependencies);
      }
    }
  }

  // It's possible that we overcommitted while trying to expand dependent
  // items.  If so, truncate the set down to the allowed size.
  commit_set_->Truncate(requested_commit_batch_size_);
}

void GetCommitIdsCommand::AddDeletes(
    syncable::BaseTransaction* trans,
    const std::set<int64>& ready_unsynced_set) {
  set<syncable::Id> legal_delete_parents;

  for (std::set<int64>::const_iterator iter = ready_unsynced_set.begin();
       !IsCommitBatchFull() && iter != ready_unsynced_set.end(); ++iter) {
    int64 metahandle = *iter;
    if (commit_set_->HaveCommitItem(metahandle))
      continue;

    syncable::Entry entry(trans, syncable::GET_BY_HANDLE,
                          metahandle);

    if (entry.Get(syncable::IS_DEL)) {
      syncable::Entry parent(trans, syncable::GET_BY_ID,
                             entry.Get(syncable::PARENT_ID));
      // If the parent is deleted and unsynced, then any children of that
      // parent don't need to be added to the delete queue.
      //
      // Note: the parent could be synced if there was an update deleting a
      // folder when we had a deleted all items in it.
      // We may get more updates, or we may want to delete the entry.
      if (parent.good() &&
          parent.Get(syncable::IS_DEL) &&
          parent.Get(syncable::IS_UNSYNCED)) {
        // However, if an entry is moved, these rules can apply differently.
        //
        // If the entry was moved, then the destination parent was deleted,
        // then we'll miss it in the roll up. We have to add it in manually.
        // TODO(chron): Unit test for move / delete cases:
        // Case 1: Locally moved, then parent deleted
        // Case 2: Server moved, then locally issue recursive delete.
        if (entry.Get(syncable::ID).ServerKnows() &&
            entry.Get(syncable::PARENT_ID) !=
                entry.Get(syncable::SERVER_PARENT_ID)) {
          DVLOG(1) << "Inserting moved and deleted entry, will be missed by "
                   << "delete roll." << entry.Get(syncable::ID);

          commit_set_->AddCommitItem(metahandle,
              entry.Get(syncable::ID),
              entry.GetModelType());
        }

        // Skip this entry since it's a child of a parent that will be
        // deleted. The server will unroll the delete and delete the
        // child as well.
        continue;
      }

      legal_delete_parents.insert(entry.Get(syncable::PARENT_ID));
    }
  }

  // We could store all the potential entries with a particular parent during
  // the above scan, but instead we rescan here. This is less efficient, but
  // we're dropping memory alloc/dealloc in favor of linear scans of recently
  // examined entries.
  //
  // Scan through the UnsyncedMetaHandles again. If we have a deleted
  // entry, then check if the parent is in legal_delete_parents.
  //
  // Parent being in legal_delete_parents means for the child:
  //   a recursive delete is not currently happening (no recent deletes in same
  //     folder)
  //   parent did expect at least one old deleted child
  //   parent was not deleted
  for (std::set<int64>::const_iterator iter = ready_unsynced_set.begin();
       !IsCommitBatchFull() && iter != ready_unsynced_set.end(); ++iter) {
    int64 metahandle = *iter;
    if (commit_set_->HaveCommitItem(metahandle))
      continue;
    syncable::Entry entry(trans, syncable::GET_BY_HANDLE,
                          metahandle);
    if (entry.Get(syncable::IS_DEL)) {
      syncable::Id parent_id = entry.Get(syncable::PARENT_ID);
      if (legal_delete_parents.count(parent_id)) {
        commit_set_->AddCommitItem(metahandle, entry.Get(syncable::ID),
            entry.GetModelType());
      }
    }
  }
}

void GetCommitIdsCommand::BuildCommitIds(
    syncable::BaseTransaction* trans,
    const ModelSafeRoutingInfo& routes,
    const std::set<int64>& ready_unsynced_set) {
  // Commits follow these rules:
  // 1. Moves or creates are preceded by needed folder creates, from
  //    root to leaf.  For folders whose contents are ordered, moves
  //    and creates appear in order.
  // 2. Moves/Creates before deletes.
  // 3. Deletes, collapsed.
  // We commit deleted moves under deleted items as moves when collapsing
  // delete trees.

  // Add moves and creates, and prepend their uncommitted parents.
  AddCreatesAndMoves(trans, routes, ready_unsynced_set);

  // Add all deletes.
  AddDeletes(trans, ready_unsynced_set);
}

}  // namespace syncer
