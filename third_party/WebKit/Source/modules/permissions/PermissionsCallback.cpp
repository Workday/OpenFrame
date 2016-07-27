// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include "modules/permissions/PermissionsCallback.h"

#include "bindings/core/v8/ScriptPromiseResolver.h"
#include "modules/permissions/PermissionStatus.h"

namespace blink {

PermissionsCallback::PermissionsCallback(ScriptPromiseResolver* resolver, PassOwnPtr<Vector<WebPermissionType>> internalPermissions, PassOwnPtr<Vector<int>> callerIndexToInternalIndex)
    : m_resolver(resolver),
    m_internalPermissions(internalPermissions),
    m_callerIndexToInternalIndex(callerIndexToInternalIndex)
{
    ASSERT(m_resolver);
}

void PermissionsCallback::onSuccess(WebPassOwnPtr<WebVector<WebPermissionStatus>> permissionStatus)
{
    if (!m_resolver->executionContext() || m_resolver->executionContext()->activeDOMObjectsAreStopped())
        return;

    OwnPtr<WebVector<WebPermissionStatus>> statusPtr = permissionStatus.release();
    HeapVector<Member<PermissionStatus>> result(m_callerIndexToInternalIndex->size());

    // Create the response vector by finding the status for each index by
    // using the caller to internal index mapping and looking up the status
    // using the internal index obtained.
    for (size_t i = 0; i < m_callerIndexToInternalIndex->size(); ++i) {
        int internalIndex = m_callerIndexToInternalIndex->operator[](i);
        result[i] = PermissionStatus::createAndListen(m_resolver->executionContext(), statusPtr->operator[](internalIndex), m_internalPermissions->operator[](internalIndex));
    }
    m_resolver->resolve(result);
}

void PermissionsCallback::onError()
{
    if (!m_resolver->executionContext() || m_resolver->executionContext()->activeDOMObjectsAreStopped())
        return;
    m_resolver->reject();
}

} // namespace blink
