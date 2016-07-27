// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TRACE_EVENT_HEAP_PROFILER_ALLOCATION_CONTEXT_TRACKER_H_
#define BASE_TRACE_EVENT_HEAP_PROFILER_ALLOCATION_CONTEXT_TRACKER_H_

#include <vector>

#include "base/atomicops.h"
#include "base/base_export.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/trace_event/heap_profiler_allocation_context.h"

namespace base {
namespace trace_event {

// The allocation context tracker keeps track of thread-local context for heap
// profiling. It includes a pseudo stack of trace events. On every allocation
// the tracker provides a snapshot of its context in the form of an
// |AllocationContext| that is to be stored together with the allocation
// details.
class BASE_EXPORT AllocationContextTracker {
 public:
  // Globally enables capturing allocation context.
  // TODO(ruuda): Should this be replaced by |EnableCapturing| in the future?
  // Or at least have something that guards agains enable -> disable -> enable?
  static void SetCaptureEnabled(bool enabled);

  // Returns whether capturing allocation context is enabled globally.
  inline static bool capture_enabled() {
    // A little lag after heap profiling is enabled or disabled is fine, it is
    // more important that the check is as cheap as possible when capturing is
    // not enabled, so do not issue a memory barrier in the fast path.
    if (subtle::NoBarrier_Load(&capture_enabled_) == 0)
      return false;

    // In the slow path, an acquire load is required to pair with the release
    // store in |SetCaptureEnabled|. This is to ensure that the TLS slot for
    // the thread-local allocation context tracker has been initialized if
    // |capture_enabled| returns true.
    return subtle::Acquire_Load(&capture_enabled_) != 0;
  }

  // Pushes a frame onto the thread-local pseudo stack.
  static void PushPseudoStackFrame(StackFrame frame);

  // Pops a frame from the thread-local pseudo stack.
  static void PopPseudoStackFrame(StackFrame frame);

  // Returns a snapshot of the current thread-local context.
  static AllocationContext GetContextSnapshot();

  ~AllocationContextTracker();

 private:
  AllocationContextTracker();

  static AllocationContextTracker* GetThreadLocalTracker();

  static subtle::Atomic32 capture_enabled_;

  // The pseudo stack where frames are |TRACE_EVENT| names.
  std::vector<StackFrame> pseudo_stack_;

  DISALLOW_COPY_AND_ASSIGN(AllocationContextTracker);
};

}  // namespace trace_event
}  // namespace base

#endif  // BASE_TRACE_EVENT_HEAP_PROFILER_ALLOCATION_CONTEXT_TRACKER_H_
