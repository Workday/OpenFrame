// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYNC_INTERNAL_API_PUBLIC_WRITE_TRANSACTION_H_
#define SYNC_INTERNAL_API_PUBLIC_WRITE_TRANSACTION_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "sync/base/sync_export.h"
#include "sync/internal_api/public/base_transaction.h"

namespace tracked_objects {
class Location;
}  // namespace tracked_objects

namespace syncer {

namespace syncable {
class BaseTransaction;
class WriteTransaction;
}  // namespace syncable

// Sync API's WriteTransaction is a read/write BaseTransaction.  It wraps
// a syncable::WriteTransaction.
//
// NOTE: Only a single model type can be mutated for a given
// WriteTransaction.
class SYNC_EXPORT WriteTransaction : public BaseTransaction {
 public:
  // Start a new read/write transaction.
  WriteTransaction(const tracked_objects::Location& from_here,
                   UserShare* share);
  // |transaction_version| stores updated model and nodes version if model
  // is changed by the transaction, or syncer::syncable::kInvalidTransaction
  // if not after transaction is closed. This constructor is used for model
  // types that support embassy data.
  WriteTransaction(const tracked_objects::Location& from_here,
                   UserShare* share, int64* transaction_version);
  virtual ~WriteTransaction();

  // Provide access to the syncable transaction from the API WriteNode.
  virtual syncable::BaseTransaction* GetWrappedTrans() const OVERRIDE;
  syncable::WriteTransaction* GetWrappedWriteTrans() { return transaction_; }

 protected:
  WriteTransaction() {}

  void SetTransaction(syncable::WriteTransaction* trans) {
    transaction_ = trans;
  }

 private:
  void* operator new(size_t size);  // Transaction is meant for stack use only.

  // The underlying syncable object which this class wraps.
  syncable::WriteTransaction* transaction_;

  DISALLOW_COPY_AND_ASSIGN(WriteTransaction);
};

}  // namespace syncer

#endif  // SYNC_INTERNAL_API_PUBLIC_WRITE_TRANSACTION_H_
