// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IPC_MACH_PORT_ATTACHMENT_MAC_H_
#define IPC_MACH_PORT_ATTACHMENT_MAC_H_

#include <mach/mach.h>
#include <stdint.h>

#include "base/process/process_handle.h"
#include "ipc/brokerable_attachment.h"
#include "ipc/ipc_export.h"
#include "ipc/mach_port_mac.h"

namespace IPC {
namespace internal {

// This class represents an OSX mach_port_t attached to a Chrome IPC message.
class IPC_EXPORT MachPortAttachmentMac : public BrokerableAttachment {
 public:
  struct IPC_EXPORT WireFormat {
    // IPC translation requires that classes passed through IPC have a default
    // constructor.
    WireFormat() : mach_port(0), destination_process(0) {}

    WireFormat(uint32_t mach_port,
               const base::ProcessId& destination_process,
               const AttachmentId& attachment_id)
        : mach_port(mach_port),
          destination_process(destination_process),
          attachment_id(attachment_id) {}

    // The mach port that is intended for duplication, or the mach port that has
    // been duplicated, depending on context.
    // The type is uint32_t instead of mach_port_t to ensure that the wire
    // format stays consistent.
    uint32_t mach_port;
    static_assert(sizeof(mach_port_t) <= sizeof(uint32_t),
                  "mach_port_t must be smaller than uint32_t");

    // The id of the destination process that the handle is duplicated into.
    base::ProcessId destination_process;

    AttachmentId attachment_id;
  };

  // This constructor increments the ref count of |mach_port_| and takes
  // ownership of the result. Should only be called by the sender of a Chrome
  // IPC message.
  explicit MachPortAttachmentMac(mach_port_t mach_port);

  // These constructors do not take ownership of |mach_port|, and should only be
  // called by the receiver of a Chrome IPC message.
  explicit MachPortAttachmentMac(const WireFormat& wire_format);
  explicit MachPortAttachmentMac(const BrokerableAttachment::AttachmentId& id);

  BrokerableType GetBrokerableType() const override;

  // Returns the wire format of this attachment.
  WireFormat GetWireFormat(const base::ProcessId& destination) const;

  mach_port_t get_mach_port() const { return mach_port_; }

  // The caller of this method has taken ownership of |mach_port_|.
  void reset_mach_port_ownership() { owns_mach_port_ = false; }

 private:
  ~MachPortAttachmentMac() override;
  const mach_port_t mach_port_;

  // In the sender process, the attachment owns the Mach port of a newly created
  // message. The attachment broker will eventually take ownership, and set
  // this member to |false|.
  // In the destination process, the attachment never owns the Mach port. The
  // client code that receives the Chrome IPC message is always expected to take
  // ownership.
  bool owns_mach_port_;
  DISALLOW_COPY_AND_ASSIGN(MachPortAttachmentMac);
};

}  // namespace internal
}  // namespace IPC

#endif  // IPC_MACH_PORT_ATTACHMENT_MAC_H_
