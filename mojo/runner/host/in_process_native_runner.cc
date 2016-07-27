// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/runner/host/in_process_native_runner.h"

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/location.h"
#include "base/task_runner.h"
#include "base/thread_task_runner_handle.h"
#include "base/threading/platform_thread.h"
#include "mojo/runner/host/native_application_support.h"
#include "mojo/runner/host/out_of_process_native_runner.h"
#include "mojo/runner/init.h"

namespace mojo {
namespace runner {

InProcessNativeRunner::InProcessNativeRunner() : app_library_(nullptr) {}

InProcessNativeRunner::~InProcessNativeRunner() {
  // It is important to let the thread exit before unloading the DSO (when
  // app_library_ is destructed), because the library may have registered
  // thread-local data and destructors to run on thread termination.
  if (thread_) {
    DCHECK(thread_->HasBeenStarted());
    DCHECK(!thread_->HasBeenJoined());
    thread_->Join();
  }
}

void InProcessNativeRunner::Start(
    const base::FilePath& app_path,
    bool start_sandboxed,
    InterfaceRequest<Application> application_request,
    const base::Closure& app_completed_callback) {
  app_path_ = app_path;

  DCHECK(!application_request_.is_pending());
  application_request_ = application_request.Pass();

  DCHECK(app_completed_callback_runner_.is_null());
  app_completed_callback_runner_ = base::Bind(
      &base::TaskRunner::PostTask, base::ThreadTaskRunnerHandle::Get(),
      FROM_HERE, app_completed_callback);

  DCHECK(!thread_);
  thread_.reset(new base::DelegateSimpleThread(this, "app_thread"));
  thread_->Start();
}

void InProcessNativeRunner::InitHost(
    ScopedHandle channel,
    InterfaceRequest<Application> application_request) {
  NOTREACHED();  // Can't host another process in this runner.
}

void InProcessNativeRunner::Run() {
  DVLOG(2) << "Loading/running Mojo app in process from library: "
           << app_path_.value()
           << " thread id=" << base::PlatformThread::CurrentId();

  // TODO(vtl): ScopedNativeLibrary doesn't have a .get() method!
  base::NativeLibrary app_library = LoadNativeApplication(app_path_);
  app_library_.Reset(app_library);
  CallLibraryEarlyInitialization(app_library);
  RunNativeApplication(app_library, application_request_.Pass());
  app_completed_callback_runner_.Run();
  app_completed_callback_runner_.Reset();
}

scoped_ptr<shell::NativeRunner> InProcessNativeRunnerFactory::Create(
    const base::FilePath& app_path) {
  // Non-Mojo apps are always run in a new process.
  if (!app_path.MatchesExtension(FILE_PATH_LITERAL(".mojo"))) {
    return make_scoped_ptr(
        new OutOfProcessNativeRunner(launch_process_runner_));
  }
  return make_scoped_ptr(new InProcessNativeRunner);
}

}  // namespace runner
}  // namespace mojo
