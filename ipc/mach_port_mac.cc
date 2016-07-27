// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ipc/mach_port_mac.h"

#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/strings/string_number_conversions.h"
#include "ipc/mach_port_attachment_mac.h"

namespace IPC {

// static
void ParamTraits<MachPortMac>::Write(Message* m, const param_type& p) {
  if (!m->WriteAttachment(
          new IPC::internal::MachPortAttachmentMac(p.get_mach_port()))) {
    NOTREACHED();
  }
}

// static
bool ParamTraits<MachPortMac>::Read(const Message* m,
                                    base::PickleIterator* iter,
                                    param_type* r) {
  scoped_refptr<MessageAttachment> attachment;
  if (!m->ReadAttachment(iter, &attachment))
    return false;
  if (attachment->GetType() != MessageAttachment::TYPE_BROKERABLE_ATTACHMENT)
    return false;
  BrokerableAttachment* brokerable_attachment =
      static_cast<BrokerableAttachment*>(attachment.get());
  if (brokerable_attachment->GetBrokerableType() !=
      BrokerableAttachment::MACH_PORT) {
    return false;
  }
  IPC::internal::MachPortAttachmentMac* mach_port_attachment =
      static_cast<IPC::internal::MachPortAttachmentMac*>(brokerable_attachment);
  r->set_mach_port(mach_port_attachment->get_mach_port());
  return true;
}

// static
void ParamTraits<MachPortMac>::Log(const param_type& p, std::string* l) {
  l->append(base::StringPrintf("mach port: 0x%X", p.get_mach_port()));
}

}  // namespace IPC
