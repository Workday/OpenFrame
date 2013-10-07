// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYNC_ENGINE_GET_COMMIT_IDS_COMMAND_H_
#define SYNC_ENGINE_GET_COMMIT_IDS_COMMAND_H_

#include <utility>
#include <vector>

#include "base/compiler_specific.h"
#include "sync/base/sync_export.h"
#include "sync/engine/syncer_command.h"
#include "sync/engine/syncer_util.h"
#include "sync/sessions/sync_session.h"
#include "sync/syncable/directory.h"

using std::pair;
using std::vector;

namespace syncer {

namespace sessions {
class OrderedCommitSet;
}

// A class that contains the code used to search the syncable::Directory for
// locally modified items that are ready to be committed to the server.
//
// See SyncerCommand documentation for more info.
class SYNC_EXPORT_PRIVATE GetCommitIdsCommand : public SyncerCommand {
  friend class SyncerTest;

 public:
  // The batch_size parameter is the maximum number of entries we are allowed
  // to commit in a single batch.  This value can be modified by the server.
  //
  // The ordered_commit_set parameter is an output parameter that will contain a
  // set of items that are ready to commit.  Its size shall not exceed the
  // provided batch_size.  This contents of this "set" will be ordered; see the
  // comments in this class' implementation for details.
  GetCommitIdsCommand(syncable::BaseTransaction* trans,
                      ModelTypeSet requested_types,
                      const size_t commit_batch_size,
                      sessions::OrderedCommitSet* ordered_commit_set);
  virtual ~GetCommitIdsCommand();

  // SyncerCommand implementation.
  virtual SyncerError ExecuteImpl(sessions::SyncSession* session) OVERRIDE;

  // Builds a vector of IDs that should be committed.
  void BuildCommitIds(syncable::BaseTransaction* write_transaction,
                      const ModelSafeRoutingInfo& routes,
                      const std::set<int64>& ready_unsynced_set);

  // Fill |ready_unsynced_set| with all entries from |unsynced_handles| that
  // are ready to commit.
  // An entry is not considered ready for commit if any are true:
  // 1. It's in conflict.
  // 2. It requires encryption (either the type is encrypted but a passphrase
  //    is missing from the cryptographer, or the entry itself wasn't properly
  //    encrypted).
  // 3. It's type is currently throttled.
  // 4. It's a delete but has not been committed.
  void FilterUnreadyEntries(
      syncable::BaseTransaction* trans,
      ModelTypeSet throttled_types,
      ModelTypeSet encrypted_types,
      bool passphrase_missing,
      const syncable::Directory::Metahandles& unsynced_handles,
      std::set<int64>* ready_unsynced_set);

 private:
  // Add all the uncommitted parents of |item| to |result| if they are ready to
  // commit. Entries are added in root->child order and predecessor->successor
  // order.
  // Returns values:
  //    False: if a parent item was in conflict, and hence no child cannot be
  //           committed.
  //    True: if all parents were checked for commit readiness and were added to
  //          |result| as necessary.
  bool AddUncommittedParentsAndTheirPredecessors(
      syncable::BaseTransaction* trans,
      const ModelSafeRoutingInfo& routes,
      const std::set<int64>& ready_unsynced_set,
      const syncable::Entry& item,
      sessions::OrderedCommitSet* result) const;

  // OrderedCommitSet helpers for adding predecessors in order.

  // Adds |item| to |result| if it's ready for committing and was not already
  // present.
  // Prereq: |item| is unsynced.
  void TryAddItem(const std::set<int64>& ready_unsynced_set,
                  const syncable::Entry& item,
                  sessions::OrderedCommitSet* result) const;

  // Adds |item| and all its unsynced predecessors to |result| as necessary.
  // Entries that are unsynced but not ready to commit are not added to the
  // list, though they do not stop the traversal.
  void AddItemThenPredecessors(syncable::BaseTransaction* trans,
                               const std::set<int64>& ready_unsynced_set,
                               const syncable::Entry& item,
                               sessions::OrderedCommitSet* result) const;

  // Appends all commit ready predecessors of |item|, followed by |item| itself,
  // to |commit_set|.
  void AddPredecessorsThenItem(syncable::BaseTransaction* trans,
                               const ModelSafeRoutingInfo& routes,
                               const std::set<int64>& ready_unsynced_set,
                               const syncable::Entry& item,
                               sessions::OrderedCommitSet* commit_set) const;

  bool IsCommitBatchFull() const;

  void AddCreatesAndMoves(syncable::BaseTransaction* write_transaction,
                          const ModelSafeRoutingInfo& routes,
                          const std::set<int64>& ready_unsynced_set);

  void AddDeletes(syncable::BaseTransaction* write_transaction,
                  const std::set<int64>& ready_unsynced_set);

  // A pointer to a valid transaction not owned by this class.
  syncable::BaseTransaction* trans_;

  // The set of types from which to draw commit IDs.
  const ModelTypeSet requested_types_;

  // Input parameter; see constructor comment.
  const size_t requested_commit_batch_size_;

  // Output parameter; see constructor comment.
  sessions::OrderedCommitSet* commit_set_;

  DISALLOW_COPY_AND_ASSIGN(GetCommitIdsCommand);
};

}  // namespace syncer

#endif  // SYNC_ENGINE_GET_COMMIT_IDS_COMMAND_H_
