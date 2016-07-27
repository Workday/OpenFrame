/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
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
#include "platform/heap/ThreadState.h"

#include "platform/ScriptForbiddenScope.h"
#include "platform/TraceEvent.h"
#include "platform/heap/BlinkGCMemoryDumpProvider.h"
#include "platform/heap/CallbackStack.h"
#include "platform/heap/Handle.h"
#include "platform/heap/Heap.h"
#include "platform/heap/MarkingVisitor.h"
#include "platform/heap/SafePoint.h"
#include "public/platform/Platform.h"
#include "public/platform/WebMemoryAllocatorDump.h"
#include "public/platform/WebProcessMemoryDump.h"
#include "public/platform/WebScheduler.h"
#include "public/platform/WebThread.h"
#include "public/platform/WebTraceLocation.h"
#include "wtf/DataLog.h"
#include "wtf/Partitions.h"
#include "wtf/ThreadingPrimitives.h"

#if OS(WIN)
#include <stddef.h>
#include <windows.h>
#include <winnt.h>
#elif defined(__GLIBC__)
extern "C" void* __libc_stack_end;  // NOLINT
#endif

#if defined(MEMORY_SANITIZER)
#include <sanitizer/msan_interface.h>
#endif

#if OS(FREEBSD)
#include <pthread_np.h>
#endif

