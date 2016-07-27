// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include "platform/scheduler/CancellableTaskFactory.h"

#include "public/platform/Platform.h"
#include "wtf/InstanceCounter.h"

namespace blink {

void CancellableTaskFactory::cancel()
{
    m_weakPtrFactory.revokeAll();
}

WebTaskRunner::Task* CancellableTaskFactory::cancelAndCreate()
{
    cancel();
    return new CancellableTask(m_weakPtrFactory.createWeakPtr());
}

void CancellableTaskFactory::CancellableTask::run()
{
    if (CancellableTaskFactory* taskFactory = m_weakPtr.get()) {
        Closure* closure = taskFactory->m_closure.get();
        taskFactory->m_weakPtrFactory.revokeAll();
        (*closure)();
    }
}

} // namespace blink
