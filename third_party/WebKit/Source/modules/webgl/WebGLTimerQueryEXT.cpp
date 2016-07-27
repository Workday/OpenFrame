// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"

#include "modules/webgl/WebGLTimerQueryEXT.h"

#include "modules/webgl/WebGLRenderingContextBase.h"
#include "public/platform/Platform.h"

namespace blink {

WebGLTimerQueryEXT* WebGLTimerQueryEXT::create(WebGLRenderingContextBase* ctx)
{
    return new WebGLTimerQueryEXT(ctx);
}

WebGLTimerQueryEXT::~WebGLTimerQueryEXT()
{
    unregisterTaskObserver();

    // See the comment in WebGLObject::detachAndDeleteObject().
    detachAndDeleteObject();
}

WebGLTimerQueryEXT::WebGLTimerQueryEXT(WebGLRenderingContextBase* ctx)
    : WebGLContextObject(ctx)
    , m_target(0)
    , m_queryId(0)
    , m_taskObserverRegistered(false)
    , m_canUpdateAvailability(false)
    , m_queryResultAvailable(false)
    , m_queryResult(0)
{
    m_queryId = context()->webContext()->createQueryEXT();
}

void WebGLTimerQueryEXT::resetCachedResult()
{
    m_canUpdateAvailability = false;
    m_queryResultAvailable = false;
    m_queryResult = 0;
    // When this is called, the implication is that we should start
    // keeping track of whether we can update the cached availability
    // and result.
    registerTaskObserver();
}

void WebGLTimerQueryEXT::updateCachedResult(WebGraphicsContext3D* ctx)
{
    if (m_queryResultAvailable)
        return;

    if (!m_canUpdateAvailability)
        return;

    if (!hasTarget())
        return;

    // We can only update the cached result when control returns to the browser.
    m_canUpdateAvailability = false;
    GLuint available = 0;
    ctx->getQueryObjectuivEXT(object(), GL_QUERY_RESULT_AVAILABLE_EXT, &available);
    m_queryResultAvailable = !!available;
    if (m_queryResultAvailable) {
        GLuint64 result = 0;
        ctx->getQueryObjectui64vEXT(object(), GL_QUERY_RESULT_EXT, &result);
        m_queryResult = result;
        unregisterTaskObserver();
    }
}

bool WebGLTimerQueryEXT::isQueryResultAvailable()
{
    return m_queryResultAvailable;
}

GLuint64 WebGLTimerQueryEXT::getQueryResult()
{
    return m_queryResult;
}

void WebGLTimerQueryEXT::deleteObjectImpl(WebGraphicsContext3D* context3d)
{
    context3d->deleteQueryEXT(m_queryId);
    m_queryId = 0;
}

void WebGLTimerQueryEXT::registerTaskObserver()
{
    if (!m_taskObserverRegistered) {
        m_taskObserverRegistered = true;
        Platform::current()->currentThread()->addTaskObserver(this);
    }
}

void WebGLTimerQueryEXT::unregisterTaskObserver()
{
    if (m_taskObserverRegistered) {
        m_taskObserverRegistered = false;
        Platform::current()->currentThread()->removeTaskObserver(this);
    }
}

void WebGLTimerQueryEXT::didProcessTask()
{
    m_canUpdateAvailability = true;
}

} // namespace blink
