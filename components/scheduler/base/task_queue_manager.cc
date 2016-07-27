// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/scheduler/base/task_queue_manager.h"

#include <queue>
#include <set>

#include "base/bind.h"
#include "components/scheduler/base/real_time_domain.h"
#include "components/scheduler/base/task_queue_impl.h"
#include "components/scheduler/base/task_queue_manager_delegate.h"
#include "components/scheduler/base/task_queue_selector.h"
#include "components/scheduler/base/task_queue_sets.h"

namespace scheduler {

TaskQueueManager::TaskQueueManager(
    scoped_refptr<TaskQueueManagerDelegate> delegate,
    const char* tracing_category,
    const char* disabled_by_default_tracing_category,
    const char* disabled_by_default_verbose_tracing_category)
    : real_time_domain_(new RealTimeDomain()),
      delegate_(delegate),
      task_was_run_on_quiescence_monitored_queue_(false),
      pending_dowork_count_(0),
      work_batch_size_(1),
      tracing_category_(tracing_category),
      disabled_by_default_tracing_category_(
          disabled_by_default_tracing_category),
      disabled_by_default_verbose_tracing_category_(
          disabled_by_default_verbose_tracing_category),
      observer_(nullptr),
      deletion_sentinel_(new DeletionSentinel()),
      weak_factory_(this) {
  DCHECK(delegate->RunsTasksOnCurrentThread());
  TRACE_EVENT_OBJECT_CREATED_WITH_ID(disabled_by_default_tracing_category,
                                     "TaskQueueManager", this);
  selector_.SetTaskQueueSelectorObserver(this);

  decrement_pending_and_do_work_closure_ =
      base::Bind(&TaskQueueManager::DoWork, weak_factory_.GetWeakPtr(), true);
  do_work_closure_ =
      base::Bind(&TaskQueueManager::DoWork, weak_factory_.GetWeakPtr(), false);

  // TODO(alexclarke): Change this to be a parameter that's passed in.
  RegisterTimeDomain(real_time_domain_.get());
}

TaskQueueManager::~TaskQueueManager() {
  TRACE_EVENT_OBJECT_DELETED_WITH_ID(disabled_by_default_tracing_category_,
                                     "TaskQueueManager", this);

  while (!queues_.empty())
    (*queues_.begin())->UnregisterTaskQueue();

  selector_.SetTaskQueueSelectorObserver(nullptr);
}

void TaskQueueManager::RegisterTimeDomain(TimeDomain* time_domain) {
  time_domains_.insert(time_domain);
  time_domain->OnRegisterWithTaskQueueManager(delegate_.get(),
                                              do_work_closure_);
}

void TaskQueueManager::UnregisterTimeDomain(TimeDomain* time_domain) {
  time_domains_.erase(time_domain);
}

scoped_refptr<internal::TaskQueueImpl> TaskQueueManager::NewTaskQueue(
    const TaskQueue::Spec& spec) {
  TRACE_EVENT1(tracing_category_,
               "TaskQueueManager::NewTaskQueue", "queue_name", spec.name);
  DCHECK(main_thread_checker_.CalledOnValidThread());
  TimeDomain* time_domain =
      spec.time_domain ? spec.time_domain : real_time_domain_.get();
  DCHECK(time_domains_.find(time_domain) != time_domains_.end());
  scoped_refptr<internal::TaskQueueImpl> queue(
      make_scoped_refptr(new internal::TaskQueueImpl(
          this, time_domain, spec, disabled_by_default_tracing_category_,
          disabled_by_default_verbose_tracing_category_)));
  queues_.insert(queue);
  selector_.AddQueue(queue.get());
  return queue;
}

void TaskQueueManager::SetObserver(Observer* observer) {
  DCHECK(main_thread_checker_.CalledOnValidThread());
  observer_ = observer;
}

void TaskQueueManager::UnregisterTaskQueue(
    scoped_refptr<internal::TaskQueueImpl> task_queue) {
  TRACE_EVENT1(tracing_category_,
               "TaskQueueManager::UnregisterTaskQueue", "queue_name",
               task_queue->GetName());
  DCHECK(main_thread_checker_.CalledOnValidThread());
  if (observer_)
    observer_->OnUnregisterTaskQueue(task_queue);

  // Add |task_queue| to |queues_to_delete_| so we can prevent it from being
  // freed while any of our structures hold hold a raw pointer to it.
  queues_to_delete_.insert(task_queue);
  queues_.erase(task_queue);
  selector_.RemoveQueue(task_queue.get());
}

void TaskQueueManager::UpdateWorkQueues(
    bool should_trigger_wakeup,
    const internal::TaskQueueImpl::Task* previous_task) {
  TRACE_EVENT0(disabled_by_default_tracing_category_,
               "TaskQueueManager::UpdateWorkQueues");

  for (TimeDomain* time_domain : time_domains_) {
    time_domain->UpdateWorkQueues(should_trigger_wakeup, previous_task);
  }
}

void TaskQueueManager::MaybePostDoWorkOnMainRunner() {
  bool on_main_thread = delegate_->BelongsToCurrentThread();
  if (on_main_thread) {
    // We only want one pending DoWork posted from the main thread, or we risk
    // an explosion of pending DoWorks which could starve out everything else.
    if (pending_dowork_count_ > 0) {
      return;
    }
    pending_dowork_count_++;
    delegate_->PostTask(FROM_HERE, decrement_pending_and_do_work_closure_);
  } else {
    delegate_->PostTask(FROM_HERE, do_work_closure_);
  }
}

void TaskQueueManager::DoWork(bool decrement_pending_dowork_count) {
  if (decrement_pending_dowork_count) {
    pending_dowork_count_--;
    DCHECK_GE(pending_dowork_count_, 0);
  }
  DCHECK(main_thread_checker_.CalledOnValidThread());

  if (!delegate_->IsNested())
    queues_to_delete_.clear();

  // Pass false and nullptr to UpdateWorkQueues here to prevent waking up a
  // pump-after-wakeup queue.
  UpdateWorkQueues(false, nullptr);

  internal::TaskQueueImpl::Task previous_task;
  for (int i = 0; i < work_batch_size_; i++) {
    internal::TaskQueueImpl* queue;
    if (!SelectQueueToService(&queue))
      break;

    switch (ProcessTaskFromWorkQueue(queue, &previous_task)) {
      case ProcessTaskResult::DEFERRED:
        // If a task was deferred, try again with another task. Note that this
        // means deferred tasks (i.e. non-nestable tasks) will never trigger
        // queue wake-ups.
        continue;
      case ProcessTaskResult::EXECUTED:
        break;
      case ProcessTaskResult::TASK_QUEUE_MANAGER_DELETED:
        return;  // The TaskQueueManager got deleted, we must bail out.
    }
    bool should_trigger_wakeup = queue->wakeup_policy() ==
                                 TaskQueue::WakeupPolicy::CAN_WAKE_OTHER_QUEUES;
    UpdateWorkQueues(should_trigger_wakeup, &previous_task);

    // Only run a single task per batch in nested run loops so that we can
    // properly exit the nested loop when someone calls RunLoop::Quit().
    if (delegate_->IsNested())
      break;
  }

  // TODO(alexclarke): Consider refactoring the above loop to terminate only
  // when there's no more work left to be done, rather than posting a
  // continuation task.
  if (!selector_.EnabledWorkQueuesEmpty() || TryAdvanceTimeDomains()) {
    MaybePostDoWorkOnMainRunner();
  } else {
    // Tell the task runner we have no more work.
    delegate_->OnNoMoreImmediateWork();
  }
}

bool TaskQueueManager::TryAdvanceTimeDomains() {
  bool can_advance = false;
  for (TimeDomain* time_domain : time_domains_) {
    can_advance |= time_domain->MaybeAdvanceTime();
  }
  return can_advance;
}

bool TaskQueueManager::SelectQueueToService(
    internal::TaskQueueImpl** out_queue) {
  bool should_run = selector_.SelectQueueToService(out_queue);
  TRACE_EVENT_OBJECT_SNAPSHOT_WITH_ID(
      disabled_by_default_tracing_category_, "TaskQueueManager", this,
      AsValueWithSelectorResult(should_run, *out_queue));
  return should_run;
}

void TaskQueueManager::DidQueueTask(
    const internal::TaskQueueImpl::Task& pending_task) {
  task_annotator_.DidQueueTask("TaskQueueManager::PostTask", pending_task);
}

TaskQueueManager::ProcessTaskResult TaskQueueManager::ProcessTaskFromWorkQueue(
    internal::TaskQueueImpl* queue,
    internal::TaskQueueImpl::Task* out_previous_task) {
  DCHECK(main_thread_checker_.CalledOnValidThread());
  scoped_refptr<DeletionSentinel> protect(deletion_sentinel_);
  // TODO(alexclarke): consider std::move() when allowed.
  internal::TaskQueueImpl::Task pending_task = queue->TakeTaskFromWorkQueue();

  if (queue->GetQuiescenceMonitored())
    task_was_run_on_quiescence_monitored_queue_ = true;

  if (!pending_task.nestable && delegate_->IsNested()) {
    // Defer non-nestable work to the main task runner.  NOTE these tasks can be
    // arbitrarily delayed so the additional delay should not be a problem.
    // TODO(skyostil): Figure out a way to not forget which task queue the
    // task is associated with. See http://crbug.com/522843.
    delegate_->PostNonNestableTask(pending_task.posted_from, pending_task.task);
    return ProcessTaskResult::DEFERRED;
  }

  TRACE_TASK_EXECUTION("TaskQueueManager::ProcessTaskFromWorkQueue",
                       pending_task);
  if (queue->GetShouldNotifyObservers()) {
    FOR_EACH_OBSERVER(base::MessageLoop::TaskObserver, task_observers_,
                      WillProcessTask(pending_task));
    queue->NotifyWillProcessTask(pending_task);
  }
  TRACE_EVENT1(tracing_category_,
               "TaskQueueManager::RunTask", "queue", queue->GetName());
  task_annotator_.RunTask("TaskQueueManager::PostTask", pending_task);

  // Detect if the TaskQueueManager just got deleted.  If this happens we must
  // not access any member variables after this point.
  if (protect->HasOneRef())
    return ProcessTaskResult::TASK_QUEUE_MANAGER_DELETED;

  if (queue->GetShouldNotifyObservers()) {
    FOR_EACH_OBSERVER(base::MessageLoop::TaskObserver, task_observers_,
                      DidProcessTask(pending_task));
    queue->NotifyDidProcessTask(pending_task);
  }

  pending_task.task.Reset();
  *out_previous_task = pending_task;
  return ProcessTaskResult::EXECUTED;
}

bool TaskQueueManager::RunsTasksOnCurrentThread() const {
  return delegate_->RunsTasksOnCurrentThread();
}

void TaskQueueManager::SetWorkBatchSize(int work_batch_size) {
  DCHECK(main_thread_checker_.CalledOnValidThread());
  DCHECK_GE(work_batch_size, 1);
  work_batch_size_ = work_batch_size;
}

void TaskQueueManager::AddTaskObserver(
    base::MessageLoop::TaskObserver* task_observer) {
  DCHECK(main_thread_checker_.CalledOnValidThread());
  task_observers_.AddObserver(task_observer);
}

void TaskQueueManager::RemoveTaskObserver(
    base::MessageLoop::TaskObserver* task_observer) {
  DCHECK(main_thread_checker_.CalledOnValidThread());
  task_observers_.RemoveObserver(task_observer);
}

bool TaskQueueManager::GetAndClearSystemIsQuiescentBit() {
  bool task_was_run = task_was_run_on_quiescence_monitored_queue_;
  task_was_run_on_quiescence_monitored_queue_ = false;
  return !task_was_run;
}

const scoped_refptr<TaskQueueManagerDelegate>& TaskQueueManager::delegate()
    const {
  return delegate_;
}

int TaskQueueManager::GetNextSequenceNumber() {
  return task_sequence_num_.GetNext();
}

scoped_refptr<base::trace_event::ConvertableToTraceFormat>
TaskQueueManager::AsValueWithSelectorResult(
    bool should_run,
    internal::TaskQueueImpl* selected_queue) const {
  DCHECK(main_thread_checker_.CalledOnValidThread());
  scoped_refptr<base::trace_event::TracedValue> state =
      new base::trace_event::TracedValue();
  state->BeginArray("queues");
  for (auto& queue : queues_)
    queue->AsValueInto(state.get());
  state->EndArray();
  state->BeginDictionary("selector");
  selector_.AsValueInto(state.get());
  state->EndDictionary();
  if (should_run)
    state->SetString("selected_queue", selected_queue->GetName());

  state->BeginArray("time_domains");
  for (auto& time_domain : time_domains_)
    time_domain->AsValueInto(state.get());
  state->EndArray();
  return state;
}

void TaskQueueManager::OnTaskQueueEnabled(internal::TaskQueueImpl* queue) {
  DCHECK(main_thread_checker_.CalledOnValidThread());
  // Only schedule DoWork if there's something to do.
  if (!queue->work_queue().empty())
    MaybePostDoWorkOnMainRunner();
}

}  // namespace scheduler
