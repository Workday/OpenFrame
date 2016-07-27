// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYNC_INTERNAL_API_EVENTS_CONFIGURE_GET_UPDATES_REQUEST_H
#define SYNC_INTERNAL_API_EVENTS_CONFIGURE_GET_UPDATES_REQUEST_H

#include "base/memory/scoped_ptr.h"
#include "base/time/time.h"
#include "base/values.h"
#include "sync/base/sync_export.h"
#include "sync/internal_api/public/events/protocol_event.h"
#include "sync/protocol/sync.pb.h"

namespace syncer {

// An event representing a configure GetUpdates request to the server.
class SYNC_EXPORT_PRIVATE ConfigureGetUpdatesRequestEvent
    : public ProtocolEvent {
 public:
  ConfigureGetUpdatesRequestEvent(
      base::Time timestamp,
      sync_pb::SyncEnums::GetUpdatesOrigin origin,
      const sync_pb::ClientToServerMessage& request);
  ~ConfigureGetUpdatesRequestEvent() override;

  base::Time GetTimestamp() const override;
  std::string GetType() const override;
  std::string GetDetails() const override;
  scoped_ptr<base::DictionaryValue> GetProtoMessage() const override;
  scoped_ptr<ProtocolEvent> Clone() const override;

 private:
  const base::Time timestamp_;
  const sync_pb::SyncEnums::GetUpdatesOrigin origin_;
  const sync_pb::ClientToServerMessage request_;

  DISALLOW_COPY_AND_ASSIGN(ConfigureGetUpdatesRequestEvent);
};

}  // namespace syncer

#endif  // SYNC_INTERNAL_API_EVENTS_CONFIGURE_GET_UPDATES_REQUEST_H
