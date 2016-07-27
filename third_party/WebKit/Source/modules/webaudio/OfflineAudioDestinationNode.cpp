/*
 * Copyright (C) 2011, Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#if ENABLE(WEB_AUDIO)
#include "modules/webaudio/OfflineAudioDestinationNode.h"

#include "core/dom/CrossThreadTask.h"
#include "modules/webaudio/AbstractAudioContext.h"
#include "modules/webaudio/AudioNodeInput.h"
#include "modules/webaudio/AudioNodeOutput.h"
#include "modules/webaudio/OfflineAudioContext.h"
#include "platform/Task.h"
#include "platform/audio/AudioBus.h"
#include "platform/audio/DenormalDisabler.h"
#include "platform/audio/HRTFDatabaseLoader.h"
#include "public/platform/Platform.h"
#include <algorithm>

namespace blink {

const size_t OfflineAudioDestinationHandler::renderQuantumSize = 128;

OfflineAudioDestinationHandler::OfflineAudioDestinationHandler(AudioNode& node, AudioBuffer* renderTarget)
    : AudioDestinationHandler(node, renderTarget->sampleRate())
    , m_renderTarget(renderTarget)
    , m_renderThread(adoptPtr(Platform::current()->createThread("offline audio renderer")))
    , m_framesProcessed(0)
    , m_framesToProcess(0)
    , m_isRenderingStarted(false)
    , m_shouldSuspend(false)
{
    m_renderBus = AudioBus::create(renderTarget->numberOfChannels(), renderQuantumSize);
    m_framesToProcess = m_renderTarget->length();
}

PassRefPtr<OfflineAudioDestinationHandler> OfflineAudioDestinationHandler::create(AudioNode& node, AudioBuffer* renderTarget)
{
    return adoptRef(new OfflineAudioDestinationHandler(node, renderTarget));
}

OfflineAudioDestinationHandler::~OfflineAudioDestinationHandler()
{
    ASSERT(!isInitialized());
}

void OfflineAudioDestinationHandler::dispose()
{
    uninitialize();
    AudioDestinationHandler::dispose();
}

void OfflineAudioDestinationHandler::initialize()
{
    if (isInitialized())
        return;

    AudioHandler::initialize();
}

void OfflineAudioDestinationHandler::uninitialize()
{
    if (!isInitialized())
        return;

    if (m_renderThread)
        m_renderThread.clear();

    AudioHandler::uninitialize();
}

OfflineAudioContext* OfflineAudioDestinationHandler::context() const
{
    return static_cast<OfflineAudioContext*>(AudioDestinationHandler::context());
}

void OfflineAudioDestinationHandler::startRendering()
{
    ASSERT(isMainThread());
    ASSERT(m_renderThread);
    ASSERT(m_renderTarget);

    if (!m_renderTarget)
        return;

    // Rendering was not started. Starting now.
    if (!m_isRenderingStarted) {
        m_isRenderingStarted = true;
        m_renderThread->taskRunner()->postTask(BLINK_FROM_HERE,
            new Task(threadSafeBind(&OfflineAudioDestinationHandler::startOfflineRendering, this)));
        return;
    }

    // Rendering is already started, which implicitly means we resume the
    // rendering by calling |doOfflineRendering| on the render thread.
    m_renderThread->taskRunner()->postTask(BLINK_FROM_HERE,
        threadSafeBind(&OfflineAudioDestinationHandler::doOfflineRendering, this));
}

void OfflineAudioDestinationHandler::stopRendering()
{
    // offline audio rendering CANNOT BE stopped by JavaScript.
    ASSERT_NOT_REACHED();
}

WebThread* OfflineAudioDestinationHandler::offlineRenderThread()
{
    ASSERT(m_renderThread);

    return m_renderThread.get();
}

void OfflineAudioDestinationHandler::startOfflineRendering()
{
    ASSERT(!isMainThread());

    ASSERT(m_renderBus);
    if (!m_renderBus)
        return;

    bool isAudioContextInitialized = context()->isDestinationInitialized();
    ASSERT(isAudioContextInitialized);
    if (!isAudioContextInitialized)
        return;

    bool channelsMatch = m_renderBus->numberOfChannels() == m_renderTarget->numberOfChannels();
    ASSERT(channelsMatch);
    if (!channelsMatch)
        return;

    bool isRenderBusAllocated = m_renderBus->length() >= renderQuantumSize;
    ASSERT(isRenderBusAllocated);
    if (!isRenderBusAllocated)
        return;

    // Start rendering.
    doOfflineRendering();
}

void OfflineAudioDestinationHandler::doOfflineRendering()
{
    ASSERT(!isMainThread());

    unsigned numberOfChannels = m_renderTarget->numberOfChannels();

    // Reset the suspend flag.
    m_shouldSuspend = false;

    // If there is more to process and there is no suspension at the moment,
    // do continue to render quanta. Then calling OfflineAudioContext.resume() will pick up
    // the render loop again from where it was suspended.
    while (m_framesToProcess > 0 && !m_shouldSuspend) {

        // Suspend the rendering and update m_shouldSuspend if a scheduled
        // suspend found at the current sample frame. Otherwise render one
        // quantum and return false.
        m_shouldSuspend = renderIfNotSuspended(0, m_renderBus.get(), renderQuantumSize);

        if (m_shouldSuspend)
            return;

        size_t framesAvailableToCopy = std::min(m_framesToProcess, renderQuantumSize);

        for (unsigned channelIndex = 0; channelIndex < numberOfChannels; ++channelIndex) {
            const float* source = m_renderBus->channel(channelIndex)->data();
            float* destination = m_renderTarget->getChannelData(channelIndex)->data();
            memcpy(destination + m_framesProcessed, source, sizeof(float) * framesAvailableToCopy);
        }

        m_framesProcessed += framesAvailableToCopy;

        ASSERT(m_framesToProcess >= framesAvailableToCopy);
        m_framesToProcess -= framesAvailableToCopy;
    }

    // Finish up the rendering loop if there is no more to process.
    if (!m_framesToProcess)
        finishOfflineRendering();
}

void OfflineAudioDestinationHandler::suspendOfflineRendering()
{
    ASSERT(!isMainThread());

    // The actual rendering has been suspended. Notify the context.
    if (context()->executionContext()) {
        context()->executionContext()->postTask(BLINK_FROM_HERE,
            createCrossThreadTask(&OfflineAudioDestinationHandler::notifySuspend, this));
    }
}

void OfflineAudioDestinationHandler::finishOfflineRendering()
{
    ASSERT(!isMainThread());

    // The actual rendering has been completed. Notify the context.
    if (context()->executionContext()) {
        context()->executionContext()->postTask(BLINK_FROM_HERE,
            createCrossThreadTask(&OfflineAudioDestinationHandler::notifyComplete, this));
    }
}

void OfflineAudioDestinationHandler::notifySuspend()
{
    if (context())
        context()->resolveSuspendOnMainThread(context()->currentSampleFrame());
}

void OfflineAudioDestinationHandler::notifyComplete()
{
    // The OfflineAudioContext might be gone.
    if (context())
        context()->fireCompletionEvent();
}

bool OfflineAudioDestinationHandler::renderIfNotSuspended(AudioBus* sourceBus, AudioBus* destinationBus, size_t numberOfFrames)
{
    // We don't want denormals slowing down any of the audio processing
    // since they can very seriously hurt performance.
    // This will take care of all AudioNodes because they all process within this scope.
    DenormalDisabler denormalDisabler;

    context()->deferredTaskHandler().setAudioThread(currentThread());

    if (!context()->isDestinationInitialized()) {
        destinationBus->zero();
        return false;
    }

    // Take care pre-render tasks at the beginning of each render quantum. Then
    // it will stop the rendering loop if the context needs to be suspended
    // at the beginning of the next render quantum.
    if (context()->handlePreOfflineRenderTasks()) {
        suspendOfflineRendering();
        return true;
    }

    // Prepare the local audio input provider for this render quantum.
    if (sourceBus)
        m_localAudioInputProvider.set(sourceBus);

    ASSERT(numberOfInputs() >= 1);
    if (numberOfInputs() < 1) {
        destinationBus->zero();
        return false;
    }
    // This will cause the node(s) connected to us to process, which in turn will pull on their input(s),
    // all the way backwards through the rendering graph.
    AudioBus* renderedBus = input(0).pull(destinationBus, numberOfFrames);

    if (!renderedBus) {
        destinationBus->zero();
    } else if (renderedBus != destinationBus) {
        // in-place processing was not possible - so copy
        destinationBus->copyFrom(*renderedBus);
    }

    // Process nodes which need a little extra help because they are not connected to anything, but still need to process.
    context()->deferredTaskHandler().processAutomaticPullNodes(numberOfFrames);

    // Let the context take care of any business at the end of each render quantum.
    context()->handlePostOfflineRenderTasks();

    // Advance current sample-frame.
    size_t newSampleFrame = m_currentSampleFrame + numberOfFrames;
    releaseStore(&m_currentSampleFrame, newSampleFrame);

    return false;
}

// ----------------------------------------------------------------

OfflineAudioDestinationNode::OfflineAudioDestinationNode(AbstractAudioContext& context, AudioBuffer* renderTarget)
    : AudioDestinationNode(context)
{
    setHandler(OfflineAudioDestinationHandler::create(*this, renderTarget));
}

OfflineAudioDestinationNode* OfflineAudioDestinationNode::create(AbstractAudioContext* context, AudioBuffer* renderTarget)
{
    return new OfflineAudioDestinationNode(*context, renderTarget);
}

} // namespace blink

#endif // ENABLE(WEB_AUDIO)
