// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ipc/attachment_broker_privileged_win.h"

#include <windows.h>

#include "base/process/process.h"
#include "ipc/attachment_broker_messages.h"
#include "ipc/brokerable_attachment.h"
#include "ipc/handle_attachment_win.h"
#include "ipc/ipc_channel.h"

namespace IPC {

AttachmentBrokerPrivilegedWin::AttachmentBrokerPrivilegedWin() {}

AttachmentBrokerPrivilegedWin::~AttachmentBrokerPrivilegedWin() {}

bool AttachmentBrokerPrivilegedWin::SendAttachmentToProcess(
    const scoped_refptr<IPC::BrokerableAttachment>& attachment,
    base::ProcessId destination_process) {
  switch (attachment->GetBrokerableType()) {
    case BrokerableAttachment::WIN_HANDLE: {
      const internal::HandleAttachmentWin* handle_attachment =
          static_cast<const internal::HandleAttachmentWin*>(attachment.get());
      HandleWireFormat wire_format =
          handle_attachment->GetWireFormat(destination_process);
      HandleWireFormat new_wire_format =
          DuplicateWinHandle(wire_format, base::Process::Current().Pid());
      if (new_wire_format.handle == 0)
        return false;
      RouteDuplicatedHandle(new_wire_format);
      return true;
    }
    case BrokerableAttachment::MACH_PORT:
    case BrokerableAttachment::PLACEHOLDER:
      NOTREACHED();
      return false;
  }
  return false;
}

bool AttachmentBrokerPrivilegedWin::OnMessageReceived(const Message& msg) {
  bool handled = true;
  switch (msg.type()) {
    IPC_MESSAGE_HANDLER_GENERIC(AttachmentBrokerMsg_DuplicateWinHandle,
                                OnDuplicateWinHandle(msg))
    IPC_MESSAGE_UNHANDLED(handled = false)
  }
  return handled;
}

void AttachmentBrokerPrivilegedWin::OnDuplicateWinHandle(
    const IPC::Message& message) {
  AttachmentBrokerMsg_DuplicateWinHandle::Param param;
  if (!AttachmentBrokerMsg_DuplicateWinHandle::Read(&message, &param))
    return;
  IPC::internal::HandleAttachmentWin::WireFormat wire_format =
      base::get<0>(param);

  if (wire_format.destination_process == base::kNullProcessId) {
    LogError(NO_DESTINATION);
    return;
  }

  HandleWireFormat new_wire_format =
      DuplicateWinHandle(wire_format, message.get_sender_pid());
  RouteDuplicatedHandle(new_wire_format);
}

void AttachmentBrokerPrivilegedWin::RouteDuplicatedHandle(
    const HandleWireFormat& wire_format) {
  // This process is the destination.
  if (wire_format.destination_process == base::Process::Current().Pid()) {
    scoped_refptr<BrokerableAttachment> attachment(
        new internal::HandleAttachmentWin(wire_format));
    HandleReceivedAttachment(attachment);
    return;
  }

  // Another process is the destination.
  base::ProcessId dest = wire_format.destination_process;
  Sender* sender = GetSenderWithProcessId(dest);
  if (!sender) {
    // Assuming that this message was not sent from a malicious process, the
    // channel endpoint that would have received this message will block
    // forever.
    LOG(ERROR) << "Failed to deliver brokerable attachment to process with id: "
               << dest;
    LogError(DESTINATION_NOT_FOUND);
    return;
  }

  LogError(DESTINATION_FOUND);
  sender->Send(new AttachmentBrokerMsg_WinHandleHasBeenDuplicated(wire_format));
}

AttachmentBrokerPrivilegedWin::HandleWireFormat
AttachmentBrokerPrivilegedWin::DuplicateWinHandle(
    const HandleWireFormat& wire_format,
    base::ProcessId source_pid) {
  base::Process source_process =
      base::Process::OpenWithExtraPrivileges(source_pid);
  base::Process dest_process =
      base::Process::OpenWithExtraPrivileges(wire_format.destination_process);
  int new_wire_format_handle = 0;
  if (source_process.Handle() && dest_process.Handle()) {
    DWORD desired_access = 0;
    DWORD options = 0;
    switch (wire_format.permissions) {
      case HandleWin::INVALID:
        LOG(ERROR) << "Received invalid permissions for duplication.";
        return CopyWireFormat(wire_format, 0);
      case HandleWin::DUPLICATE:
        options = DUPLICATE_SAME_ACCESS;
        break;
      case HandleWin::FILE_READ_WRITE:
        desired_access = FILE_GENERIC_READ | FILE_GENERIC_WRITE;
        break;
    }

    HANDLE new_handle;
    HANDLE original_handle = LongToHandle(wire_format.handle);
    DWORD result = ::DuplicateHandle(source_process.Handle(), original_handle,
                                     dest_process.Handle(), &new_handle,
                                     desired_access, FALSE, options);

    new_wire_format_handle = (result != 0) ? HandleToLong(new_handle) : 0;
  }

  return CopyWireFormat(wire_format, new_wire_format_handle);
}

AttachmentBrokerPrivilegedWin::HandleWireFormat
AttachmentBrokerPrivilegedWin::CopyWireFormat(
    const HandleWireFormat& wire_format,
    int handle) {
  return HandleWireFormat(handle, wire_format.destination_process,
                          wire_format.permissions, wire_format.attachment_id);
}

}  // namespace IPC
