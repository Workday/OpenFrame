// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_EDK_EMBEDDER_EMBEDDER_H_
#define MOJO_EDK_EMBEDDER_EMBEDDER_H_

#include <string>

#include "base/callback.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/process/process_handle.h"
#include "base/task_runner.h"
#include "mojo/edk/embedder/scoped_platform_handle.h"
#include "mojo/edk/system/system_impl_export.h"
#include "mojo/public/cpp/system/message_pipe.h"

namespace mojo {
namespace edk {

class ProcessDelegate;

// Basic configuration/initialization ------------------------------------------

// |Init()| sets up the basic Mojo system environment, making the |Mojo...()|
// functions available and functional. This is never shut down (except in tests
// -- see test_embedder.h).

// Allows changing the default max message size. Must be called before Init.
MOJO_SYSTEM_IMPL_EXPORT void SetMaxMessageSize(size_t bytes);

// Must be called before Init in the parent (unsandboxed) process.
MOJO_SYSTEM_IMPL_EXPORT void PreInitializeParentProcess();

// Must be called before Init in the child (sandboxed) process.
MOJO_SYSTEM_IMPL_EXPORT void PreInitializeChildProcess();

// Called in the parent process for each child process that is launched. The
// returned handle must be sent to the child process which then calls
// SetParentPipeHandle.
MOJO_SYSTEM_IMPL_EXPORT ScopedPlatformHandle ChildProcessLaunched(
    base::ProcessHandle child_process);
// Like above, except used when the embedder establishes the pipe between the
// parent and child processes itself.
MOJO_SYSTEM_IMPL_EXPORT void ChildProcessLaunched(
    base::ProcessHandle child_process, ScopedPlatformHandle server_pipe);

// Should be called as early as possible in the child process with the handle
// that the parent received from ChildProcessLaunched.
MOJO_SYSTEM_IMPL_EXPORT void SetParentPipeHandle(ScopedPlatformHandle pipe);

// Must be called first, or just after setting configuration parameters, to
// initialize the (global, singleton) system.
MOJO_SYSTEM_IMPL_EXPORT void Init();

// Basic functions -------------------------------------------------------------

// The functions in this section are available once |Init()| has been called.

// Start waiting on the handle asynchronously. On success, |callback| will be
// called exactly once, when |handle| satisfies a signal in |signals| or it
// becomes known that it will never do so. |callback| will be executed on an
// arbitrary thread, so it must not call any Mojo system or embedder functions.
MOJO_SYSTEM_IMPL_EXPORT MojoResult
AsyncWait(MojoHandle handle,
          MojoHandleSignals signals,
          const base::Callback<void(MojoResult)>& callback);

// Creates a |MojoHandle| that wraps the given |PlatformHandle| (taking
// ownership of it). This |MojoHandle| can then, e.g., be passed through message
// pipes. Note: This takes ownership (and thus closes) |platform_handle| even on
// failure, which is different from what you'd expect from a Mojo API, but it
// makes for a more convenient embedder API.
MOJO_SYSTEM_IMPL_EXPORT MojoResult
CreatePlatformHandleWrapper(ScopedPlatformHandle platform_handle,
                            MojoHandle* platform_handle_wrapper_handle);

// Retrieves the |PlatformHandle| that was wrapped into a |MojoHandle| (using
// |CreatePlatformHandleWrapper()| above). Note that the |MojoHandle| must still
// be closed separately.
MOJO_SYSTEM_IMPL_EXPORT MojoResult
PassWrappedPlatformHandle(MojoHandle platform_handle_wrapper_handle,
                          ScopedPlatformHandle* platform_handle);

// Initialialization/shutdown for interprocess communication (IPC) -------------

// |InitIPCSupport()| sets up the subsystem for interprocess communication,
// making the IPC functions (in the following section) available and functional.
// (This may only be done after |Init()|.)
//
// This subsystem may be shut down, using |ShutdownIPCSupportOnIOThread()| or
// |ShutdownIPCSupport()|. None of the IPC functions may be called while or
// after either of these is called.

// Initializes a process of the given type; to be called after |Init()|.
//   - |process_delegate| must be a process delegate of the appropriate type
//     corresponding to |process_type|; its methods will be called on the same
//     thread as Shutdown.
//   - |process_delegate|, and |io_thread_task_runner| should live at least
//     until |ShutdownIPCSupport()|'s callback has been run or
//     |ShutdownIPCSupportOnIOThread()| has completed.
MOJO_SYSTEM_IMPL_EXPORT void InitIPCSupport(
    ProcessDelegate* process_delegate,
    scoped_refptr<base::TaskRunner> io_thread_task_runner);

// Shuts down the subsystem initialized by |InitIPCSupport()|. This must be
// called on the I/O thread (given to |InitIPCSupport()|). This completes
// synchronously and does not result in a call to the process delegate's
// |OnShutdownComplete()|.
MOJO_SYSTEM_IMPL_EXPORT void ShutdownIPCSupportOnIOThread();

// Like |ShutdownIPCSupportOnIOThread()|, but may be called from any thread,
// signalling shutdown completion via the process delegate's
// |OnShutdownComplete()|.
MOJO_SYSTEM_IMPL_EXPORT void ShutdownIPCSupport();

// Like above, but doesn't call |OnShutdownComplete| until all channels are
// gone.
// TODO(jam): this should be the default behavior.
MOJO_SYSTEM_IMPL_EXPORT void ShutdownIPCSupportAndWaitForNoChannels();

// Creates a message pipe from a platform handle. Safe to call from any thread.
MOJO_SYSTEM_IMPL_EXPORT ScopedMessagePipeHandle
CreateMessagePipe(ScopedPlatformHandle platform_handle);

}  // namespace edk
}  // namespace mojo

#endif  // MOJO_EDK_EMBEDDER_EMBEDDER_H_
