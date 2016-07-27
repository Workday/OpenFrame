// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_TREES_PROXY_H_
#define CC_TREES_PROXY_H_

#include <string>

#include "base/basictypes.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "base/values.h"
#include "cc/base/cc_export.h"
#include "cc/input/top_controls_state.h"
#include "cc/trees/task_runner_provider.h"

namespace gfx {
class Rect;
class Vector2d;
}

namespace cc {
class LayerTreeDebugState;
class OutputSurface;
struct RendererCapabilities;

// Abstract interface responsible for proxying commands from the main-thread
// side of the compositor over to the compositor implementation.
class CC_EXPORT Proxy {
 public:
  virtual ~Proxy() {}

  virtual void FinishAllRendering() = 0;

  virtual bool IsStarted() const = 0;
  virtual bool CommitToActiveTree() const = 0;

  // Will call LayerTreeHost::OnCreateAndInitializeOutputSurfaceAttempted
  // with the result of this function.
  virtual void SetOutputSurface(OutputSurface* output_surface) = 0;

  virtual void ReleaseOutputSurface() = 0;

  virtual void SetVisible(bool visible) = 0;

  virtual void SetThrottleFrameProduction(bool throttle) = 0;

  virtual const RendererCapabilities& GetRendererCapabilities() const = 0;

  virtual void SetNeedsAnimate() = 0;
  virtual void SetNeedsUpdateLayers() = 0;
  virtual void SetNeedsCommit() = 0;
  virtual void SetNeedsRedraw(const gfx::Rect& damage_rect) = 0;
  virtual void SetNextCommitWaitsForActivation() = 0;

  virtual void NotifyInputThrottledUntilCommit() = 0;

  // Defers commits until it is reset. It is only supported when using a
  // scheduler.
  virtual void SetDeferCommits(bool defer_commits) = 0;

  virtual void MainThreadHasStoppedFlinging() = 0;

  virtual bool CommitRequested() const = 0;
  virtual bool BeginMainFrameRequested() const = 0;

  // Must be called before using the proxy.
  virtual void Start() = 0;
  virtual void Stop() = 0;   // Must be called before deleting the proxy.

  virtual bool SupportsImplScrolling() const = 0;

  virtual void SetChildrenNeedBeginFrames(bool children_need_begin_frames) = 0;

  virtual void SetAuthoritativeVSyncInterval(
      const base::TimeDelta& interval) = 0;

  virtual void UpdateTopControlsState(TopControlsState constraints,
                                      TopControlsState current,
                                      bool animate) = 0;

  // Testing hooks
  virtual bool MainFrameWillHappenForTesting() = 0;
};

}  // namespace cc

#endif  // CC_TREES_PROXY_H_
