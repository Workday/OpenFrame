// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_TREES_CHANNEL_MAIN_H_
#define CC_TREES_CHANNEL_MAIN_H_

#include "cc/base/cc_export.h"
#include "cc/base/completion_event.h"
#include "cc/input/top_controls_state.h"
#include "cc/output/output_surface.h"
#include "cc/scheduler/commit_earlyout_reason.h"
#include "cc/trees/proxy_common.h"

namespace cc {
// ChannelMain and ChannelImpl provide an abstract communication layer for
// the main and impl side of the compositor.
//
// The communication sequence between the 2 sides is:
//
// LayerTreeHost<-->ProxyMain<-->ChannelMain
//                                      |
//                                      |
//                               ChannelImpl<-->ProxyImpl<-->LayerTreeHostImpl

class CC_EXPORT ChannelMain {
 public:
  // Interface for commands sent to the ProxyImpl
  virtual void SetThrottleFrameProductionOnImpl(bool throttle) = 0;
  virtual void UpdateTopControlsStateOnImpl(TopControlsState constraints,
                                            TopControlsState current,
                                            bool animate) = 0;
  virtual void InitializeOutputSurfaceOnImpl(OutputSurface* output_surface) = 0;
  virtual void MainThreadHasStoppedFlingingOnImpl() = 0;
  virtual void SetInputThrottledUntilCommitOnImpl(bool is_throttled) = 0;
  virtual void SetDeferCommitsOnImpl(bool defer_commits) = 0;
  virtual void FinishAllRenderingOnImpl(CompletionEvent* completion) = 0;
  virtual void SetVisibleOnImpl(bool visible) = 0;
  virtual void ReleaseOutputSurfaceOnImpl(CompletionEvent* completion) = 0;
  virtual void FinishGLOnImpl(CompletionEvent* completion) = 0;
  virtual void MainFrameWillHappenOnImplForTesting(
      CompletionEvent* completion,
      bool* main_frame_will_happen) = 0;
  virtual void SetNeedsRedrawOnImpl(const gfx::Rect& damage_rect) = 0;
  virtual void SetNeedsCommitOnImpl() = 0;
  virtual void BeginMainFrameAbortedOnImpl(
      CommitEarlyOutReason reason,
      base::TimeTicks main_thread_start_time) = 0;
  virtual void StartCommitOnImpl(CompletionEvent* completion,
                                 LayerTreeHost* layer_tree_host,
                                 base::TimeTicks main_thread_start_time,
                                 bool hold_commit_for_activation) = 0;
  virtual void InitializeImplOnImpl(CompletionEvent* completion,
                                    LayerTreeHost* layer_tree_host) = 0;
  virtual void LayerTreeHostClosedOnImpl(CompletionEvent* completion) = 0;

  virtual ~ChannelMain() {}
};

}  // namespace cc

#endif  // CC_TREES_CHANNEL_MAIN_H_
