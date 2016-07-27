// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/raster/single_thread_task_graph_runner.h"

#include <string>

#include "base/threading/simple_thread.h"
#include "base/threading/thread_restrictions.h"
#include "base/trace_event/trace_event.h"

namespace cc {

SingleThreadTaskGraphRunner::SingleThreadTaskGraphRunner()
    : lock_(),
      has_ready_to_run_tasks_cv_(&lock_),
      has_namespaces_with_finished_running_tasks_cv_(&lock_),
      shutdown_(false) {}

SingleThreadTaskGraphRunner::~SingleThreadTaskGraphRunner() {}

void SingleThreadTaskGraphRunner::Start(
    const std::string& thread_name,
    const base::SimpleThread::Options& thread_options) {
  thread_.reset(
      new base::DelegateSimpleThread(this, thread_name, thread_options));
  thread_->Start();
}

void SingleThreadTaskGraphRunner::Shutdown() {
  {
    base::AutoLock lock(lock_);

    DCHECK(!work_queue_.HasReadyToRunTasks());
    DCHECK(!work_queue_.HasAnyNamespaces());

    DCHECK(!shutdown_);
    shutdown_ = true;

    // Wake up the worker so it knows it should exit.
    has_ready_to_run_tasks_cv_.Signal();
  }
  thread_->Join();
}

NamespaceToken SingleThreadTaskGraphRunner::GetNamespaceToken() {
  base::AutoLock lock(lock_);
  return work_queue_.GetNamespaceToken();
}

void SingleThreadTaskGraphRunner::ScheduleTasks(NamespaceToken token,
                                                TaskGraph* graph) {
  TRACE_EVENT2("cc", "SingleThreadTaskGraphRunner::ScheduleTasks", "num_nodes",
               graph->nodes.size(), "num_edges", graph->edges.size());

  DCHECK(token.IsValid());
  DCHECK(!TaskGraphWorkQueue::DependencyMismatch(graph));

  {
    base::AutoLock lock(lock_);

    DCHECK(!shutdown_);

    work_queue_.ScheduleTasks(token, graph);

    // If there is more work available, wake up the worker thread.
    if (work_queue_.HasReadyToRunTasks())
      has_ready_to_run_tasks_cv_.Signal();
  }
}

void SingleThreadTaskGraphRunner::WaitForTasksToFinishRunning(
    NamespaceToken token) {
  TRACE_EVENT0("cc",
               "SingleThreadTaskGraphRunner::WaitForTasksToFinishRunning");

  DCHECK(token.IsValid());

  {
    base::AutoLock lock(lock_);
    base::ThreadRestrictions::ScopedAllowWait allow_wait;

    auto* task_namespace = work_queue_.GetNamespaceForToken(token);

    if (!task_namespace)
      return;

    while (!work_queue_.HasFinishedRunningTasksInNamespace(task_namespace))
      has_namespaces_with_finished_running_tasks_cv_.Wait();

    // There may be other namespaces that have finished running tasks, so wake
    // up another origin thread.
    has_namespaces_with_finished_running_tasks_cv_.Signal();
  }
}

void SingleThreadTaskGraphRunner::CollectCompletedTasks(
    NamespaceToken token,
    Task::Vector* completed_tasks) {
  TRACE_EVENT0("cc", "SingleThreadTaskGraphRunner::CollectCompletedTasks");

  DCHECK(token.IsValid());

  {
    base::AutoLock lock(lock_);
    work_queue_.CollectCompletedTasks(token, completed_tasks);
  }
}

void SingleThreadTaskGraphRunner::Run() {
  base::AutoLock lock(lock_);

  while (true) {
    if (!work_queue_.HasReadyToRunTasks()) {
      // Exit when shutdown is set and no more tasks are pending.
      if (shutdown_)
        break;

      // Wait for more tasks.
      has_ready_to_run_tasks_cv_.Wait();
      continue;
    }

    RunTaskWithLockAcquired();
  }
}

void SingleThreadTaskGraphRunner::RunTaskWithLockAcquired() {
  TRACE_EVENT0("toplevel",
               "SingleThreadTaskGraphRunner::RunTaskWithLockAcquired");

  lock_.AssertAcquired();

  auto prioritized_task = work_queue_.GetNextTaskToRun();
  Task* task = prioritized_task.task;

  // Call WillRun() before releasing |lock_| and running task.
  task->WillRun();

  {
    base::AutoUnlock unlock(lock_);
    task->RunOnWorkerThread();
  }

  // This will mark task as finished running.
  task->DidRun();

  work_queue_.CompleteTask(prioritized_task);

  // If namespace has finished running all tasks, wake up origin thread.
  if (work_queue_.HasFinishedRunningTasksInNamespace(
          prioritized_task.task_namespace))
    has_namespaces_with_finished_running_tasks_cv_.Signal();
}

}  // namespace cc
