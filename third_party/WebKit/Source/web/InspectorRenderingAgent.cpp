// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include "web/InspectorRenderingAgent.h"

#include "core/frame/FrameView.h"
#include "core/frame/Settings.h"
#include "core/inspector/InspectorState.h"
#include "core/page/Page.h"
#include "web/WebLocalFrameImpl.h"
#include "web/WebViewImpl.h"

namespace blink {

namespace RenderingAgentState {
static const char showDebugBorders[] = "showDebugBorders";
static const char showFPSCounter[] = "showFPSCounter";
static const char showPaintRects[] = "showPaintRects";
static const char showScrollBottleneckRects[] = "showScrollBottleneckRects";
}

PassOwnPtrWillBeRawPtr<InspectorRenderingAgent> InspectorRenderingAgent::create(WebLocalFrameImpl* webLocalFrameImpl)
{
    return adoptPtrWillBeNoop(new InspectorRenderingAgent(webLocalFrameImpl));
}

InspectorRenderingAgent::InspectorRenderingAgent(WebLocalFrameImpl* webLocalFrameImpl)
    : InspectorBaseAgent<InspectorRenderingAgent, InspectorFrontend::Rendering>("Rendering")
    , m_webLocalFrameImpl(webLocalFrameImpl)
{
}

WebViewImpl* InspectorRenderingAgent::webViewImpl()
{
    return m_webLocalFrameImpl->viewImpl();
}

void InspectorRenderingAgent::restore()
{
    ErrorString error;
    setShowDebugBorders(&error, m_state->getBoolean(RenderingAgentState::showDebugBorders));
    setShowFPSCounter(&error, m_state->getBoolean(RenderingAgentState::showFPSCounter));
    setShowPaintRects(&error, m_state->getBoolean(RenderingAgentState::showPaintRects));
    setShowScrollBottleneckRects(&error, m_state->getBoolean(RenderingAgentState::showScrollBottleneckRects));
}

void InspectorRenderingAgent::disable(ErrorString*)
{
    ErrorString error;
    setShowDebugBorders(&error, false);
    setShowFPSCounter(&error, false);
    setShowPaintRects(&error, false);
    setShowScrollBottleneckRects(&error, false);
}

void InspectorRenderingAgent::setShowDebugBorders(ErrorString* errorString, bool show)
{
    m_state->setBoolean(RenderingAgentState::showDebugBorders, show);
    if (show && !compositingEnabled(errorString))
        return;
    webViewImpl()->setShowDebugBorders(show);
}

void InspectorRenderingAgent::setShowFPSCounter(ErrorString* errorString, bool show)
{
    m_state->setBoolean(RenderingAgentState::showFPSCounter, show);
    if (show && !compositingEnabled(errorString))
        return;
    webViewImpl()->setShowFPSCounter(show);
}

void InspectorRenderingAgent::setShowPaintRects(ErrorString*, bool show)
{
    m_state->setBoolean(RenderingAgentState::showPaintRects, show);
    webViewImpl()->setShowPaintRects(show);
    if (!show && m_webLocalFrameImpl->frameView())
        m_webLocalFrameImpl->frameView()->invalidate();
}

void InspectorRenderingAgent::setShowScrollBottleneckRects(ErrorString* errorString, bool show)
{
    m_state->setBoolean(RenderingAgentState::showScrollBottleneckRects, show);
    if (show && !compositingEnabled(errorString))
        return;
    webViewImpl()->setShowScrollBottleneckRects(show);
}

bool InspectorRenderingAgent::compositingEnabled(ErrorString* errorString)
{
    if (!webViewImpl()->page()->settings().acceleratedCompositingEnabled()) {
        if (errorString)
            *errorString = "Compositing mode is not supported";
        return false;
    }
    return true;
}

DEFINE_TRACE(InspectorRenderingAgent)
{
    visitor->trace(m_webLocalFrameImpl);
    InspectorBaseAgent::trace(visitor);
}

} // namespace blink
