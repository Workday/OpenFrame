// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_MOJO_SRC_MOJO_EDK_SYSTEM_CONNECTION_MANAGER_MESSAGES_H_
#define THIRD_PARTY_MOJO_SRC_MOJO_EDK_SYSTEM_CONNECTION_MANAGER_MESSAGES_H_

#include <string.h>

#include "third_party/mojo/src/mojo/edk/system/process_identifier.h"
#include "third_party/mojo/src/mojo/edk/system/system_impl_export.h"

namespace mojo {
namespace system {

struct ConnectionManagerAckSuccessConnectData {
  // Set to the process identifier of the process that the receiver of this ack
  // ack should connect to.
  ProcessIdentifier peer_process_identifier;

  // Whether the receiver of this ack is the first party in the connection. This
  // is typically used to decide whether the receiver should initiate the
  // message (or data) pipe (i.e., allocate endpoint IDs, etc.).
  bool is_first;
};

}  // namespace system
}  // namespace mojo

#endif  // THIRD_PARTY_MOJO_SRC_MOJO_EDK_SYSTEM_CONNECTION_MANAGER_MESSAGES_H_
