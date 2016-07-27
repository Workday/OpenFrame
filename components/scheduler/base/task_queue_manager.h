// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_SCHEDULER_BASE_TASK_QUEUE_MANAGER_H_
#define CONTENT_RENDERER_SCHEDULER_BASE_TASK_QUEUE_MANAGER_H_

#include <map>

#include "base/atomic_sequence_num.h"
#include "base/debug/task_annotator.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/pending_task.h"
#include "base/synchronization/lock.h"
#include "base/threading/thread_checker.h"
#include "components/scheduler/base/task_queue_impl.h"
#include "components/scheduler/base/task_queue_selector.h"
#include "components/scheduler/scheduler_export.h"

namespace base {
class TickClock;

namespace trace_event {
class ConvertableToTraceFormat;
class TracedValue;
}  // namespace trace_event
}  // namespace base

namespace scheduler {
namespace internal {
class TaskQueueImpl;
}  // namespace internal

class LazyNow;
class RealTimeDomain;
class TimeDomain;
class TaskQueueManagerDelegate;

// The task queue manager provides N task queues and a selector interface for
// choosing which task queue to service next. Each task queue consists of two
// sub queues:
//
// 1. Incoming task queue. Tasks that are posted get immediately appended here.
//    When a task is appended into an empty incoming queue, the task manager
//    work function (DoWork) is scheduled to run on the main task runner.
//
// 2. Work queue. If a work queue is empty when DoWork() is entered, tasks from
//    the incoming task queue (if any) are moved here. The work queues are
//    registered with the selector as input to the scheduling decision.
//
class SCHEDULER_EXPORT TaskQueueManager
    : public internal::TaskQueueSelector::Observer {
 public:
  // Create a task queue manager where |delegate| identifies the thread
  // on which where the tasks are  eventually run. Category strings must have
  // application lifetime (statics or literals). They may not include " chars.
  TaskQueueManager(scoped_refptr<TaskQueueManagerDelegate> delegate,
                   const char* tracing_category,
                   const char* disabled_by_default_tracing_category,
                   const char* disabled_by_default_verbose_tracing_category);
  ~TaskQueueManager() override;

  // Set the number of tasks executed in a single invocation of the task queue
  // manager. Increasing the batch size can reduce the overhead of yielding
  // back to the main message loop -- at the cost of potentially delaying other
  // tasks posted to the main loop. The batch size is 1 by default.
  void SetWorkBatchSize(int work_batch_size);

  // These functions can only be called on the same thread that the task queue
  // manager executes its tasks on.
  void AddTaskObserver(base::MessageLoop::TaskObserver* task_observer);
  void RemoveTaskObserver(base::MessageLoop::TaskObserver* task_observer);

  // Returns true if any task from a monitored task queue was was run since the
  // last call to GetAndClearSystemIsQuiescentBit.
  bool GetAndClearSystemIsQuiescentBit();

  // Creates a task queue with the given |spec|.  Must be called on the thread
  // this class was created on.
  scoped_refptr<internal::TaskQueueImpl> NewTaskQueue(
      const TaskQueue::Spec& spec);

  class SCHEDULER_EXPORT Observer {
   public:
    virtual ~Observer() {}

    // Called when |queue| is unregistered.
    virtual void OnUnregisterTaskQueue(
        const scoped_refptr<internal::TaskQueueImpl>& queue) = 0;
  };

  // Called once to set the Observer. This function is called on the main
  // thread. If |observer| is null, then no callbacks will occur.
  // Note |observer| is expected to outlive the SchedulerHelper.
  void SetObserver(Observer* observer);

  // Returns the delegate used by the TaskQueueManager.
  const scoped_refptr<TaskQueueManagerDelegate>& delegate() const;

  // Time domains must be registered for the task queues to get updated.
  void RegisterTimeDomain(TimeDomain* time_domain);
  void UnregisterTimeDomain(TimeDomain* time_domain);

  RealTimeDomain* real_time_domain() const { return real_time_domain_.get(); }

 private:
  friend class LazyNow;
  friend class internal::TaskQueueImpl;
  friend class TaskQueueManagerTest;

  class DeletionSentinel : public base::RefCounted<DeletionSentinel> {
   private:
    friend class base::RefCounted<DeletionSentinel>;
    ~DeletionSentinel() {}
  };

  // Unregisters a TaskQueue previously created by |NewTaskQueue()|.
  // NOTE we have to flush the queue from |newly_updatable_| which means as a
  // side effect MoveNewlyUpdatableQueuesIntoUpdatableQueueSet is called by this
  // function.
  void UnregisterTaskQueue(scoped_refptr<internal::TaskQueueImpl> task_queue);

  // TaskQueueSelector::Observer implementation:
  void OnTaskQueueEnabled(internal::TaskQueueImpl* queue) override;

  // Called by the task queue to register a new pending task.
  void DidQueueTask(const internal::TaskQueueImpl::Task& pending_task);

  // Post a task to call DoWork() on the main task runner.  Only one pending
  // DoWork is allowed from the main thread, to prevent an explosion of pending
  // DoWorks.
  void MaybePostDoWorkOnMainRunner();

  // Use the selector to choose a pending task and run it.
  void DoWork(bool decrement_pending_dowork_count);

  // Delayed Tasks with run_times <= Now() are enqueued onto the work queue.
  // Reloads any empty work queues which have automatic pumping enabled and
  // which are eligible to be auto pumped based on the |previous_task| which was
  // run and |should_trigger_wakeup|. Call with an empty |previous_task| if no
  // task was just run.
  void UpdateWorkQueues(bool should_trigger_wakeup,
                        const internal::TaskQueueImpl::Task* previous_task);

  // Chooses the next work queue to service. Returns true if |out_queue|
  // indicates the queue from which the next task should be run, false to
  // avoid running any tasks.
  bool SelectQueueToService(internal::TaskQueueImpl** out_queue);

  // Runs a single nestable task from the |queue|. On exit, |out_task| will
  // contain the task which was executed. Non-nestable task are reposted on the
  // run loop. The queue must not be empty.
  enum class ProcessTaskResult {
    DEFERRED,
    EXECUTED,
    TASK_QUEUE_MANAGER_DELETED
  };
  ProcessTaskResult ProcessTaskFromWorkQueue(
      internal::TaskQueueImpl* queue,
      internal::TaskQueueImpl::Task* out_previous_task);

  bool RunsTasksOnCurrentThread() const;
  bool PostNonNestableDelayedTask(const tracked_objects::Location& from_here,
                                  const base::Closure& task,
                                  base::TimeDelta delay);

  int GetNextSequenceNumber();

  bool TryAdvanceTimeDomains();

  scoped_refptr<base::trace_event::ConvertableToTraceFormat>
  AsValueWithSelectorResult(bool should_run,
                            internal::TaskQueueImpl* selected_queue) const;

  std::set<TimeDomain*> time_domains_;
  scoped_ptr<RealTimeDomain> real_time_domain_;

  std::set<scoped_refptr<internal::TaskQueueImpl>> queues_;

  // We have to be careful when deleting a queue because some of the code uses
  // raw pointers and doesn't expect the rug to be pulled out from underneath.
  std::set<scoped_refptr<internal::TaskQueueImpl>> queues_to_delete_;


  base::AtomicSequenceNumber task_sequence_num_;
  base::debug::TaskAnnotator task_annotator_;

  base::ThreadChecker main_thread_checker_;
  scoped_refptr<TaskQueueManagerDelegate> delegate_;
  internal::TaskQueueSelector selector_;

  base::Closure decrement_pending_and_do_work_closure_;
  base::Closure do_work_closure_;

  bool task_was_run_on_quiescence_monitored_queue_;

  // The pending_dowork_count_ is only tracked on the main thread since that's
  // where re-entrant problems happen.
  int pending_dowork_count_;

  int work_batch_size_;

  base::ObserverList<base::MessageLoop::TaskObserver> task_observers_;

  const char* tracing_category_;
  const char* disabled_by_default_tracing_category_;
  const char* disabled_by_default_verbose_tracing_category_;

  Observer* observer_;  // NOT OWNED
  scoped_refptr<DeletionSentinel> deletion_sentinel_;
  base::WeakPtrFactory<TaskQueueManager> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(TaskQueueManager);
};

}  // namespace scheduler

#endif  // CONTENT_RENDERER_SCHEDULER_BASE_TASK_QUEUE_MANAGER_H_