namespace blink {

WTF::ThreadSpecific<ThreadState*>* ThreadState::s_threadSpecific = nullptr;
uintptr_t ThreadState::s_mainThreadStackStart = 0;
uintptr_t ThreadState::s_mainThreadUnderestimatedStackSize = 0;
uint8_t ThreadState::s_mainThreadStateStorage[sizeof(ThreadState)];
SafePointBarrier* ThreadState::s_safePointBarrier = nullptr;

RecursiveMutex& ThreadState::threadAttachMutex()
{
    AtomicallyInitializedStaticReference(RecursiveMutex, mutex, (new RecursiveMutex));
    return mutex;
}

ThreadState::ThreadState()
    : m_thread(currentThread())
    , m_persistentRegion(adoptPtr(new PersistentRegion()))
#if OS(WIN) && COMPILER(MSVC)
    , m_threadStackSize(0)
#endif
    , m_startOfStack(reinterpret_cast<intptr_t*>(StackFrameDepth::getStackStart()))
    , m_endOfStack(reinterpret_cast<intptr_t*>(StackFrameDepth::getStackStart()))
    , m_safePointScopeMarker(nullptr)
    , m_atSafePoint(false)
    , m_interruptors()
    , m_sweepForbidden(false)
    , m_noAllocationCount(0)
    , m_gcForbiddenCount(0)
    , m_accumulatedSweepingTime(0)
    , m_vectorBackingHeapIndex(BlinkGC::Vector1HeapIndex)
    , m_currentHeapAges(0)
    , m_isTerminating(false)
    , m_gcMixinMarker(nullptr)
    , m_shouldFlushHeapDoesNotContainCache(false)
    , m_gcState(NoGCScheduled)
    , m_traceDOMWrappers(nullptr)
#if defined(ADDRESS_SANITIZER)
    , m_asanFakeStack(__asan_get_current_fake_stack())
#endif
{
    ASSERT(checkThread());
    ASSERT(!**s_threadSpecific);
    **s_threadSpecific = this;

    if (isMainThread()) {
        s_mainThreadStackStart = reinterpret_cast<uintptr_t>(m_startOfStack) - sizeof(void*);
        size_t underestimatedStackSize = StackFrameDepth::getUnderestimatedStackSize();
        if (underestimatedStackSize > sizeof(void*))
            s_mainThreadUnderestimatedStackSize = underestimatedStackSize - sizeof(void*);
    }

    for (int heapIndex = 0; heapIndex < BlinkGC::LargeObjectHeapIndex; heapIndex++)
        m_heaps[heapIndex] = new NormalPageHeap(this, heapIndex);
    m_heaps[BlinkGC::LargeObjectHeapIndex] = new LargeObjectHeap(this, BlinkGC::LargeObjectHeapIndex);

    m_likelyToBePromptlyFreed = adoptArrayPtr(new int[likelyToBePromptlyFreedArraySize]);
    clearHeapAges();

    m_threadLocalWeakCallbackStack = new CallbackStack();
}

ThreadState::~ThreadState()
{
    ASSERT(checkThread());
    delete m_threadLocalWeakCallbackStack;
    m_threadLocalWeakCallbackStack = nullptr;
    for (int i = 0; i < BlinkGC::NumberOfHeaps; ++i)
        delete m_heaps[i];

    **s_threadSpecific = nullptr;
    if (isMainThread()) {
        s_mainThreadStackStart = 0;
        s_mainThreadUnderestimatedStackSize = 0;
    }
}

void ThreadState::init()
{
    s_threadSpecific = new WTF::ThreadSpecific<ThreadState*>();
    s_safePointBarrier = new SafePointBarrier;
}

void ThreadState::shutdown()
{
    delete s_safePointBarrier;
    s_safePointBarrier = nullptr;

    // Thread-local storage shouldn't be disposed, so we don't call ~ThreadSpecific().
}

#if OS(WIN) && COMPILER(MSVC)
size_t ThreadState::threadStackSize()
{
    if (m_threadStackSize)
        return m_threadStackSize;

    // Notice that we cannot use the TIB's StackLimit for the stack end, as it
    // tracks the end of the committed range. We're after the end of the reserved
    // stack area (most of which will be uncommitted, most times.)
    MEMORY_BASIC_INFORMATION stackInfo;
    memset(&stackInfo, 0, sizeof(MEMORY_BASIC_INFORMATION));
    size_t resultSize = VirtualQuery(&stackInfo, &stackInfo, sizeof(MEMORY_BASIC_INFORMATION));
    ASSERT_UNUSED(resultSize, resultSize >= sizeof(MEMORY_BASIC_INFORMATION));
    Address stackEnd = reinterpret_cast<Address>(stackInfo.AllocationBase);

    Address stackStart = reinterpret_cast<Address>(StackFrameDepth::getStackStart());
    RELEASE_ASSERT(stackStart && stackStart > stackEnd);
    m_threadStackSize = static_cast<size_t>(stackStart - stackEnd);
    // When the third last page of the reserved stack is accessed as a
    // guard page, the second last page will be committed (along with removing
    // the guard bit on the third last) _and_ a stack overflow exception
    // is raised.
    //
    // We have zero interest in running into stack overflow exceptions while
    // marking objects, so simply consider the last three pages + one above
    // as off-limits and adjust the reported stack size accordingly.
    //
    // http://blogs.msdn.com/b/satyem/archive/2012/08/13/thread-s-stack-memory-management.aspx
    // explains the details.
    RELEASE_ASSERT(m_threadStackSize > 4 * 0x1000);
    m_threadStackSize -= 4 * 0x1000;
    return m_threadStackSize;
}
#endif

void ThreadState::attachMainThread()
{
    RELEASE_ASSERT(!Heap::s_shutdownCalled);
    MutexLocker locker(threadAttachMutex());
    ThreadState* state = new(s_mainThreadStateStorage) ThreadState();
    attachedThreads().add(state);
}

void ThreadState::detachMainThread()
{
    // Enter a safe point before trying to acquire threadAttachMutex
    // to avoid dead lock if another thread is preparing for GC, has acquired
    // threadAttachMutex and waiting for other threads to pause or reach a
    // safepoint.
    ThreadState* state = mainThreadState();

    // 1. Finish sweeping.
    state->completeSweep();
    {
        SafePointAwareMutexLocker locker(threadAttachMutex(), BlinkGC::NoHeapPointersOnStack);

        // 2. Add the main thread's heap pages to the orphaned pool.
        state->cleanupPages();

        // 3. Detach the main thread.
        ASSERT(attachedThreads().contains(state));
        attachedThreads().remove(state);
        state->~ThreadState();
    }
    shutdownHeapIfNecessary();
}

void ThreadState::shutdownHeapIfNecessary()
{
    // We don't need to enter a safe point before acquiring threadAttachMutex
    // because this thread is already detached.

    MutexLocker locker(threadAttachMutex());
    // We start shutting down the heap if there is no running thread
    // and Heap::shutdown() is already called.
    if (!attachedThreads().size() && Heap::s_shutdownCalled)
        Heap::doShutdown();
}

void ThreadState::attach()
{
    RELEASE_ASSERT(!Heap::s_shutdownCalled);
    MutexLocker locker(threadAttachMutex());
    ThreadState* state = new ThreadState();
    attachedThreads().add(state);
}

void ThreadState::cleanupPages()
{
    ASSERT(checkThread());
    for (int i = 0; i < BlinkGC::NumberOfHeaps; ++i)
        m_heaps[i]->cleanupPages();
}

void ThreadState::cleanup()
{
    ASSERT(checkThread());
    {
        // Grab the threadAttachMutex to ensure only one thread can shutdown at
        // a time and that no other thread can do a global GC. It also allows
        // safe iteration of the attachedThreads set which happens as part of
        // thread local GC asserts. We enter a safepoint while waiting for the
        // lock to avoid a dead-lock where another thread has already requested
        // GC.
        SafePointAwareMutexLocker locker(threadAttachMutex(), BlinkGC::NoHeapPointersOnStack);

        // Finish sweeping.
        completeSweep();

        // From here on ignore all conservatively discovered
        // pointers into the heap owned by this thread.
        m_isTerminating = true;

        // Set the terminate flag on all heap pages of this thread. This is used to
        // ensure we don't trace pages on other threads that are not part of the
        // thread local GC.
        prepareForThreadStateTermination();

        ThreadState::crossThreadPersistentRegion().prepareForThreadStateTermination(this);

        // Do thread local GC's as long as the count of thread local Persistents
        // changes and is above zero.
        int oldCount = -1;
        int currentCount = persistentRegion()->numberOfPersistents();
        ASSERT(currentCount >= 0);
        while (currentCount != oldCount) {
            Heap::collectGarbageForTerminatingThread(this);
            oldCount = currentCount;
            currentCount = persistentRegion()->numberOfPersistents();
        }
        // We should not have any persistents left when getting to this point,
        // if we have it is probably a bug so adding a debug ASSERT to catch this.
        ASSERT(!currentCount);
        // All of pre-finalizers should be consumed.
        ASSERT(m_orderedPreFinalizers.isEmpty());
        RELEASE_ASSERT(gcState() == NoGCScheduled);

        // Add pages to the orphaned page pool to ensure any global GCs from this point
        // on will not trace objects on this thread's heaps.
        cleanupPages();

        ASSERT(attachedThreads().contains(this));
        attachedThreads().remove(this);
    }
}

void ThreadState::detach()
{
    ThreadState* state = current();
    state->cleanup();
    RELEASE_ASSERT(state->gcState() == ThreadState::NoGCScheduled);
    delete state;
    shutdownHeapIfNecessary();
}

void ThreadState::visitPersistentRoots(Visitor* visitor)
{
    TRACE_EVENT0("blink_gc", "ThreadState::visitPersistentRoots");
    crossThreadPersistentRegion().tracePersistentNodes(visitor);

    for (ThreadState* state : attachedThreads())
        state->visitPersistents(visitor);
}

void ThreadState::visitStackRoots(Visitor* visitor)
{
    TRACE_EVENT0("blink_gc", "ThreadState::visitStackRoots");
    for (ThreadState* state : attachedThreads())
        state->visitStack(visitor);
}

NO_SANITIZE_ADDRESS
void ThreadState::visitAsanFakeStackForPointer(Visitor* visitor, Address ptr)
{
#if defined(ADDRESS_SANITIZER)
    Address* start = reinterpret_cast<Address*>(m_startOfStack);
    Address* end = reinterpret_cast<Address*>(m_endOfStack);
    Address* fakeFrameStart = nullptr;
    Address* fakeFrameEnd = nullptr;
    Address* maybeFakeFrame = reinterpret_cast<Address*>(ptr);
    Address* realFrameForFakeFrame =
        reinterpret_cast<Address*>(
            __asan_addr_is_in_fake_stack(
                m_asanFakeStack, maybeFakeFrame,
                reinterpret_cast<void**>(&fakeFrameStart),
                reinterpret_cast<void**>(&fakeFrameEnd)));
    if (realFrameForFakeFrame) {
        // This is a fake frame from the asan fake stack.
        if (realFrameForFakeFrame > end && start > realFrameForFakeFrame) {
            // The real stack address for the asan fake frame is
            // within the stack range that we need to scan so we need
            // to visit the values in the fake frame.
            for (Address* p = fakeFrameStart; p < fakeFrameEnd; ++p)
                Heap::checkAndMarkPointer(visitor, *p);
        }
    }
#endif
}

NO_SANITIZE_ADDRESS
void ThreadState::visitStack(Visitor* visitor)
{
    if (m_stackState == BlinkGC::NoHeapPointersOnStack)
        return;

    Address* start = reinterpret_cast<Address*>(m_startOfStack);
    // If there is a safepoint scope marker we should stop the stack
    // scanning there to not touch active parts of the stack. Anything
    // interesting beyond that point is in the safepoint stack copy.
    // If there is no scope marker the thread is blocked and we should
    // scan all the way to the recorded end stack pointer.
    Address* end = reinterpret_cast<Address*>(m_endOfStack);
    Address* safePointScopeMarker = reinterpret_cast<Address*>(m_safePointScopeMarker);
    Address* current = safePointScopeMarker ? safePointScopeMarker : end;

    // Ensure that current is aligned by address size otherwise the loop below
    // will read past start address.
    current = reinterpret_cast<Address*>(reinterpret_cast<intptr_t>(current) & ~(sizeof(Address) - 1));

    for (; current < start; ++current) {
        Address ptr = *current;
#if defined(MEMORY_SANITIZER)
        // |ptr| may be uninitialized by design. Mark it as initialized to keep
        // MSan from complaining.
        // Note: it may be tempting to get rid of |ptr| and simply use |current|
        // here, but that would be incorrect. We intentionally use a local
        // variable because we don't want to unpoison the original stack.
        __msan_unpoison(&ptr, sizeof(ptr));
#endif
        Heap::checkAndMarkPointer(visitor, ptr);
        visitAsanFakeStackForPointer(visitor, ptr);
    }

    for (Address ptr : m_safePointStackCopy) {
#if defined(MEMORY_SANITIZER)
        // See the comment above.
        __msan_unpoison(&ptr, sizeof(ptr));
#endif
        Heap::checkAndMarkPointer(visitor, ptr);
        visitAsanFakeStackForPointer(visitor, ptr);
    }
}

void ThreadState::visitPersistents(Visitor* visitor)
{
    m_persistentRegion->tracePersistentNodes(visitor);
    if (m_traceDOMWrappers) {
        TRACE_EVENT0("blink_gc", "V8GCController::traceDOMWrappers");
        m_traceDOMWrappers(m_isolate, visitor);
    }
}

ThreadState::GCSnapshotInfo::GCSnapshotInfo(size_t numObjectTypes)
    : liveCount(Vector<int>(numObjectTypes))
    , deadCount(Vector<int>(numObjectTypes))
    , liveSize(Vector<size_t>(numObjectTypes))
    , deadSize(Vector<size_t>(numObjectTypes))
{
}

void ThreadState::pushThreadLocalWeakCallback(void* object, WeakCallback callback)
{
    CallbackStack::Item* slot = m_threadLocalWeakCallbackStack->allocateEntry();
    *slot = CallbackStack::Item(object, callback);
}

bool ThreadState::popAndInvokeThreadLocalWeakCallback(Visitor* visitor)
{
    ASSERT(checkThread());
    // For weak processing we should never reach orphaned pages since orphaned
    // pages are not traced and thus objects on those pages are never be
    // registered as objects on orphaned pages. We cannot assert this here since
    // we might have an off-heap collection. We assert it in
    // Heap::pushThreadLocalWeakCallback.
    if (CallbackStack::Item* item = m_threadLocalWeakCallbackStack->pop()) {
        // Note that the thread-local weak processing can be called for
        // an already dead object (for which isHeapObjectAlive(object) can
        // return false). This can happen in the following scenario:
        //
        // 1) Marking runs. A weak callback for an object X is registered
        //    to the thread that created the object X (say, thread P).
        // 2) Marking finishes. All other threads are resumed.
        // 3) The object X becomes unreachable.
        // 4) A next GC hits before the thread P wakes up.
        // 5) Marking runs. The object X is not marked.
        // 6) Marking finishes. All other threads are resumed.
        // 7) The thread P wakes up and invokes pending weak callbacks.
        //    The weak callback for the object X is called, but the object X
        //    is already dead.
        //
        // Even in this case, it is safe to access the object X in the weak
        // callback because it is not yet swept. It is completely wasteful
        // to invoke the weak callback for dead objects but it is just
        // wasteful and safe.
        //
        // TODO(Oilpan): Avoid calling weak callbacks for dead objects.
        // We can do that by checking isHeapObjectAlive(object) before
        // calling the weak callback, but in that case Callback::Item
        // needs to understand T*.
        item->call(visitor);
        return true;
    }
    return false;
}

void ThreadState::threadLocalWeakProcessing()
{
    ASSERT(checkThread());
    ASSERT(!sweepForbidden());
    TRACE_EVENT0("blink_gc", "ThreadState::threadLocalWeakProcessing");
    double startTime = WTF::currentTimeMS();

    SweepForbiddenScope forbiddenScope(this);
    if (isMainThread())
        ScriptForbiddenScope::enter();

    // Disallow allocation during weak processing.
    // It would be technically safe to allow allocations, but it is unsafe
    // to mutate an object graph in a way in which a dead object gets
    // resurrected or mutate a HashTable (because HashTable's weak processing
    // assumes that the HashTable hasn't been mutated since the latest marking).
    // Due to the complexity, we just forbid allocations.
    NoAllocationScope noAllocationScope(this);

    MarkingVisitor<Visitor::WeakProcessing> weakProcessingVisitor;

    // Perform thread-specific weak processing.
    while (popAndInvokeThreadLocalWeakCallback(&weakProcessingVisitor)) { }

    if (isMainThread()) {
        ScriptForbiddenScope::exit();
        double timeForThreadLocalWeakProcessing = WTF::currentTimeMS() - startTime;
        Platform::current()->histogramCustomCounts("BlinkGC.timeForThreadLocalWeakProcessing", timeForThreadLocalWeakProcessing, 1, 10 * 1000, 50);
    }
}

CrossThreadPersistentRegion& ThreadState::crossThreadPersistentRegion()
{
    AtomicallyInitializedStaticReference(CrossThreadPersistentRegion, persistentRegion, new CrossThreadPersistentRegion());
    return persistentRegion;
}

size_t ThreadState::totalMemorySize()
{
    return Heap::allocatedObjectSize() + Heap::markedObjectSize() + WTF::Partitions::totalSizeOfCommittedPages();
}

size_t ThreadState::estimatedLiveSize(size_t estimationBaseSize, size_t sizeAtLastGC)
{
    if (Heap::wrapperCountAtLastGC() == 0) {
        // We'll reach here only before hitting the first GC.
        return 0;
    }

    // (estimated size) = (estimation base size) - (heap size at the last GC) / (# of persistent handles at the last GC) * (# of persistent handles collected since the last GC);
    size_t sizeRetainedByCollectedPersistents = static_cast<size_t>(1.0 * sizeAtLastGC / Heap::wrapperCountAtLastGC() * Heap::collectedWrapperCount());
    if (estimationBaseSize < sizeRetainedByCollectedPersistents)
        return 0;
    return estimationBaseSize - sizeRetainedByCollectedPersistents;
}

double ThreadState::heapGrowingRate()
{
    size_t currentSize = Heap::allocatedObjectSize() + Heap::markedObjectSize();
    size_t estimatedSize = estimatedLiveSize(Heap::markedObjectSizeAtLastCompleteSweep(), Heap::markedObjectSizeAtLastCompleteSweep());

    // If the estimatedSize is 0, we set a high growing rate to trigger a GC.
    double growingRate = estimatedSize > 0 ? 1.0 * currentSize / estimatedSize : 100;
    TRACE_COUNTER1(TRACE_DISABLED_BY_DEFAULT("blink_gc"), "ThreadState::heapEstimatedSizeKB", std::min(estimatedSize / 1024, static_cast<size_t>(INT_MAX)));
    TRACE_COUNTER1(TRACE_DISABLED_BY_DEFAULT("blink_gc"), "ThreadState::heapGrowingRate", static_cast<int>(100 * growingRate));
    return growingRate;
}

double ThreadState::partitionAllocGrowingRate()
{
    size_t currentSize = WTF::Partitions::totalSizeOfCommittedPages();
    size_t estimatedSize = estimatedLiveSize(currentSize, Heap::partitionAllocSizeAtLastGC());

    // If the estimatedSize is 0, we set a high growing rate to trigger a GC.
    double growingRate = estimatedSize > 0 ? 1.0 * currentSize / estimatedSize : 100;
    TRACE_COUNTER1(TRACE_DISABLED_BY_DEFAULT("blink_gc"), "ThreadState::partitionAllocEstimatedSizeKB", std::min(estimatedSize / 1024, static_cast<size_t>(INT_MAX)));
    TRACE_COUNTER1(TRACE_DISABLED_BY_DEFAULT("blink_gc"), "ThreadState::partitionAllocGrowingRate", static_cast<int>(100 * growingRate));
    return growingRate;
}

// TODO(haraken): We should improve the GC heuristics. The heuristics affect
// performance significantly.
bool ThreadState::judgeGCThreshold(size_t totalMemorySizeThreshold, double heapGrowingRateThreshold)
{
    // If the allocated object size or the total memory size is small, don't trigger a GC.
    if (Heap::allocatedObjectSize() < 100 * 1024 || totalMemorySize() < totalMemorySizeThreshold)
        return false;
    // If the growing rate of Oilpan's heap or PartitionAlloc is high enough,
    // trigger a GC.
#if PRINT_HEAP_STATS
    dataLogF("heapGrowingRate=%.1lf, partitionAllocGrowingRate=%.1lf\n", heapGrowingRate(), partitionAllocGrowingRate());
#endif
    return heapGrowingRate() >= heapGrowingRateThreshold || partitionAllocGrowingRate() >= heapGrowingRateThreshold;
}

bool ThreadState::shouldScheduleIdleGC()
{
    if (gcState() != NoGCScheduled)
        return false;
    return judgeGCThreshold(1024 * 1024, 1.5);
}

bool ThreadState::shouldScheduleV8FollowupGC()
{
    return judgeGCThreshold(32 * 1024 * 1024, 1.5);
}

bool ThreadState::shouldSchedulePageNavigationGC(float estimatedRemovalRatio)
{
    return judgeGCThreshold(1024 * 1024, 1.5 * (1 - estimatedRemovalRatio));
}

bool ThreadState::shouldForceConservativeGC()
{
    // TODO(haraken): 400% is too large. Lower the heap growing factor.
    return judgeGCThreshold(32 * 1024 * 1024, 5.0);
}

// If we're consuming too much memory, trigger a conservative GC
// aggressively. This is a safe guard to avoid OOM.
bool ThreadState::shouldForceMemoryPressureGC()
{
    if (totalMemorySize() < 300 * 1024 * 1024)
        return false;
    return judgeGCThreshold(0, 1.5);
}

void ThreadState::scheduleV8FollowupGCIfNeeded(BlinkGC::V8GCType gcType)
{
    ASSERT(checkThread());
    Heap::reportMemoryUsageForTracing();

#if PRINT_HEAP_STATS
    dataLogF("ThreadState::scheduleV8FollowupGCIfNeeded (gcType=%s)\n", gcType == BlinkGC::V8MajorGC ? "MajorGC" : "MinorGC");
#endif

    if (isGCForbidden())
        return;

    // This completeSweep() will do nothing in common cases since we've
    // called completeSweep() before V8 starts minor/major GCs.
    completeSweep();
    ASSERT(!isSweepingInProgress());
    ASSERT(!sweepForbidden());

    // TODO(haraken): Consider if we should trigger a memory pressure GC
    // for V8 minor GCs as well.
    if (gcType == BlinkGC::V8MajorGC && shouldForceMemoryPressureGC()) {
#if PRINT_HEAP_STATS
        dataLogF("Scheduled MemoryPressureGC\n");
#endif
        Heap::collectGarbage(BlinkGC::HeapPointersOnStack, BlinkGC::GCWithoutSweep, BlinkGC::MemoryPressureGC);
        return;
    }
    if (shouldScheduleV8FollowupGC()) {
#if PRINT_HEAP_STATS
        dataLogF("Scheduled PreciseGC\n");
#endif
        schedulePreciseGC();
        return;
    }
    if (gcType == BlinkGC::V8MajorGC) {
#if PRINT_HEAP_STATS
        dataLogF("Scheduled IdleGC\n");
#endif
        scheduleIdleGC();
        return;
    }
}

void ThreadState::willStartV8GC()
{
    // Finish Oilpan's complete sweeping before running a V8 GC.
    // This will let the GC collect more V8 objects.
    //
    // TODO(haraken): It's a bit too late for a major GC to schedule
    // completeSweep() here, because gcPrologue for a major GC is called
    // not at the point where the major GC started but at the point where
    // the major GC requests object grouping.
    completeSweep();

    // The fact that the PageNavigation GC is scheduled means that there is
    // a dead frame. In common cases, a sequence of Oilpan's GC => V8 GC =>
    // Oilpan's GC is needed to collect the dead frame. So we force the
    // PageNavigation GC before running the V8 GC.
    if (gcState() == PageNavigationGCScheduled) {
#if PRINT_HEAP_STATS
        dataLogF("Scheduled PageNavigationGC\n");
#endif
        Heap::collectGarbage(BlinkGC::HeapPointersOnStack, BlinkGC::GCWithSweep, BlinkGC::PageNavigationGC);
    }
}

void ThreadState::schedulePageNavigationGCIfNeeded(float estimatedRemovalRatio)
{
    ASSERT(checkThread());
    Heap::reportMemoryUsageForTracing();

#if PRINT_HEAP_STATS
    dataLogF("ThreadState::schedulePageNavigationGCIfNeeded (estimatedRemovalRatio=%.2lf)\n", estimatedRemovalRatio);
#endif

    if (isGCForbidden())
        return;

    // Finish on-going lazy sweeping.
    // TODO(haraken): It might not make sense to force completeSweep() for all
    // page navigations.
    completeSweep();
    ASSERT(!isSweepingInProgress());
    ASSERT(!sweepForbidden());

    if (shouldForceMemoryPressureGC()) {
#if PRINT_HEAP_STATS
        dataLogF("Scheduled MemoryPressureGC\n");
#endif
        Heap::collectGarbage(BlinkGC::HeapPointersOnStack, BlinkGC::GCWithoutSweep, BlinkGC::MemoryPressureGC);
        return;
    }
    if (shouldSchedulePageNavigationGC(estimatedRemovalRatio)) {
#if PRINT_HEAP_STATS
        dataLogF("Scheduled PageNavigationGC\n");
#endif
        schedulePageNavigationGC();
        return;
    }
}

void ThreadState::schedulePageNavigationGC()
{
    ASSERT(checkThread());
    ASSERT(!isSweepingInProgress());
    setGCState(PageNavigationGCScheduled);
}

void ThreadState::scheduleGCIfNeeded()
{
    ASSERT(checkThread());
    Heap::reportMemoryUsageForTracing();

#if PRINT_HEAP_STATS
    dataLogF("ThreadState::scheduleGCIfNeeded\n");
#endif

    // Allocation is allowed during sweeping, but those allocations should not
    // trigger nested GCs.
    if (isGCForbidden())
        return;

    if (isSweepingInProgress())
        return;
    ASSERT(!sweepForbidden());

    if (shouldForceMemoryPressureGC()) {
        completeSweep();
        if (shouldForceMemoryPressureGC()) {
#if PRINT_HEAP_STATS
            dataLogF("Scheduled MemoryPressureGC\n");
#endif
            Heap::collectGarbage(BlinkGC::HeapPointersOnStack, BlinkGC::GCWithoutSweep, BlinkGC::MemoryPressureGC);
            return;
        }
    }

    if (shouldForceConservativeGC()) {
        completeSweep();
        if (shouldForceConservativeGC()) {
#if PRINT_HEAP_STATS
            dataLogF("Scheduled ConservativeGC\n");
#endif
            Heap::collectGarbage(BlinkGC::HeapPointersOnStack, BlinkGC::GCWithoutSweep, BlinkGC::ConservativeGC);
            return;
        }
    }
    if (shouldScheduleIdleGC()) {
#if PRINT_HEAP_STATS
        dataLogF("Scheduled IdleGC\n");
#endif
        scheduleIdleGC();
        return;
    }
}

void ThreadState::performIdleGC(double deadlineSeconds)
{
    ASSERT(checkThread());
    ASSERT(isMainThread());

    if (gcState() != IdleGCScheduled)
        return;

    double idleDeltaInSeconds = deadlineSeconds - Platform::current()->monotonicallyIncreasingTimeSeconds();
    TRACE_EVENT2("blink_gc", "ThreadState::performIdleGC", "idleDeltaInSeconds", idleDeltaInSeconds, "estimatedMarkingTime", Heap::estimatedMarkingTime());
    if (idleDeltaInSeconds <= Heap::estimatedMarkingTime() && !Platform::current()->currentThread()->scheduler()->canExceedIdleDeadlineIfRequired()) {
        // If marking is estimated to take longer than the deadline and we can't
        // exceed the deadline, then reschedule for the next idle period.
        scheduleIdleGC();
        return;
    }

    Heap::collectGarbage(BlinkGC::NoHeapPointersOnStack, BlinkGC::GCWithoutSweep, BlinkGC::IdleGC);
}

void ThreadState::performIdleLazySweep(double deadlineSeconds)
{
    ASSERT(checkThread());
    ASSERT(isMainThread());

    // If we are not in a sweeping phase, there is nothing to do here.
    if (!isSweepingInProgress())
        return;

    // This check is here to prevent performIdleLazySweep() from being called
    // recursively. I'm not sure if it can happen but it would be safer to have
    // the check just in case.
    if (sweepForbidden())
        return;

    TRACE_EVENT1("blink_gc", "ThreadState::performIdleLazySweep", "idleDeltaInSeconds", deadlineSeconds - Platform::current()->monotonicallyIncreasingTimeSeconds());

    bool sweepCompleted = true;
    SweepForbiddenScope scope(this);
    {
        double startTime = WTF::currentTimeMS();
        if (isMainThread())
            ScriptForbiddenScope::enter();

        for (int i = 0; i < BlinkGC::NumberOfHeaps; i++) {
            // lazySweepWithDeadline() won't check the deadline until it sweeps
            // 10 pages. So we give a small slack for safety.
            double slack = 0.001;
            double remainingBudget = deadlineSeconds - slack - Platform::current()->monotonicallyIncreasingTimeSeconds();
            if (remainingBudget <= 0 || !m_heaps[i]->lazySweepWithDeadline(deadlineSeconds)) {
                // We couldn't finish the sweeping within the deadline.
                // We request another idle task for the remaining sweeping.
                scheduleIdleLazySweep();
                sweepCompleted = false;
                break;
            }
        }

        if (isMainThread())
            ScriptForbiddenScope::exit();
        accumulateSweepingTime(WTF::currentTimeMS() - startTime);
    }

    if (sweepCompleted)
        postSweep();
}

void ThreadState::scheduleIdleGC()
{
    // TODO(haraken): Idle GC should be supported in worker threads as well.
    if (!isMainThread())
        return;

    if (isSweepingInProgress()) {
        setGCState(SweepingAndIdleGCScheduled);
        return;
    }

    Platform::current()->currentThread()->scheduler()->postNonNestableIdleTask(BLINK_FROM_HERE, WTF::bind<double>(&ThreadState::performIdleGC, this));
    setGCState(IdleGCScheduled);
}

void ThreadState::scheduleIdleLazySweep()
{
    // TODO(haraken): Idle complete sweep should be supported in worker threads.
    if (!isMainThread())
        return;

    Platform::current()->currentThread()->scheduler()->postIdleTask(BLINK_FROM_HERE, WTF::bind<double>(&ThreadState::performIdleLazySweep, this));
}

void ThreadState::schedulePreciseGC()
{
    ASSERT(checkThread());
    if (isSweepingInProgress()) {
        setGCState(SweepingAndPreciseGCScheduled);
        return;
    }

    setGCState(PreciseGCScheduled);
}

namespace {

#define UNEXPECTED_GCSTATE(s) case ThreadState::s: RELEASE_ASSERT_WITH_MESSAGE(false, "Unexpected transition while in GCState " #s); return

void unexpectedGCState(ThreadState::GCState gcState)
{
    switch (gcState) {
        UNEXPECTED_GCSTATE(NoGCScheduled);
        UNEXPECTED_GCSTATE(IdleGCScheduled);
        UNEXPECTED_GCSTATE(PreciseGCScheduled);
        UNEXPECTED_GCSTATE(FullGCScheduled);
        UNEXPECTED_GCSTATE(GCRunning);
        UNEXPECTED_GCSTATE(EagerSweepScheduled);
        UNEXPECTED_GCSTATE(LazySweepScheduled);
        UNEXPECTED_GCSTATE(Sweeping);
        UNEXPECTED_GCSTATE(SweepingAndIdleGCScheduled);
        UNEXPECTED_GCSTATE(SweepingAndPreciseGCScheduled);
    default:
        ASSERT_NOT_REACHED();
        return;
    }
}

#undef UNEXPECTED_GCSTATE

} // namespace

#define VERIFY_STATE_TRANSITION(condition) if (UNLIKELY(!(condition))) unexpectedGCState(m_gcState)

void ThreadState::setGCState(GCState gcState)
{
    switch (gcState) {
    case NoGCScheduled:
        ASSERT(checkThread());
        VERIFY_STATE_TRANSITION(m_gcState == Sweeping || m_gcState == SweepingAndIdleGCScheduled);
        break;
    case IdleGCScheduled:
    case PreciseGCScheduled:
    case FullGCScheduled:
    case PageNavigationGCScheduled:
        ASSERT(checkThread());
        VERIFY_STATE_TRANSITION(m_gcState == NoGCScheduled || m_gcState == IdleGCScheduled || m_gcState == PreciseGCScheduled || m_gcState == FullGCScheduled || m_gcState == PageNavigationGCScheduled || m_gcState == SweepingAndIdleGCScheduled || m_gcState == SweepingAndPreciseGCScheduled);
        completeSweep();
        break;
    case GCRunning:
        ASSERT(!isInGC());
        VERIFY_STATE_TRANSITION(m_gcState != GCRunning);
        break;
    case EagerSweepScheduled:
    case LazySweepScheduled:
        ASSERT(isInGC());
        VERIFY_STATE_TRANSITION(m_gcState == GCRunning);
        break;
    case Sweeping:
        ASSERT(checkThread());
        VERIFY_STATE_TRANSITION(m_gcState == EagerSweepScheduled || m_gcState == LazySweepScheduled);
        break;
    case SweepingAndIdleGCScheduled:
    case SweepingAndPreciseGCScheduled:
        ASSERT(checkThread());
        VERIFY_STATE_TRANSITION(m_gcState == Sweeping || m_gcState == SweepingAndIdleGCScheduled || m_gcState == SweepingAndPreciseGCScheduled);
        break;
    default:
        ASSERT_NOT_REACHED();
    }
    m_gcState = gcState;
}

#undef VERIFY_STATE_TRANSITION

void ThreadState::runScheduledGC(BlinkGC::StackState stackState)
{
    ASSERT(checkThread());
    if (stackState != BlinkGC::NoHeapPointersOnStack)
        return;

    // If a safe point is entered while initiating a GC, we clearly do
    // not want to do another as part that -- the safe point is only
    // entered after checking if a scheduled GC ought to run first.
    // Prevent that from happening by marking GCs as forbidden while
    // one is initiated and later running.
    if (isGCForbidden())
        return;

    switch (gcState()) {
    case FullGCScheduled:
        Heap::collectAllGarbage();
        break;
    case PreciseGCScheduled:
        Heap::collectGarbage(BlinkGC::NoHeapPointersOnStack, BlinkGC::GCWithoutSweep, BlinkGC::PreciseGC);
        break;
    case PageNavigationGCScheduled:
        Heap::collectGarbage(BlinkGC::NoHeapPointersOnStack, BlinkGC::GCWithSweep, BlinkGC::PageNavigationGC);
        break;
    case IdleGCScheduled:
        // Idle time GC will be scheduled by Blink Scheduler.
        break;
    default:
        break;
    }
}

void ThreadState::flushHeapDoesNotContainCacheIfNeeded()
{
    if (m_shouldFlushHeapDoesNotContainCache) {
        Heap::flushHeapDoesNotContainCache();
        m_shouldFlushHeapDoesNotContainCache = false;
    }
}

void ThreadState::makeConsistentForGC()
{
    ASSERT(isInGC());
    TRACE_EVENT0("blink_gc", "ThreadState::makeConsistentForGC");
    for (int i = 0; i < BlinkGC::NumberOfHeaps; ++i)
        m_heaps[i]->makeConsistentForGC();
}

void ThreadState::makeConsistentForMutator()
{
    ASSERT(isInGC());
    for (int i = 0; i < BlinkGC::NumberOfHeaps; ++i)
        m_heaps[i]->makeConsistentForMutator();
}

void ThreadState::preGC()
{
    ASSERT(!isInGC());
    setGCState(GCRunning);
    makeConsistentForGC();
    flushHeapDoesNotContainCacheIfNeeded();
    clearHeapAges();
}

void ThreadState::postGC(BlinkGC::GCType gcType)
{
    ASSERT(isInGC());
    for (int i = 0; i < BlinkGC::NumberOfHeaps; i++)
        m_heaps[i]->prepareForSweep();

    if (gcType == BlinkGC::GCWithSweep) {
        setGCState(EagerSweepScheduled);
    } else if (gcType == BlinkGC::GCWithoutSweep) {
        setGCState(LazySweepScheduled);
    } else {
        takeSnapshot(SnapshotType::HeapSnapshot);

        // This unmarks all marked objects and marks all unmarked objects dead.
        makeConsistentForMutator();

        takeSnapshot(SnapshotType::FreelistSnapshot);

        // Force setting NoGCScheduled to circumvent checkThread()
        // in setGCState().
        m_gcState = NoGCScheduled;
    }
}

void ThreadState::preSweep()
{
    ASSERT(checkThread());
    if (gcState() != EagerSweepScheduled && gcState() != LazySweepScheduled)
        return;

    threadLocalWeakProcessing();

    GCState previousGCState = gcState();
    // We have to set the GCState to Sweeping before calling pre-finalizers
    // to disallow a GC during the pre-finalizers.
    setGCState(Sweeping);

    // Allocation is allowed during the pre-finalizers and destructors.
    // However, they must not mutate an object graph in a way in which
    // a dead object gets resurrected.
    invokePreFinalizers();

    m_accumulatedSweepingTime = 0;

#if defined(ADDRESS_SANITIZER)
    poisonEagerHeap(BlinkGC::SetPoison);
#endif
    eagerSweep();
#if defined(ADDRESS_SANITIZER)
    poisonAllHeaps();
#endif

    if (previousGCState == EagerSweepScheduled) {
        // Eager sweeping should happen only in testing.
        completeSweep();
    } else {
        // The default behavior is lazy sweeping.
        scheduleIdleLazySweep();
    }
}

#if defined(ADDRESS_SANITIZER)
void ThreadState::poisonAllHeaps()
{
    // TODO(Oilpan): enable the poisoning always.
#if ENABLE(OILPAN)
    // Unpoison the live objects remaining in the eager heaps..
    poisonEagerHeap(BlinkGC::ClearPoison);
    // ..along with poisoning all unmarked objects in the other heaps.
    for (int i = 1; i < BlinkGC::NumberOfHeaps; i++)
        m_heaps[i]->poisonHeap(BlinkGC::UnmarkedOnly, BlinkGC::SetPoison);
#endif
}

void ThreadState::poisonEagerHeap(BlinkGC::Poisoning poisoning)
{
    // TODO(Oilpan): enable the poisoning always.
#if ENABLE(OILPAN)
    m_heaps[BlinkGC::EagerSweepHeapIndex]->poisonHeap(BlinkGC::MarkedAndUnmarked, poisoning);
#endif
}
#endif

void ThreadState::eagerSweep()
{
    ASSERT(checkThread());
    // Some objects need to be finalized promptly and cannot be handled
    // by lazy sweeping. Keep those in a designated heap and sweep it
    // eagerly.
    ASSERT(isSweepingInProgress());

    // Mirroring the completeSweep() condition; see its comment.
    if (sweepForbidden())
        return;

    SweepForbiddenScope scope(this);
    {
        double startTime = WTF::currentTimeMS();
        if (isMainThread())
            ScriptForbiddenScope::enter();

        m_heaps[BlinkGC::EagerSweepHeapIndex]->completeSweep();

        if (isMainThread())
            ScriptForbiddenScope::exit();
        accumulateSweepingTime(WTF::currentTimeMS() - startTime);
    }
}

void ThreadState::completeSweep()
{
    ASSERT(checkThread());
    // If we are not in a sweeping phase, there is nothing to do here.
    if (!isSweepingInProgress())
        return;

    // completeSweep() can be called recursively if finalizers can allocate
    // memory and the allocation triggers completeSweep(). This check prevents
    // the sweeping from being executed recursively.
    if (sweepForbidden())
        return;

    SweepForbiddenScope scope(this);
    {
        if (isMainThread())
            ScriptForbiddenScope::enter();

        TRACE_EVENT0("blink_gc", "ThreadState::completeSweep");
        double startTime = WTF::currentTimeMS();

        static_assert(BlinkGC::EagerSweepHeapIndex == 0, "Eagerly swept heaps must be processed first.");
        for (int i = 0; i < BlinkGC::NumberOfHeaps; i++)
            m_heaps[i]->completeSweep();

        double timeForCompleteSweep = WTF::currentTimeMS() - startTime;
        accumulateSweepingTime(timeForCompleteSweep);

        if (isMainThread()) {
            ScriptForbiddenScope::exit();
            Platform::current()->histogramCustomCounts("BlinkGC.CompleteSweep", timeForCompleteSweep, 1, 10 * 1000, 50);
        }
    }

    postSweep();
}

void ThreadState::postSweep()
{
    ASSERT(checkThread());
    Heap::reportMemoryUsageForTracing();

    if (isMainThread()) {
        double collectionRate = 0;
        if (Heap::objectSizeAtLastGC() > 0)
            collectionRate = 1 - 1.0 * Heap::markedObjectSize() / Heap::objectSizeAtLastGC();
        TRACE_COUNTER1(TRACE_DISABLED_BY_DEFAULT("blink_gc"), "ThreadState::collectionRate", static_cast<int>(100 * collectionRate));

#if PRINT_HEAP_STATS
        dataLogF("ThreadState::postSweep (collectionRate=%d%%)\n", static_cast<int>(100 * collectionRate));
#endif

        // Heap::markedObjectSize() may be underestimated here if any other
        // thread has not yet finished lazy sweeping.
        Heap::setMarkedObjectSizeAtLastCompleteSweep(Heap::markedObjectSize());

        Platform::current()->histogramCustomCounts("BlinkGC.ObjectSizeBeforeGC", Heap::objectSizeAtLastGC() / 1024, 1, 4 * 1024 * 1024, 50);
        Platform::current()->histogramCustomCounts("BlinkGC.ObjectSizeAfterGC", Heap::markedObjectSize() / 1024, 1, 4 * 1024 * 1024, 50);
        Platform::current()->histogramCustomCounts("BlinkGC.CollectionRate", static_cast<int>(100 * collectionRate), 1, 100, 20);
        Platform::current()->histogramCustomCounts("BlinkGC.TimeForSweepingAllObjects", m_accumulatedSweepingTime, 1, 10 * 1000, 50);
    }

    switch (gcState()) {
    case Sweeping:
        setGCState(NoGCScheduled);
        break;
    case SweepingAndPreciseGCScheduled:
        setGCState(PreciseGCScheduled);
        break;
    case SweepingAndIdleGCScheduled:
        setGCState(NoGCScheduled);
        scheduleIdleGC();
        break;
    default:
        ASSERT_NOT_REACHED();
    }
}

void ThreadState::prepareForThreadStateTermination()
{
    ASSERT(checkThread());
    for (int i = 0; i < BlinkGC::NumberOfHeaps; ++i)
        m_heaps[i]->prepareHeapForTermination();
}

#if ENABLE(ASSERT)
BasePage* ThreadState::findPageFromAddress(Address address)
{
    for (int i = 0; i < BlinkGC::NumberOfHeaps; ++i) {
        if (BasePage* page = m_heaps[i]->findPageFromAddress(address))
            return page;
    }
    return nullptr;
}
#endif

size_t ThreadState::objectPayloadSizeForTesting()
{
    size_t objectPayloadSize = 0;
    for (int i = 0; i < BlinkGC::NumberOfHeaps; ++i)
        objectPayloadSize += m_heaps[i]->objectPayloadSizeForTesting();
    return objectPayloadSize;
}

bool ThreadState::stopThreads()
{
    return s_safePointBarrier->parkOthers();
}

void ThreadState::resumeThreads()
{
    s_safePointBarrier->resumeOthers();
}

void ThreadState::safePoint(BlinkGC::StackState stackState)
{
    ASSERT(checkThread());
    Heap::reportMemoryUsageForTracing();

    runScheduledGC(stackState);
    ASSERT(!m_atSafePoint);
    m_stackState = stackState;
    m_atSafePoint = true;
    s_safePointBarrier->checkAndPark(this);
    m_atSafePoint = false;
    m_stackState = BlinkGC::HeapPointersOnStack;
    preSweep();
}

#ifdef ADDRESS_SANITIZER
// When we are running under AddressSanitizer with detect_stack_use_after_return=1
// then stack marker obtained from SafePointScope will point into a fake stack.
// Detect this case by checking if it falls in between current stack frame
// and stack start and use an arbitrary high enough value for it.
// Don't adjust stack marker in any other case to match behavior of code running
// without AddressSanitizer.
NO_SANITIZE_ADDRESS static void* adjustScopeMarkerForAdressSanitizer(void* scopeMarker)
{
    Address start = reinterpret_cast<Address>(StackFrameDepth::getStackStart());
    Address end = reinterpret_cast<Address>(&start);
    RELEASE_ASSERT(end < start);

    if (end <= scopeMarker && scopeMarker < start)
        return scopeMarker;

    // 256 is as good an approximation as any else.
    const size_t bytesToCopy = sizeof(Address) * 256;
    if (static_cast<size_t>(start - end) < bytesToCopy)
        return start;

    return end + bytesToCopy;
}
#endif

void ThreadState::enterSafePoint(BlinkGC::StackState stackState, void* scopeMarker)
{
    ASSERT(checkThread());
#ifdef ADDRESS_SANITIZER
    if (stackState == BlinkGC::HeapPointersOnStack)
        scopeMarker = adjustScopeMarkerForAdressSanitizer(scopeMarker);
#endif
    ASSERT(stackState == BlinkGC::NoHeapPointersOnStack || scopeMarker);
    runScheduledGC(stackState);
    ASSERT(!m_atSafePoint);
    m_atSafePoint = true;
    m_stackState = stackState;
    m_safePointScopeMarker = scopeMarker;
    s_safePointBarrier->enterSafePoint(this);
}

void ThreadState::leaveSafePoint(SafePointAwareMutexLocker* locker)
{
    ASSERT(checkThread());
    ASSERT(m_atSafePoint);
    s_safePointBarrier->leaveSafePoint(this, locker);
    m_atSafePoint = false;
    m_stackState = BlinkGC::HeapPointersOnStack;
    clearSafePointScopeMarker();
    preSweep();
}

void ThreadState::copyStackUntilSafePointScope()
{
    if (!m_safePointScopeMarker || m_stackState == BlinkGC::NoHeapPointersOnStack)
        return;

    Address* to = reinterpret_cast<Address*>(m_safePointScopeMarker);
    Address* from = reinterpret_cast<Address*>(m_endOfStack);
    RELEASE_ASSERT(from < to);
    RELEASE_ASSERT(to <= reinterpret_cast<Address*>(m_startOfStack));
    size_t slotCount = static_cast<size_t>(to - from);
    // Catch potential performance issues.
#if defined(LEAK_SANITIZER) || defined(ADDRESS_SANITIZER)
    // ASan/LSan use more space on the stack and we therefore
    // increase the allowed stack copying for those builds.
    ASSERT(slotCount < 2048);
#else
    ASSERT(slotCount < 1024);
#endif

    ASSERT(!m_safePointStackCopy.size());
    m_safePointStackCopy.resize(slotCount);
    for (size_t i = 0; i < slotCount; ++i) {
        m_safePointStackCopy[i] = from[i];
    }
}

void ThreadState::addInterruptor(PassOwnPtr<BlinkGCInterruptor> interruptor)
{
    ASSERT(checkThread());
    SafePointScope scope(BlinkGC::HeapPointersOnStack);
    {
        MutexLocker locker(threadAttachMutex());
        m_interruptors.append(interruptor);
    }
}

void ThreadState::removeInterruptor(BlinkGCInterruptor* interruptor)
{
    ASSERT(checkThread());
    SafePointScope scope(BlinkGC::HeapPointersOnStack);
    {
        MutexLocker locker(threadAttachMutex());
        size_t index = m_interruptors.find(interruptor);
        RELEASE_ASSERT(index != kNotFound);
        m_interruptors.remove(index);
    }
}

ThreadState::AttachedThreadStateSet& ThreadState::attachedThreads()
{
    DEFINE_STATIC_LOCAL(AttachedThreadStateSet, threads, ());
    return threads;
}

void ThreadState::lockThreadAttachMutex()
{
    threadAttachMutex().lock();
}

void ThreadState::unlockThreadAttachMutex()
{
    threadAttachMutex().unlock();
}

void ThreadState::invokePreFinalizers()
{
    ASSERT(checkThread());
    ASSERT(!sweepForbidden());
    TRACE_EVENT0("blink_gc", "ThreadState::invokePreFinalizers");
    double startTime = WTF::currentTimeMS();

    if (isMainThread())
        ScriptForbiddenScope::enter();

    SweepForbiddenScope forbiddenScope(this);
    Vector<PreFinalizer> deadPreFinalizers;
    // Call the pre-finalizers in the reverse order in which they
    // are registered.
    for (auto it = m_orderedPreFinalizers.rbegin(); it != m_orderedPreFinalizers.rend(); ++it) {
        if (!(it->second)(it->first))
            continue;
        deadPreFinalizers.append(*it);
    }
    // FIXME: removeAll is inefficient.  It can shrink repeatedly.
    m_orderedPreFinalizers.removeAll(deadPreFinalizers);

    if (isMainThread()) {
        ScriptForbiddenScope::exit();
        double timeForInvokingPreFinalizers = WTF::currentTimeMS() - startTime;
        Platform::current()->histogramCustomCounts("BlinkGC.TimeForInvokingPreFinalizers", timeForInvokingPreFinalizers, 1, 10 * 1000, 50);
    }
}

void ThreadState::clearHeapAges()
{
    memset(m_heapAges, 0, sizeof(size_t) * BlinkGC::NumberOfHeaps);
    memset(m_likelyToBePromptlyFreed.get(), 0, sizeof(int) * likelyToBePromptlyFreedArraySize);
    m_currentHeapAges = 0;
}

int ThreadState::heapIndexOfVectorHeapLeastRecentlyExpanded(int beginHeapIndex, int endHeapIndex)
{
    size_t minHeapAge = m_heapAges[beginHeapIndex];
    int heapIndexWithMinHeapAge = beginHeapIndex;
    for (int heapIndex = beginHeapIndex + 1; heapIndex <= endHeapIndex; heapIndex++) {
        if (m_heapAges[heapIndex] < minHeapAge) {
            minHeapAge = m_heapAges[heapIndex];
            heapIndexWithMinHeapAge = heapIndex;
        }
    }
    ASSERT(isVectorHeapIndex(heapIndexWithMinHeapAge));
    return heapIndexWithMinHeapAge;
}

BaseHeap* ThreadState::expandedVectorBackingHeap(size_t gcInfoIndex)
{
    ASSERT(checkThread());
    size_t entryIndex = gcInfoIndex & likelyToBePromptlyFreedArrayMask;
    --m_likelyToBePromptlyFreed[entryIndex];
    int heapIndex = m_vectorBackingHeapIndex;
    m_heapAges[heapIndex] = ++m_currentHeapAges;
    m_vectorBackingHeapIndex = heapIndexOfVectorHeapLeastRecentlyExpanded(BlinkGC::Vector1HeapIndex, BlinkGC::Vector4HeapIndex);
    return m_heaps[heapIndex];
}

void ThreadState::allocationPointAdjusted(int heapIndex)
{
    m_heapAges[heapIndex] = ++m_currentHeapAges;
    if (m_vectorBackingHeapIndex == heapIndex)
        m_vectorBackingHeapIndex = heapIndexOfVectorHeapLeastRecentlyExpanded(BlinkGC::Vector1HeapIndex, BlinkGC::Vector4HeapIndex);
}

void ThreadState::promptlyFreed(size_t gcInfoIndex)
{
    ASSERT(checkThread());
    size_t entryIndex = gcInfoIndex & likelyToBePromptlyFreedArrayMask;
    // See the comment in vectorBackingHeap() for why this is +3.
    m_likelyToBePromptlyFreed[entryIndex] += 3;
}

void ThreadState::takeSnapshot(SnapshotType type)
{
    ASSERT(isInGC());

    // 0 is used as index for freelist entries. Objects are indexed 1 to
    // gcInfoIndex.
    GCSnapshotInfo info(GCInfoTable::gcInfoIndex() + 1);
    String threadDumpName = String::format("blink_gc/thread_%lu", static_cast<unsigned long>(m_thread));
    const String heapsDumpName = threadDumpName + "/heaps";
    const String classesDumpName = threadDumpName + "/classes";

    int numberOfHeapsReported = 0;
#define SNAPSHOT_HEAP(HeapType)                                                                \
    {                                                                                          \
        numberOfHeapsReported++;                                                               \
        switch (type) {                                                                        \
        case SnapshotType::HeapSnapshot:                                                       \
            m_heaps[BlinkGC::HeapType##HeapIndex]->takeSnapshot(heapsDumpName + "/" #HeapType, info);   \
            break;                                                                             \
        case SnapshotType::FreelistSnapshot:                                                   \
            m_heaps[BlinkGC::HeapType##HeapIndex]->takeFreelistSnapshot(heapsDumpName + "/" #HeapType); \
            break;                                                                             \
        default:                                                                               \
            ASSERT_NOT_REACHED();                                                              \
        }                                                                                      \
    }

    SNAPSHOT_HEAP(NormalPage1);
    SNAPSHOT_HEAP(NormalPage2);
    SNAPSHOT_HEAP(NormalPage3);
    SNAPSHOT_HEAP(NormalPage4);
    SNAPSHOT_HEAP(EagerSweep);
    SNAPSHOT_HEAP(Vector1);
    SNAPSHOT_HEAP(Vector2);
    SNAPSHOT_HEAP(Vector3);
    SNAPSHOT_HEAP(Vector4);
    SNAPSHOT_HEAP(InlineVector);
    SNAPSHOT_HEAP(HashTable);
    SNAPSHOT_HEAP(LargeObject);
    FOR_EACH_TYPED_HEAP(SNAPSHOT_HEAP);

    ASSERT(numberOfHeapsReported == BlinkGC::NumberOfHeaps);

#undef SNAPSHOT_HEAP

    if (type == SnapshotType::FreelistSnapshot)
        return;

    size_t totalLiveCount = 0;
    size_t totalDeadCount = 0;
    size_t totalLiveSize = 0;
    size_t totalDeadSize = 0;
    for (size_t gcInfoIndex = 1; gcInfoIndex <= GCInfoTable::gcInfoIndex(); ++gcInfoIndex) {
        String dumpName = classesDumpName + String::format("/%lu_", static_cast<unsigned long>(gcInfoIndex));
#if ENABLE(DETAILED_MEMORY_INFRA)
        dumpName.append(Heap::gcInfo(gcInfoIndex)->className());
#endif
        WebMemoryAllocatorDump* classDump = BlinkGCMemoryDumpProvider::instance()->createMemoryAllocatorDumpForCurrentGC(dumpName);
        classDump->addScalar("live_count", "objects", info.liveCount[gcInfoIndex]);
        classDump->addScalar("dead_count", "objects", info.deadCount[gcInfoIndex]);
        classDump->addScalar("live_size", "bytes", info.liveSize[gcInfoIndex]);
        classDump->addScalar("dead_size", "bytes", info.deadSize[gcInfoIndex]);

        totalLiveCount += info.liveCount[gcInfoIndex];
        totalDeadCount += info.deadCount[gcInfoIndex];
        totalLiveSize += info.liveSize[gcInfoIndex];
        totalDeadSize += info.deadSize[gcInfoIndex];
    }

    WebMemoryAllocatorDump* threadDump = BlinkGCMemoryDumpProvider::instance()->createMemoryAllocatorDumpForCurrentGC(threadDumpName);
    threadDump->addScalar("live_count", "objects", totalLiveCount);
    threadDump->addScalar("dead_count", "objects", totalDeadCount);
    threadDump->addScalar("live_size", "bytes", totalLiveSize);
    threadDump->addScalar("dead_size", "bytes", totalDeadSize);

    WebMemoryAllocatorDump* heapsDump = BlinkGCMemoryDumpProvider::instance()->createMemoryAllocatorDumpForCurrentGC(heapsDumpName);
    WebMemoryAllocatorDump* classesDump = BlinkGCMemoryDumpProvider::instance()->createMemoryAllocatorDumpForCurrentGC(classesDumpName);
    BlinkGCMemoryDumpProvider::instance()->currentProcessMemoryDump()->addOwnershipEdge(classesDump->guid(), heapsDump->guid());
}

} // namespace blink
