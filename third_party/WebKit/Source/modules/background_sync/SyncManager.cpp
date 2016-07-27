// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include "modules/background_sync/SyncManager.h"

#include "bindings/core/v8/CallbackPromiseAdapter.h"
#include "bindings/core/v8/ScriptPromise.h"
#include "bindings/core/v8/ScriptPromiseResolver.h"
#include "bindings/core/v8/ScriptState.h"
#include "core/dom/DOMException.h"
#include "core/dom/Document.h"
#include "core/dom/ExceptionCode.h"
#include "core/dom/ExecutionContext.h"
#include "modules/background_sync/SyncCallbacks.h"
#include "modules/background_sync/SyncRegistrationOptions.h"
#include "modules/serviceworkers/ServiceWorkerRegistration.h"
#include "public/platform/Platform.h"
#include "public/platform/modules/background_sync/WebSyncProvider.h"
#include "public/platform/modules/background_sync/WebSyncRegistration.h"
#include "wtf/RefPtr.h"


namespace blink {
namespace {

WebSyncProvider* backgroundSyncProvider()
{
    WebSyncProvider* webSyncProvider = Platform::current()->backgroundSyncProvider();
    ASSERT(webSyncProvider);
    return webSyncProvider;
}

}

SyncManager::SyncManager(ServiceWorkerRegistration* registration)
    : m_registration(registration)
{
    ASSERT(registration);
}

ScriptPromise SyncManager::registerFunction(ScriptState* scriptState, ExecutionContext* context, const String& tag)
{
    // TODO(jkarlin): Wait for the registration to become active instead of rejecting. See crbug.com/542437.
    if (!m_registration->active())
        return ScriptPromise::rejectWithDOMException(scriptState, DOMException::create(AbortError, "Registration failed - no active Service Worker"));

    if (scriptState->executionContext()->isDocument()) {
        Document* document = toDocument(scriptState->executionContext());
        if (!document->domWindow() || !document->frame())
            return ScriptPromise::rejectWithDOMException(scriptState, DOMException::create(InvalidStateError, "Document is detached from window."));
        if (!document->frame()->isMainFrame())
            return ScriptPromise::rejectWithDOMException(scriptState, DOMException::create(AbortError, "Registration failed - not called from a main frame."));
    }

    ScriptPromiseResolver* resolver = ScriptPromiseResolver::create(scriptState);
    ScriptPromise promise = resolver->promise();

    WebSyncRegistration* webSyncRegistration = new WebSyncRegistration(
        WebSyncRegistration::UNREGISTERED_SYNC_ID /* id */,
        WebSyncRegistration::PeriodicityOneShot,
        tag,
        0 /* minPeriod */,
        WebSyncRegistration::NetworkStateOnline /* networkState */,
        WebSyncRegistration::PowerStateAuto /* powerState */
    );
    backgroundSyncProvider()->registerBackgroundSync(webSyncRegistration, m_registration->webRegistration(), context->isServiceWorkerGlobalScope(), new SyncRegistrationCallbacks(resolver, m_registration));

    return promise;
}

ScriptPromise SyncManager::getTags(ScriptState* scriptState)
{
    ScriptPromiseResolver* resolver = ScriptPromiseResolver::create(scriptState);
    ScriptPromise promise = resolver->promise();

    backgroundSyncProvider()->getRegistrations(WebSyncRegistration::PeriodicityOneShot, m_registration->webRegistration(), new SyncGetRegistrationsCallbacks(resolver, m_registration));

    return promise;
}

DEFINE_TRACE(SyncManager)
{
    visitor->trace(m_registration);
}

} // namespace blink
