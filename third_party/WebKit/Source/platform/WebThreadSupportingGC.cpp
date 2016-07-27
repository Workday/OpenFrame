// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include "platform/WebThreadSupportingGC.h"

#include "platform/heap/SafePoint.h"
#include "public/platform/WebScheduler.h"
#include "wtf/Threading.h"

namespace blink {

PassOwnPtr<WebThreadSupportingGC> WebThreadSupportingGC::create(const char* name)
{
    return adoptPtr(new WebThreadSupportingGC(name, nullptr));
}

PassOwnPtr<WebThreadSupportingGC> WebThreadSupportingGC::createForThread(WebThread* thread)
{
    return adoptPtr(new WebThreadSupportingGC(nullptr, thread));
}

WebThreadSupportingGC::WebThreadSupportingGC(const char* name, WebThread* thread)
    : m_thread(thread)
{
#if ENABLE(ASSERT)
    ASSERT(!name || !thread);
    // We call this regardless of whether an existing thread is given or not,
    // as it means that blink is going to run with more than one thread.
    WTF::willCreateThread();
#endif
    if (!m_thread) {
        // If |thread| is not given, create a new one and own it.
        m_owningThread = adoptPtr(Platform::current()->createThread(name));
        m_thread = m_owningThread.get();
    }
}

WebThreadSupportingGC::~WebThreadSupportingGC()
{
    if (ThreadState::current() && m_owningThread) {
        // WebThread's destructor blocks until all the tasks are processed.
        SafePointScope scope(BlinkGC::HeapPointersOnStack);
        m_owningThread.clear();
    }
}

void WebThreadSupportingGC::initialize()
{
    ThreadState::attach();
    m_gcTaskRunner = adoptPtr(new GCTaskRunner(m_thread));
}

void WebThreadSupportingGC::shutdown()
{
    // Ensure no posted tasks will run from this point on.
    m_gcTaskRunner.clear();

    // Shutdown the thread (via its scheduler) only when the thread is created
    // and is owned by this instance.
    if (m_owningThread)
        m_owningThread->scheduler()->shutdown();

    ThreadState::detach();
}

} // namespace blink
