// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WindowProxyManager_h
#define WindowProxyManager_h

#include "core/CoreExport.h"
#include "platform/heap/Handle.h"
#include "wtf/Vector.h"
#include <utility>
#include <v8.h>

namespace blink {

class DOMWrapperWorld;
class Frame;
class ScriptState;
class SecurityOrigin;
class WindowProxy;

class CORE_EXPORT WindowProxyManager final : public NoBaseWillBeGarbageCollectedFinalized<WindowProxyManager> {
    USING_FAST_MALLOC_WILL_BE_REMOVED(WindowProxyManager);
public:
    static PassOwnPtrWillBeRawPtr<WindowProxyManager> create(Frame&);

    ~WindowProxyManager();
    DECLARE_TRACE();

    Frame* frame() const { return m_frame.get(); }
    v8::Isolate* isolate() const { return m_isolate; }
    WindowProxy* mainWorldProxy() const { return m_windowProxy.get(); }

    WindowProxy* windowProxy(DOMWrapperWorld&);

    void clearForClose();
    void clearForNavigation();

    // For devtools:
    WindowProxy* existingWindowProxy(DOMWrapperWorld&);
    void collectIsolatedContexts(Vector<std::pair<ScriptState*, SecurityOrigin*>>&);

    void releaseGlobals(HashMap<DOMWrapperWorld*, v8::Local<v8::Object>>&);
    void setGlobals(const HashMap<DOMWrapperWorld*, v8::Local<v8::Object>>&);

private:
    typedef WillBeHeapHashMap<int, OwnPtrWillBeMember<WindowProxy>> IsolatedWorldMap;

    explicit WindowProxyManager(Frame&);

    RawPtrWillBeMember<Frame> m_frame;
    v8::Isolate* const m_isolate;

    const OwnPtrWillBeMember<WindowProxy> m_windowProxy;
    IsolatedWorldMap m_isolatedWorlds;
};

} // namespace blink

#endif // WindowProxyManager_h
