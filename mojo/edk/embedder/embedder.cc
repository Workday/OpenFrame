// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/edk/embedder/embedder.h"

#include "base/atomicops.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/task_runner.h"
#include "mojo/edk/embedder/embedder_internal.h"
#include "mojo/edk/embedder/platform_channel_pair.h"
#include "mojo/edk/embedder/process_delegate.h"
#include "mojo/edk/embedder/simple_platform_support.h"
#include "mojo/edk/system/broker_state.h"
#include "mojo/edk/system/child_broker.h"
#include "mojo/edk/system/child_broker_host.h"
#include "mojo/edk/system/configuration.h"
#include "mojo/edk/system/core.h"
#include "mojo/edk/system/message_pipe_dispatcher.h"
#include "mojo/edk/system/platform_handle_dispatcher.h"

namespace mojo {
namespace edk {

// TODO(jam): move into annonymous namespace. Keep outside for debugging in VS
// temporarily.
int g_channel_count = 0;
bool g_wait_for_no_more_channels = false;
base::TaskRunner* g_delegate_task_runner = nullptr;  // Used at shutdown.

namespace {

// Note: Called on the I/O thread.
void ShutdownIPCSupportHelper(bool wait_for_no_more_channels) {
  if (wait_for_no_more_channels && g_channel_count) {
    g_wait_for_no_more_channels = true;
    return;
  }

  g_delegate_task_runner->PostTask(
      FROM_HERE, base::Bind(&ProcessDelegate::OnShutdownComplete,
                            base::Unretained(internal::g_process_delegate)));
  g_delegate_task_runner = nullptr;
}

}  // namespace

namespace internal {

// Declared in embedder_internal.h.
Broker* g_broker = nullptr;
PlatformSupport* g_platform_support = nullptr;
Core* g_core = nullptr;

ProcessDelegate* g_process_delegate;
base::TaskRunner* g_io_thread_task_runner = nullptr;

Core* GetCore() {
  return g_core;
}

void ChannelStarted() {
  DCHECK(g_io_thread_task_runner->RunsTasksOnCurrentThread());
  g_channel_count++;
}

void ChannelShutdown() {
  DCHECK(g_io_thread_task_runner->RunsTasksOnCurrentThread());
  DCHECK_GT(g_channel_count, 0);
  g_channel_count--;
  if (!g_channel_count && g_wait_for_no_more_channels) {
    // Reset g_wait_for_no_more_channels for unit tests which initialize and
    // tear down multiple times in a process.
    g_wait_for_no_more_channels = false;
    ShutdownIPCSupportHelper(false);
  }
}

}  // namespace internal

void SetMaxMessageSize(size_t bytes) {
  GetMutableConfiguration()->max_message_num_bytes = bytes;
}

void PreInitializeParentProcess() {
  BrokerState::GetInstance();
}

void PreInitializeChildProcess() {
  ChildBroker::GetInstance();
}

ScopedPlatformHandle ChildProcessLaunched(base::ProcessHandle child_process) {
#if defined(OS_WIN)
  PlatformChannelPair token_channel;
  new ChildBrokerHost(child_process, token_channel.PassServerHandle());
  return token_channel.PassClientHandle();
#else
  // TODO(jam): create this for POSIX. Need to implement channel reading first
  // so we don't leak handles.
  return ScopedPlatformHandle();
#endif
}

void ChildProcessLaunched(base::ProcessHandle child_process,
                          ScopedPlatformHandle server_pipe) {
  new ChildBrokerHost(child_process, server_pipe.Pass());
}

void SetParentPipeHandle(ScopedPlatformHandle pipe) {
  ChildBroker::GetInstance()->SetChildBrokerHostHandle(pipe.Pass());
}

void Init() {
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  if (command_line.HasSwitch("use-new-edk") && !internal::g_broker)
    BrokerState::GetInstance();

  DCHECK(!internal::g_platform_support);
  internal::g_platform_support = new SimplePlatformSupport();

  DCHECK(!internal::g_core);
  internal::g_core = new Core(internal::g_platform_support);
}

MojoResult AsyncWait(MojoHandle handle,
                     MojoHandleSignals signals,
                     const base::Callback<void(MojoResult)>& callback) {
  return internal::g_core->AsyncWait(handle, signals, callback);
}

MojoResult CreatePlatformHandleWrapper(
    ScopedPlatformHandle platform_handle,
    MojoHandle* platform_handle_wrapper_handle) {
  DCHECK(platform_handle_wrapper_handle);

  scoped_refptr<Dispatcher> dispatcher =
      PlatformHandleDispatcher::Create(platform_handle.Pass());

  DCHECK(internal::g_core);
  MojoHandle h = internal::g_core->AddDispatcher(dispatcher);
  if (h == MOJO_HANDLE_INVALID) {
    LOG(ERROR) << "Handle table full";
    dispatcher->Close();
    return MOJO_RESULT_RESOURCE_EXHAUSTED;
  }

  *platform_handle_wrapper_handle = h;
  return MOJO_RESULT_OK;
}

MojoResult PassWrappedPlatformHandle(MojoHandle platform_handle_wrapper_handle,
                                     ScopedPlatformHandle* platform_handle) {
  DCHECK(platform_handle);

  DCHECK(internal::g_core);
  scoped_refptr<Dispatcher> dispatcher(
      internal::g_core->GetDispatcher(platform_handle_wrapper_handle));
  if (!dispatcher)
    return MOJO_RESULT_INVALID_ARGUMENT;

  if (dispatcher->GetType() != Dispatcher::Type::PLATFORM_HANDLE)
    return MOJO_RESULT_INVALID_ARGUMENT;

  *platform_handle =
      static_cast<PlatformHandleDispatcher*>(dispatcher.get())
          ->PassPlatformHandle()
          .Pass();
  return MOJO_RESULT_OK;
}

void InitIPCSupport(ProcessDelegate* process_delegate,
                    scoped_refptr<base::TaskRunner> io_thread_task_runner) {
  // |Init()| must have already been called.
  DCHECK(internal::g_core);
  internal::g_process_delegate = process_delegate;
  internal::g_io_thread_task_runner = io_thread_task_runner.get();
}

void ShutdownIPCSupportOnIOThread() {
}

void ShutdownIPCSupport() {
  g_delegate_task_runner = base::MessageLoop::current()->task_runner().get();
  internal::g_io_thread_task_runner->PostTask(
      FROM_HERE, base::Bind(&ShutdownIPCSupportHelper, false));
}

void ShutdownIPCSupportAndWaitForNoChannels() {
  g_delegate_task_runner = base::MessageLoop::current()->task_runner().get();
  internal::g_io_thread_task_runner->PostTask(
      FROM_HERE, base::Bind(&ShutdownIPCSupportHelper, true));
}

ScopedMessagePipeHandle CreateMessagePipe(
    ScopedPlatformHandle platform_handle) {
  scoped_refptr<MessagePipeDispatcher> dispatcher =
      MessagePipeDispatcher::Create(
          MessagePipeDispatcher::kDefaultCreateOptions);

  ScopedMessagePipeHandle rv(
      MessagePipeHandle(internal::g_core->AddDispatcher(dispatcher)));
  CHECK(rv.is_valid());
  dispatcher->Init(platform_handle.Pass(), nullptr, 0, nullptr, 0, nullptr,
                   nullptr);
  // TODO(vtl): The |.Pass()| below is only needed due to an MSVS bug; remove it
  // once that's fixed.
  return rv.Pass();
}

}  // namespace edk
}  // namespace mojo
