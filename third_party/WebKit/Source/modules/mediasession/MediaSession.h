// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MediaSession_h
#define MediaSession_h

#include "bindings/core/v8/ScriptPromise.h"
#include "bindings/core/v8/ScriptWrappable.h"
#include "modules/ModulesExport.h"
#include "platform/heap/Handle.h"
#include "public/platform/modules/mediasession/WebMediaSession.h"
#include "wtf/OwnPtr.h"

namespace blink {

class ScriptState;

class MODULES_EXPORT MediaSession
    : public GarbageCollectedFinalized<MediaSession>
    , public ScriptWrappable {
    DEFINE_WRAPPERTYPEINFO();
public:
    static MediaSession* create(ExecutionContext*, ExceptionState&);

    ScriptPromise activate(ScriptState*);
    ScriptPromise deactivate(ScriptState*);

    DEFINE_INLINE_TRACE() { }

private:
    friend class MediaSessionTest;

    explicit MediaSession(PassOwnPtr<WebMediaSession>);

    OwnPtr<WebMediaSession> m_webMediaSession;
};

} // namespace blink

#endif // MediaSession_h
