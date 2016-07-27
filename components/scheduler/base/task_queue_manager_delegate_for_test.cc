// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/scheduler/base/task_queue_manager_delegate_for_test.h"

#include "base/bind.h"
#include "base/bind_helpers.h"

namespace scheduler {

// static
scoped_refptr<TaskQueueManagerDelegateForTest>
TaskQueueManagerDelegateForTest::Create(
    scoped_refptr<base::SingleThreadTaskRunner> task_runner,
    scoped_ptr<base::TickClock> time_source) {
  return make_scoped_refptr(
      new TaskQueueManagerDelegateForTest(task_runner, time_source.Pass()));
}

TaskQueueManagerDelegateForTest::TaskQueueManagerDelegateForTest(
    scoped_refptr<base::SingleThreadTaskRunner> task_runner,
    scoped_ptr<base::TickClock> time_source)
    : task_runner_(task_runner), time_source_(time_source.Pass()) {}

TaskQueueManagerDelegateForTest::~TaskQueueManagerDelegateForTest() {}

bool TaskQueueManagerDelegateForTest::PostDelayedTask(
    const tracked_objects::Location& from_here,
    const base::Closure& task,
    base::TimeDelta delay) {
  return task_runner_->PostDelayedTask(from_here, task, delay);
}

bool TaskQueueManagerDelegateForTest::PostNonNestableDelayedTask(
    const tracked_objects::Location& from_here,
    const base::Closure& task,
    base::TimeDelta delay) {
  return task_runner_->PostNonNestableDelayedTask(from_here, task, delay);
}

bool TaskQueueManagerDelegateForTest::RunsTasksOnCurrentThread() const {
  return task_runner_->RunsTasksOnCurrentThread();
}

bool TaskQueueManagerDelegateForTest::IsNested() const {
  return false;
}

base::TimeTicks TaskQueueManagerDelegateForTest::NowTicks() {
  return time_source_->NowTicks();
}

double TaskQueueManagerDelegateForTest::CurrentTimeSeconds() const {
  return (time_source_->NowTicks() - base::TimeTicks::UnixEpoch()).InSecondsF();
}

void TaskQueueManagerDelegateForTest::OnNoMoreImmediateWork() {}

}  // namespace scheduler
