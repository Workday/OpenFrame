// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include "web/InspectorEmulationAgent.h"

#include "core/frame/FrameHost.h"
#include "core/frame/FrameView.h"
#include "core/frame/Settings.h"
#include "core/inspector/InspectorState.h"
#include "core/page/Page.h"
#include "platform/geometry/DoubleRect.h"
#include "web/DevToolsEmulator.h"
#include "web/WebLocalFrameImpl.h"
#include "web/WebViewImpl.h"

namespace blink {

namespace EmulationAgentState {
static const char scriptExecutionDisabled[] = "scriptExecutionDisabled";
static const char touchEventEmulationEnabled[] = "touchEventEmulationEnabled";
static const char emulatedMedia[] = "emulatedMedia";
}

PassOwnPtrWillBeRawPtr<InspectorEmulationAgent> InspectorEmulationAgent::create(WebLocalFrameImpl* webLocalFrameImpl)
{
    return adoptPtrWillBeNoop(new InspectorEmulationAgent(webLocalFrameImpl));
}

InspectorEmulationAgent::InspectorEmulationAgent(WebLocalFrameImpl* webLocalFrameImpl)
    : InspectorBaseAgent<InspectorEmulationAgent, InspectorFrontend::Emulation>("Emulation")
    , m_webLocalFrameImpl(webLocalFrameImpl)
{
    webViewImpl()->devToolsEmulator()->setEmulationAgent(this);
}

InspectorEmulationAgent::~InspectorEmulationAgent()
{
}

WebViewImpl* InspectorEmulationAgent::webViewImpl()
{
    return m_webLocalFrameImpl->viewImpl();
}

void InspectorEmulationAgent::restore()
{
    ErrorString error;
    setScriptExecutionDisabled(&error, m_state->getBoolean(EmulationAgentState::scriptExecutionDisabled));
    setTouchEmulationEnabled(&error, m_state->getBoolean(EmulationAgentState::touchEventEmulationEnabled), nullptr);
    setEmulatedMedia(&error, m_state->getString(EmulationAgentState::emulatedMedia));
}

void InspectorEmulationAgent::disable(ErrorString*)
{
    ErrorString error;
    setScriptExecutionDisabled(&error, false);
    setTouchEmulationEnabled(&error, false, nullptr);
    setEmulatedMedia(&error, String());
}

void InspectorEmulationAgent::discardAgent()
{
    webViewImpl()->devToolsEmulator()->setEmulationAgent(nullptr);
}

void InspectorEmulationAgent::didCommitLoadForLocalFrame(LocalFrame* frame)
{
    if (frame == m_webLocalFrameImpl->frame())
        viewportChanged();
}

void InspectorEmulationAgent::resetScrollAndPageScaleFactor(ErrorString*)
{
    webViewImpl()->resetScrollAndScaleStateImmediately();
}

void InspectorEmulationAgent::setPageScaleFactor(ErrorString*, double pageScaleFactor)
{
    webViewImpl()->setPageScaleFactor(static_cast<float>(pageScaleFactor));
}

void InspectorEmulationAgent::setScriptExecutionDisabled(ErrorString*, bool value)
{
    m_state->setBoolean(EmulationAgentState::scriptExecutionDisabled, value);
    webViewImpl()->devToolsEmulator()->setScriptExecutionDisabled(value);
}

void InspectorEmulationAgent::setTouchEmulationEnabled(ErrorString*, bool enabled, const String* configuration)
{
    m_state->setBoolean(EmulationAgentState::touchEventEmulationEnabled, enabled);
    webViewImpl()->devToolsEmulator()->setTouchEventEmulationEnabled(enabled);
}

void InspectorEmulationAgent::setEmulatedMedia(ErrorString*, const String& media)
{
    m_state->setString(EmulationAgentState::emulatedMedia, media);
    webViewImpl()->page()->settings().setMediaTypeOverride(media);
}

void InspectorEmulationAgent::viewportChanged()
{
    if (!webViewImpl()->devToolsEmulator()->deviceEmulationEnabled() || !frontend())
        return;

    FrameView* view = m_webLocalFrameImpl->frameView();
    if (!view)
        return;

    IntSize contentsSize = view->contentsSize();
    FloatPoint scrollOffset;
    scrollOffset = FloatPoint(view->scrollableArea()->visibleContentRectDouble().location());

    RefPtr<TypeBuilder::Emulation::Viewport> viewport = TypeBuilder::Emulation::Viewport::create()
        .setScrollX(scrollOffset.x())
        .setScrollY(scrollOffset.y())
        .setContentsWidth(contentsSize.width())
        .setContentsHeight(contentsSize.height())
        .setPageScaleFactor(webViewImpl()->page()->pageScaleFactor())
        .setMinimumPageScaleFactor(webViewImpl()->minimumPageScaleFactor())
        .setMaximumPageScaleFactor(webViewImpl()->maximumPageScaleFactor());
    frontend()->viewportChanged(viewport);
}

DEFINE_TRACE(InspectorEmulationAgent)
{
    visitor->trace(m_webLocalFrameImpl);
    InspectorBaseAgent::trace(visitor);
}

} // namespace blink
