// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include "ServiceWorkerRegistration.h"

#include "bindings/core/v8/CallbackPromiseAdapter.h"
#include "bindings/core/v8/ScriptPromise.h"
#include "bindings/core/v8/ScriptState.h"
#include "core/dom/DOMException.h"
#include "core/dom/ExceptionCode.h"
#include "core/dom/ExecutionContext.h"
#include "core/events/Event.h"
#include "modules/EventTargetModules.h"
#include "modules/serviceworkers/ServiceWorkerContainerClient.h"
#include "modules/serviceworkers/ServiceWorkerError.h"
#include "public/platform/modules/serviceworker/WebServiceWorkerProvider.h"

namespace blink {

const AtomicString& ServiceWorkerRegistration::interfaceName() const
{
    return EventTargetNames::ServiceWorkerRegistration;
}

void ServiceWorkerRegistration::dispatchUpdateFoundEvent()
{
    dispatchEvent(Event::create(EventTypeNames::updatefound));
}

void ServiceWorkerRegistration::setInstalling(WebPassOwnPtr<WebServiceWorker::Handle> handle)
{
    if (!executionContext())
        return;
    m_installing = ServiceWorker::from(executionContext(), handle.release());
}

void ServiceWorkerRegistration::setWaiting(WebPassOwnPtr<WebServiceWorker::Handle> handle)
{
    if (!executionContext())
        return;
    m_waiting = ServiceWorker::from(executionContext(), handle.release());
}

void ServiceWorkerRegistration::setActive(WebPassOwnPtr<WebServiceWorker::Handle> handle)
{
    if (!executionContext())
        return;
    m_active = ServiceWorker::from(executionContext(), handle.release());
}

ServiceWorkerRegistration* ServiceWorkerRegistration::getOrCreate(ExecutionContext* executionContext, PassOwnPtr<WebServiceWorkerRegistration::Handle> handle)
{
    ASSERT(handle);

    ServiceWorkerRegistration* existingRegistration = static_cast<ServiceWorkerRegistration*>(handle->registration()->proxy());
    if (existingRegistration) {
        ASSERT(existingRegistration->executionContext() == executionContext);
        return existingRegistration;
    }

    ServiceWorkerRegistration* newRegistration = new ServiceWorkerRegistration(executionContext, handle);
    newRegistration->suspendIfNeeded();
    return newRegistration;
}

String ServiceWorkerRegistration::scope() const
{
    return m_handle->registration()->scope().string();
}

ScriptPromise ServiceWorkerRegistration::update(ScriptState* scriptState)
{
    ScriptPromiseResolver* resolver = ScriptPromiseResolver::create(scriptState);
    ScriptPromise promise = resolver->promise();

    if (!m_provider) {
        resolver->reject(DOMException::create(InvalidStateError, "Failed to update a ServiceWorkerRegistration: No associated provider is available."));
        return promise;
    }

    m_handle->registration()->update(m_provider, new CallbackPromiseAdapter<void, ServiceWorkerError>(resolver));
    return promise;
}

ScriptPromise ServiceWorkerRegistration::unregister(ScriptState* scriptState)
{
    ScriptPromiseResolver* resolver = ScriptPromiseResolver::create(scriptState);
    ScriptPromise promise = resolver->promise();

    if (!m_provider) {
        resolver->reject(DOMException::create(InvalidStateError, "Failed to unregister a ServiceWorkerRegistration: No associated provider is available."));
        return promise;
    }

    m_handle->registration()->unregister(m_provider, new CallbackPromiseAdapter<bool, ServiceWorkerError>(resolver));
    return promise;
}

ServiceWorkerRegistration::ServiceWorkerRegistration(ExecutionContext* executionContext, PassOwnPtr<WebServiceWorkerRegistration::Handle> handle)
    : ActiveDOMObject(executionContext)
    , m_handle(handle)
    , m_provider(nullptr)
    , m_stopped(false)
{
    ASSERT(m_handle);
    ASSERT(!m_handle->registration()->proxy());

    if (!executionContext)
        return;
    if (ServiceWorkerContainerClient* client = ServiceWorkerContainerClient::from(executionContext))
        m_provider = client->provider();
    m_handle->registration()->setProxy(this);
}

ServiceWorkerRegistration::~ServiceWorkerRegistration()
{
}

DEFINE_TRACE(ServiceWorkerRegistration)
{
    visitor->trace(m_installing);
    visitor->trace(m_waiting);
    visitor->trace(m_active);
    RefCountedGarbageCollectedEventTargetWithInlineData<ServiceWorkerRegistration>::trace(visitor);
    HeapSupplementable<ServiceWorkerRegistration>::trace(visitor);
    ActiveDOMObject::trace(visitor);
}

bool ServiceWorkerRegistration::hasPendingActivity() const
{
    return !m_stopped;
}

void ServiceWorkerRegistration::stop()
{
    if (m_stopped)
        return;
    m_stopped = true;
    m_handle->registration()->proxyStopped();
}

} // namespace blink
