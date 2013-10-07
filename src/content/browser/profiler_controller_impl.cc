// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/profiler_controller_impl.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/tracked_objects.h"
#include "content/common/child_process_messages.h"
#include "content/public/browser/browser_child_process_host_iterator.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/child_process_data.h"
#include "content/public/browser/profiler_subscriber.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/common/content_switches.h"

namespace content {

ProfilerController* ProfilerController::GetInstance() {
  return ProfilerControllerImpl::GetInstance();
}

ProfilerControllerImpl* ProfilerControllerImpl::GetInstance() {
  return Singleton<ProfilerControllerImpl>::get();
}

ProfilerControllerImpl::ProfilerControllerImpl() : subscriber_(NULL) {
}

ProfilerControllerImpl::~ProfilerControllerImpl() {
}

void ProfilerControllerImpl::OnPendingProcesses(int sequence_number,
                                                int pending_processes,
                                                bool end) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (subscriber_)
    subscriber_->OnPendingProcesses(sequence_number, pending_processes, end);
}

void ProfilerControllerImpl::OnProfilerDataCollected(
    int sequence_number,
    const tracked_objects::ProcessDataSnapshot& profiler_data,
    int process_type) {
  if (!BrowserThread::CurrentlyOn(BrowserThread::UI)) {
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(&ProfilerControllerImpl::OnProfilerDataCollected,
                   base::Unretained(this),
                   sequence_number,
                   profiler_data,
                   process_type));
    return;
  }

  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (subscriber_) {
    subscriber_->OnProfilerDataCollected(sequence_number, profiler_data,
                                         process_type);
  }
}

void ProfilerControllerImpl::Register(ProfilerSubscriber* subscriber) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!subscriber_);
  subscriber_ = subscriber;
}

void ProfilerControllerImpl::Unregister(const ProfilerSubscriber* subscriber) {
  DCHECK_EQ(subscriber_, subscriber);
  subscriber_ = NULL;
}

void ProfilerControllerImpl::GetProfilerDataFromChildProcesses(
    int sequence_number) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  int pending_processes = 0;
  for (BrowserChildProcessHostIterator iter; !iter.Done(); ++iter) {
    // Skips requesting profiler data from the "GPU Process" if we are using in
    // process GPU. Those stats should be in the Browser-process's GPU thread.
    if (iter.GetData().process_type == PROCESS_TYPE_GPU &&
        CommandLine::ForCurrentProcess()->HasSwitch(switches::kInProcessGPU)) {
      continue;
    }

    ++pending_processes;
    if (!iter.Send(new ChildProcessMsg_GetChildProfilerData(sequence_number)))
      --pending_processes;
  }

  BrowserThread::PostTask(
      BrowserThread::UI,
      FROM_HERE,
      base::Bind(
          &ProfilerControllerImpl::OnPendingProcesses,
          base::Unretained(this),
          sequence_number,
          pending_processes,
          true));
}

void ProfilerControllerImpl::GetProfilerData(int sequence_number) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  int pending_processes = 0;
  for (RenderProcessHost::iterator it(RenderProcessHost::AllHostsIterator());
       !it.IsAtEnd(); it.Advance()) {
    ++pending_processes;
    if (!it.GetCurrentValue()->Send(
            new ChildProcessMsg_GetChildProfilerData(sequence_number))) {
      --pending_processes;
    }
  }
  OnPendingProcesses(sequence_number, pending_processes, false);

  BrowserThread::PostTask(
      BrowserThread::IO,
      FROM_HERE,
      base::Bind(&ProfilerControllerImpl::GetProfilerDataFromChildProcesses,
                 base::Unretained(this),
                 sequence_number));
}

}  // namespace content
