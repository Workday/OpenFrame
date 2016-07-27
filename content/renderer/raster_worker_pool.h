// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_RASTER_WORKER_POOL_H_
#define CONTENT_RENDERER_RASTER_WORKER_POOL_H_

#include "base/callback.h"
#include "base/containers/hash_tables.h"
#include "base/memory/scoped_vector.h"
#include "base/sequenced_task_runner.h"
#include "base/synchronization/condition_variable.h"
#include "base/task_runner.h"
#include "base/threading/simple_thread.h"
#include "cc/raster/task_graph_runner.h"
#include "cc/raster/task_graph_work_queue.h"
#include "content/common/content_export.h"

namespace content {

// A pool of threads used to run raster work.
// Work can be scheduled on the threads using different interfaces.
// The pool itself implements TaskRunner interface and tasks posted via that
// interface might run in parallel.
// CreateSequencedTaskRunner creates a sequenced task runner that might run in
// parallel with other instances of sequenced task runners.
// It's also possible to get the underlying TaskGraphRunner to schedule a graph
// of tasks with their dependencies.
class CONTENT_EXPORT RasterWorkerPool
    : public base::TaskRunner,
      public cc::TaskGraphRunner,
      public base::DelegateSimpleThread::Delegate {
 public:
  RasterWorkerPool();

  // Overridden from base::TaskRunner:
  bool PostDelayedTask(const tracked_objects::Location& from_here,
                       const base::Closure& task,
                       base::TimeDelta delay) override;
  bool RunsTasksOnCurrentThread() const override;

  // Overridden from cc::TaskGraphRunner:
  cc::NamespaceToken GetNamespaceToken() override;
  void ScheduleTasks(cc::NamespaceToken token, cc::TaskGraph* graph) override;
  void WaitForTasksToFinishRunning(cc::NamespaceToken token) override;
  void CollectCompletedTasks(cc::NamespaceToken token,
                             cc::Task::Vector* completed_tasks) override;

  // Overridden from base::DelegateSimpleThread::Delegate:
  void Run() override;

  void FlushForTesting();

  // Spawn |num_threads| number of threads and start running work on the
  // worker threads.
  void Start(int num_threads,
             const base::SimpleThread::Options& thread_options);

  // Finish running all the posted tasks (and nested task posted by those tasks)
  // of all the associated task runners.
  // Once all the tasks are executed the method blocks until the threads are
  // terminated.
  void Shutdown();

  cc::TaskGraphRunner* GetTaskGraphRunner() { return this; }

  // Create a new sequenced task graph runner.
  scoped_refptr<base::SequencedTaskRunner> CreateSequencedTaskRunner();

 protected:
  ~RasterWorkerPool() override;

 private:
  class RasterWorkerPoolSequencedTaskRunner;
  friend class RasterWorkerPoolSequencedTaskRunner;

  // Run next task. Caller must acquire |lock_| prior to calling this function
  // and make sure at least one task is ready to run.
  void RunTaskWithLockAcquired();

  // Simple Task for the TaskGraphRunner that wraps a closure.
  // This class is used to schedule TaskRunner tasks on the
  // |task_graph_runner_|.
  class ClosureTask : public cc::Task {
   public:
    explicit ClosureTask(const base::Closure& closure);

    // Overridden from cc::Task:
    void RunOnWorkerThread() override;

   protected:
    ~ClosureTask() override;

   private:
    base::Closure closure_;

    DISALLOW_COPY_AND_ASSIGN(ClosureTask);
  };

  void ScheduleTasksWithLockAcquired(cc::NamespaceToken token,
                                     cc::TaskGraph* graph);
  void CollectCompletedTasksWithLockAcquired(cc::NamespaceToken token,
                                             cc::Task::Vector* completed_tasks);

  // The actual threads where work is done.
  ScopedVector<base::DelegateSimpleThread> threads_;

  // Lock to exclusively access all the following members that are used to
  // implement the TaskRunner and TaskGraphRunner interfaces.
  base::Lock lock_;
  // Stores the tasks to be run, sorted by priority.
  cc::TaskGraphWorkQueue work_queue_;
  // Namespace used to schedule tasks in the task graph runner.
  cc::NamespaceToken namespace_token_;
  // List of tasks currently queued up for execution.
  cc::Task::Vector tasks_;
  // Graph object used for scheduling tasks.
  cc::TaskGraph graph_;
  // Cached vector to avoid allocation when getting the list of complete
  // tasks.
  cc::Task::Vector completed_tasks_;
  // Condition variable that is waited on by Run() until new tasks are ready to
  // run or shutdown starts.
  base::ConditionVariable has_ready_to_run_tasks_cv_;
  // Condition variable that is waited on by origin threads until a namespace
  // has finished running all associated tasks.
  base::ConditionVariable has_namespaces_with_finished_running_tasks_cv_;
  // Set during shutdown. Tells Run() to return when no more tasks are pending.
  bool shutdown_;
};

}  // namespace content

#endif  // CONTENT_RENDERER_RASTER_WORKER_POOL_H_
