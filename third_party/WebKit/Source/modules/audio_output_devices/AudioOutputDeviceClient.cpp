// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include "modules/audio_output_devices/AudioOutputDeviceClient.h"

#include "core/dom/Document.h"
#include "core/dom/ExecutionContext.h"
#include "core/frame/LocalFrame.h"

namespace blink {

const char* AudioOutputDeviceClient::supplementName()
{
    return "AudioOutputDeviceClient";
}

AudioOutputDeviceClient* AudioOutputDeviceClient::from(ExecutionContext* context)
{
    if (!context->isDocument())
        return nullptr;

    const Document* document = toDocument(context);
    if (!document->frame() || !document->frame()->isLocalFrame())
        return nullptr;

    return static_cast<AudioOutputDeviceClient*>(WillBeHeapSupplement<LocalFrame>::from(document->frame(), supplementName()));
}

void provideAudioOutputDeviceClientTo(LocalFrame& frame, PassOwnPtrWillBeRawPtr<AudioOutputDeviceClient> client)
{
    frame.provideSupplement(AudioOutputDeviceClient::supplementName(), client);
}

} // namespace blink
