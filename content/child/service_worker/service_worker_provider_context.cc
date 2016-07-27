// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/child/service_worker/service_worker_provider_context.h"

#include "base/thread_task_runner_handle.h"
#include "content/child/child_thread_impl.h"
#include "content/child/service_worker/service_worker_dispatcher.h"
#include "content/child/service_worker/service_worker_handle_reference.h"
#include "content/child/service_worker/service_worker_registration_handle_reference.h"
#include "content/child/thread_safe_sender.h"
#include "content/child/worker_task_runner.h"

namespace content {

class ServiceWorkerProviderContext::Delegate {
 public:
  virtual ~Delegate(){};
  virtual void AssociateRegistration(
      scoped_ptr<ServiceWorkerRegistrationHandleReference> registration,
      scoped_ptr<ServiceWorkerHandleReference> installing,
      scoped_ptr<ServiceWorkerHandleReference> waiting,
      scoped_ptr<ServiceWorkerHandleReference> active) = 0;
  virtual void DisassociateRegistration() = 0;
  virtual void GetAssociatedRegistration(
      ServiceWorkerRegistrationObjectInfo* info,
      ServiceWorkerVersionAttributes* attrs) = 0;
  virtual void SetController(
      scoped_ptr<ServiceWorkerHandleReference> controller) = 0;
  virtual ServiceWorkerHandleReference* controller() = 0;
};

// Delegate class for ServiceWorker client (Document, SharedWorker, etc) to
// keep the associated registration and the controller until
// ServiceWorkerContainer is initialized.
class ServiceWorkerProviderContext::ControlleeDelegate
    : public ServiceWorkerProviderContext::Delegate {
 public:
  ControlleeDelegate() {}
  ~ControlleeDelegate() override {}

  void AssociateRegistration(
      scoped_ptr<ServiceWorkerRegistrationHandleReference> registration,
      scoped_ptr<ServiceWorkerHandleReference> installing,
      scoped_ptr<ServiceWorkerHandleReference> waiting,
      scoped_ptr<ServiceWorkerHandleReference> active) override {
    DCHECK(!registration_);
    registration_ = registration.Pass();
  }

  void DisassociateRegistration() override {
    controller_.reset();
    registration_.reset();
  }

  void SetController(
      scoped_ptr<ServiceWorkerHandleReference> controller) override {
    DCHECK(registration_);
    DCHECK(!controller ||
           controller->handle_id() != kInvalidServiceWorkerHandleId);
    controller_ = controller.Pass();
  }

  void GetAssociatedRegistration(
      ServiceWorkerRegistrationObjectInfo* info,
      ServiceWorkerVersionAttributes* attrs) override {
    NOTREACHED();
  }

  ServiceWorkerHandleReference* controller() override {
    return controller_.get();
  }

 private:
  scoped_ptr<ServiceWorkerRegistrationHandleReference> registration_;
  scoped_ptr<ServiceWorkerHandleReference> controller_;

  DISALLOW_COPY_AND_ASSIGN(ControlleeDelegate);
};

// Delegate class for ServiceWorkerGlobalScope to keep the associated
// registration and its versions until the execution context is initialized.
class ServiceWorkerProviderContext::ControllerDelegate
    : public ServiceWorkerProviderContext::Delegate {
 public:
  ControllerDelegate() {}
  ~ControllerDelegate() override {}

  void AssociateRegistration(
      scoped_ptr<ServiceWorkerRegistrationHandleReference> registration,
      scoped_ptr<ServiceWorkerHandleReference> installing,
      scoped_ptr<ServiceWorkerHandleReference> waiting,
      scoped_ptr<ServiceWorkerHandleReference> active) override {
    DCHECK(!registration_);
    registration_ = registration.Pass();
    installing_ = installing.Pass();
    waiting_ = waiting.Pass();
    active_ = active.Pass();
  }

  void DisassociateRegistration() override {
    // ServiceWorkerGlobalScope is never disassociated.
    NOTREACHED();
  }

  void SetController(
      scoped_ptr<ServiceWorkerHandleReference> controller) override {
    NOTREACHED();
  }

  void GetAssociatedRegistration(
      ServiceWorkerRegistrationObjectInfo* info,
      ServiceWorkerVersionAttributes* attrs) override {
    DCHECK(registration_);
    *info = registration_->info();
    if (installing_)
      attrs->installing = installing_->info();
    if (waiting_)
      attrs->waiting = waiting_->info();
    if (active_)
      attrs->active = active_->info();
  }

  ServiceWorkerHandleReference* controller() override {
    NOTREACHED();
    return nullptr;
  }

 private:
  scoped_ptr<ServiceWorkerRegistrationHandleReference> registration_;
  scoped_ptr<ServiceWorkerHandleReference> installing_;
  scoped_ptr<ServiceWorkerHandleReference> waiting_;
  scoped_ptr<ServiceWorkerHandleReference> active_;

  ServiceWorkerProviderContext* context_;

  DISALLOW_COPY_AND_ASSIGN(ControllerDelegate);
};

ServiceWorkerProviderContext::ServiceWorkerProviderContext(
    int provider_id,
    ServiceWorkerProviderType provider_type,
    ThreadSafeSender* thread_safe_sender)
    : provider_id_(provider_id),
      main_thread_task_runner_(base::ThreadTaskRunnerHandle::Get()),
      thread_safe_sender_(thread_safe_sender) {
  if (provider_type == SERVICE_WORKER_PROVIDER_FOR_CONTROLLER)
    delegate_.reset(new ControllerDelegate);
  else
    delegate_.reset(new ControlleeDelegate);

  ServiceWorkerDispatcher* dispatcher =
      ServiceWorkerDispatcher::GetOrCreateThreadSpecificInstance(
          thread_safe_sender_.get(), main_thread_task_runner_.get());
  dispatcher->AddProviderContext(this);
}

ServiceWorkerProviderContext::~ServiceWorkerProviderContext() {
  if (ServiceWorkerDispatcher* dispatcher =
          ServiceWorkerDispatcher::GetThreadSpecificInstance()) {
    // Remove this context from the dispatcher living on the main thread.
    dispatcher->RemoveProviderContext(this);
  }
}

void ServiceWorkerProviderContext::OnAssociateRegistration(
    scoped_ptr<ServiceWorkerRegistrationHandleReference> registration,
    scoped_ptr<ServiceWorkerHandleReference> installing,
    scoped_ptr<ServiceWorkerHandleReference> waiting,
    scoped_ptr<ServiceWorkerHandleReference> active) {
  DCHECK(main_thread_task_runner_->RunsTasksOnCurrentThread());
  delegate_->AssociateRegistration(registration.Pass(), installing.Pass(),
                                   waiting.Pass(), active.Pass());
}

void ServiceWorkerProviderContext::OnDisassociateRegistration() {
  DCHECK(main_thread_task_runner_->RunsTasksOnCurrentThread());
  delegate_->DisassociateRegistration();
}

void ServiceWorkerProviderContext::OnSetControllerServiceWorker(
    scoped_ptr<ServiceWorkerHandleReference> controller) {
  DCHECK(main_thread_task_runner_->RunsTasksOnCurrentThread());
  delegate_->SetController(controller.Pass());
}

void ServiceWorkerProviderContext::GetAssociatedRegistration(
    ServiceWorkerRegistrationObjectInfo* info,
    ServiceWorkerVersionAttributes* attrs) {
  DCHECK(!main_thread_task_runner_->RunsTasksOnCurrentThread());
  delegate_->GetAssociatedRegistration(info, attrs);
}

ServiceWorkerHandleReference* ServiceWorkerProviderContext::controller() {
  DCHECK(main_thread_task_runner_->RunsTasksOnCurrentThread());
  return delegate_->controller();
}

void ServiceWorkerProviderContext::DestructOnMainThread() const {
  if (!main_thread_task_runner_->RunsTasksOnCurrentThread() &&
      main_thread_task_runner_->DeleteSoon(FROM_HERE, this)) {
    return;
  }
  delete this;
}

}  // namespace content
