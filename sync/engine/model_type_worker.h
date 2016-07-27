// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYNC_ENGINE_MODEL_TYPE_WORKER_H_
#define SYNC_ENGINE_MODEL_TYPE_WORKER_H_

#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/non_thread_safe.h"
#include "sync/base/sync_export.h"
#include "sync/engine/commit_contributor.h"
#include "sync/engine/commit_queue.h"
#include "sync/engine/nudge_handler.h"
#include "sync/engine/update_handler.h"
#include "sync/internal_api/public/base/model_type.h"
#include "sync/internal_api/public/non_blocking_sync_common.h"
#include "sync/internal_api/public/sync_encryption_handler.h"
#include "sync/protocol/sync.pb.h"
#include "sync/util/cryptographer.h"

namespace base {
class SingleThreadTaskRunner;
}

namespace syncer_v2 {

class ModelTypeProcessor;
class EntityTracker;

// A smart cache for sync types that use message passing (rather than
// transactions and the syncable::Directory) to communicate with the sync
// thread.
//
// When the non-blocking sync type wants to talk with the sync server, it will
// send a message from its thread to this object on the sync thread.  This
// object ensures the appropriate sync server communication gets scheduled and
// executed.  The response, if any, will be returned to the non-blocking sync
// type's thread eventually.
//
// This object also has a role to play in communications in the opposite
// direction.  Sometimes the sync thread will receive changes from the sync
// server and deliver them here.  This object will post this information back to
// the appropriate component on the model type's thread.
//
// This object does more than just pass along messages.  It understands the sync
// protocol, and it can make decisions when it sees conflicting messages.  For
// example, if the sync server sends down an update for a sync entity that is
// currently pending for commit, this object will detect this condition and
// cancel the pending commit.
class SYNC_EXPORT ModelTypeWorker : public syncer::UpdateHandler,
                                    public syncer::CommitContributor,
                                    public CommitQueue,
                                    public base::NonThreadSafe {
 public:
  ModelTypeWorker(syncer::ModelType type,
                  const DataTypeState& initial_state,
                  const UpdateResponseDataList& saved_pending_updates,
                  scoped_ptr<syncer::Cryptographer> cryptographer,
                  syncer::NudgeHandler* nudge_handler,
                  scoped_ptr<ModelTypeProcessor> model_type_processor);
  ~ModelTypeWorker() override;

  syncer::ModelType GetModelType() const;

  bool IsEncryptionRequired() const;
  void UpdateCryptographer(scoped_ptr<syncer::Cryptographer> cryptographer);

  // UpdateHandler implementation.
  void GetDownloadProgress(
      sync_pb::DataTypeProgressMarker* progress_marker) const override;
  void GetDataTypeContext(sync_pb::DataTypeContext* context) const override;
  syncer::SyncerError ProcessGetUpdatesResponse(
      const sync_pb::DataTypeProgressMarker& progress_marker,
      const sync_pb::DataTypeContext& mutated_context,
      const SyncEntityList& applicable_updates,
      syncer::sessions::StatusController* status) override;
  void ApplyUpdates(syncer::sessions::StatusController* status) override;
  void PassiveApplyUpdates(syncer::sessions::StatusController* status) override;

  // CommitQueue implementation.
  void EnqueueForCommit(const CommitRequestDataList& request_list) override;

  // CommitContributor implementation.
  scoped_ptr<syncer::CommitContribution> GetContribution(
      size_t max_entries) override;

  // Callback for when our contribution gets a response.
  void OnCommitResponse(const CommitResponseDataList& response_list);

  base::WeakPtr<ModelTypeWorker> AsWeakPtr();

 private:
  using EntityMap = std::map<std::string, scoped_ptr<EntityTracker>>;

  // Stores a single commit request in this object's internal state.
  void StorePendingCommit(const CommitRequestData& request);

  // Returns true if this type has successfully fetched all available updates
  // from the server at least once.  Our state may or may not be stale, but at
  // least we know that it was valid at some point in the past.
  bool IsTypeInitialized() const;

  // Returns true if this type is prepared to commit items.  Currently, this
  // depends on having downloaded the initial data and having the encryption
  // settings in a good state.
  bool CanCommitItems() const;

  // Initializes the parts of a commit entity that are the responsibility of
  // this class, and not the EntityTracker.  Some fields, like the
  // client-assigned ID, can only be set by an entity with knowledge of the
  // entire data type's state.
  void HelpInitializeCommitEntity(sync_pb::SyncEntity* commit_entity);

  // Attempts to decrypt pending updates stored in the EntityMap.  If
  // successful, will remove the update from the its EntityTracker and forward
  // it to the proxy thread for application.  Will forward any new encryption
  // keys to the proxy to trigger re-encryption if necessary.
  void OnCryptographerUpdated();

  // Attempts to decrypt the given specifics and return them in the |out|
  // parameter.  Assumes cryptographer->CanDecrypt(specifics) returned true.
  //
  // Returns false if the decryption failed.  There are no guarantees about the
  // contents of |out| when that happens.
  //
  // In theory, this should never fail.  Only corrupt or invalid entries could
  // cause this to fail, and no clients are known to create such entries.  The
  // failure case is an attempt to be defensive against bad input.
  static bool DecryptSpecifics(syncer::Cryptographer* cryptographer,
                               const sync_pb::EntitySpecifics& in,
                               sync_pb::EntitySpecifics* out);

  syncer::ModelType type_;

  // State that applies to the entire model type.
  DataTypeState data_type_state_;

  // Pointer to the ModelTypeProcessor associated with this worker.
  // This is NULL when no proxy is connected..
  scoped_ptr<ModelTypeProcessor> model_type_processor_;

  // A private copy of the most recent cryptographer known to sync.
  // Initialized at construction time and updated with UpdateCryptographer().
  // NULL if encryption is not enabled for this type.
  scoped_ptr<syncer::Cryptographer> cryptographer_;

  // Interface used to access and send nudges to the sync scheduler.  Not owned.
  syncer::NudgeHandler* nudge_handler_;

  // A map of per-entity information known to this object.
  //
  // When commits are pending, their information is stored here.  This
  // information is dropped from memory when the commit succeeds or gets
  // cancelled.
  //
  // This also stores some information related to received server state in
  // order to implement reflection blocking and conflict detection.  This
  // information is kept in memory indefinitely.  With a bit more coordination
  // with the model thread, we could optimize this to reduce memory usage in
  // the steady state.
  EntityMap entities_;

  base::WeakPtrFactory<ModelTypeWorker> weak_ptr_factory_;
};

}  // namespace syncer

#endif  // SYNC_ENGINE_MODEL_TYPE_WORKER_H_
