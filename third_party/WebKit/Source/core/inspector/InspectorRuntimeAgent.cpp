/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "core/inspector/InspectorRuntimeAgent.h"

#include "bindings/core/v8/ScriptState.h"
#include "core/inspector/InjectedScript.h"
#include "core/inspector/InjectedScriptManager.h"
#include "core/inspector/InspectorState.h"
#include "core/inspector/MuteConsoleScope.h"
#include "core/inspector/RemoteObjectId.h"
#include "core/inspector/v8/V8Debugger.h"
#include "core/inspector/v8/V8RuntimeAgent.h"
#include "platform/JSONValues.h"

using blink::TypeBuilder::Runtime::ExecutionContextDescription;

namespace blink {

namespace InspectorRuntimeAgentState {
static const char runtimeEnabled[] = "runtimeEnabled";
};

InspectorRuntimeAgent::InspectorRuntimeAgent(InjectedScriptManager* injectedScriptManager, V8Debugger* debugger, Client* client)
    : InspectorBaseAgent<InspectorRuntimeAgent, InspectorFrontend::Runtime>("Runtime")
    , m_enabled(false)
    , m_v8RuntimeAgent(V8RuntimeAgent::create(injectedScriptManager, debugger))
    , m_injectedScriptManager(injectedScriptManager)
    , m_client(client)
{
}

InspectorRuntimeAgent::~InspectorRuntimeAgent()
{
}

DEFINE_TRACE(InspectorRuntimeAgent)
{
    visitor->trace(m_injectedScriptManager);
    InspectorBaseAgent::trace(visitor);
}

// InspectorBaseAgent overrides.
void InspectorRuntimeAgent::init()
{
    m_v8RuntimeAgent->setInspectorState(m_state);
}

void InspectorRuntimeAgent::setFrontend(InspectorFrontend* frontend)
{
    InspectorBaseAgent::setFrontend(frontend);
    m_v8RuntimeAgent->setFrontend(InspectorFrontend::Runtime::from(frontend));
}

void InspectorRuntimeAgent::clearFrontend()
{
    m_v8RuntimeAgent->clearFrontend();
    InspectorBaseAgent::clearFrontend();
}

void InspectorRuntimeAgent::restore()
{
    if (!m_state->getBoolean(InspectorRuntimeAgentState::runtimeEnabled))
        return;
    m_v8RuntimeAgent->restore();
    ErrorString errorString;
    enable(&errorString);
}

void InspectorRuntimeAgent::evaluate(ErrorString* errorString, const String& expression, const String* const objectGroup, const bool* const includeCommandLineAPI, const bool* const doNotPauseOnExceptionsAndMuteConsole, const int* optExecutionContextId, const bool* const returnByValue, const bool* generatePreview, RefPtr<TypeBuilder::Runtime::RemoteObject>& result, TypeBuilder::OptOutput<bool>* wasThrown, RefPtr<TypeBuilder::Debugger::ExceptionDetails>& exceptionDetails)
{
    int executionContextId = optExecutionContextId ? *optExecutionContextId : m_injectedScriptManager->injectedScriptIdFor(defaultScriptState());
    MuteConsoleScope<InspectorRuntimeAgent> muteScope;
    if (asBool(doNotPauseOnExceptionsAndMuteConsole))
        muteScope.enter(this);
    m_v8RuntimeAgent->evaluate(errorString, expression, objectGroup, includeCommandLineAPI, doNotPauseOnExceptionsAndMuteConsole, &executionContextId, returnByValue, generatePreview, result, wasThrown, exceptionDetails);
}

void InspectorRuntimeAgent::callFunctionOn(ErrorString* errorString, const String& objectId, const String& expression, const RefPtr<JSONArray>* const optionalArguments, const bool* const doNotPauseOnExceptionsAndMuteConsole, const bool* const returnByValue, const bool* generatePreview, RefPtr<TypeBuilder::Runtime::RemoteObject>& result, TypeBuilder::OptOutput<bool>* wasThrown)
{
    MuteConsoleScope<InspectorRuntimeAgent> muteScope;
    if (asBool(doNotPauseOnExceptionsAndMuteConsole))
        muteScope.enter(this);
    m_v8RuntimeAgent->callFunctionOn(errorString, objectId, expression, optionalArguments, doNotPauseOnExceptionsAndMuteConsole, returnByValue, generatePreview, result, wasThrown);
}

void InspectorRuntimeAgent::getProperties(ErrorString* errorString, const String& objectId, const bool* ownProperties, const bool* accessorPropertiesOnly, const bool* generatePreview, RefPtr<TypeBuilder::Array<TypeBuilder::Runtime::PropertyDescriptor>>& result, RefPtr<TypeBuilder::Array<TypeBuilder::Runtime::InternalPropertyDescriptor>>& internalProperties, RefPtr<TypeBuilder::Debugger::ExceptionDetails>& exceptionDetails)
{
    MuteConsoleScope<InspectorRuntimeAgent> muteScope(this);
    m_v8RuntimeAgent->getProperties(errorString, objectId, ownProperties, accessorPropertiesOnly, generatePreview, result, internalProperties, exceptionDetails);
}

void InspectorRuntimeAgent::releaseObject(ErrorString* errorString, const String& objectId)
{
    m_v8RuntimeAgent->releaseObject(errorString, objectId);
}

void InspectorRuntimeAgent::releaseObjectGroup(ErrorString* errorString, const String& objectGroup)
{
    m_v8RuntimeAgent->releaseObjectGroup(errorString, objectGroup);
}

void InspectorRuntimeAgent::run(ErrorString* errorString)
{
    m_client->resumeStartup();
}

void InspectorRuntimeAgent::isRunRequired(ErrorString* errorString, bool* outResult)
{
    *outResult = m_client->isRunRequired();
}

void InspectorRuntimeAgent::setCustomObjectFormatterEnabled(ErrorString* errorString, bool enabled)
{
    m_v8RuntimeAgent->setCustomObjectFormatterEnabled(errorString, enabled);
}

void InspectorRuntimeAgent::enable(ErrorString* errorString)
{
    if (m_enabled)
        return;

    m_enabled = true;
    m_state->setBoolean(InspectorRuntimeAgentState::runtimeEnabled, true);
    m_v8RuntimeAgent->enable(errorString);
}

void InspectorRuntimeAgent::disable(ErrorString* errorString)
{
    if (!m_enabled)
        return;

    m_enabled = false;
    m_state->setBoolean(InspectorRuntimeAgentState::runtimeEnabled, false);
    m_v8RuntimeAgent->disable(errorString);
}

void InspectorRuntimeAgent::addExecutionContextToFrontend(int executionContextId, const String& type, const String& origin, const String& humanReadableName, const String& frameId)
{
    m_v8RuntimeAgent->addExecutionContextToFrontend(executionContextId, type, origin, humanReadableName, frameId);
}

} // namespace blink
