// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_TREES_THREADED_CHANNEL_H_
#define CC_TREES_THREADED_CHANNEL_H_

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "cc/base/cc_export.h"
#include "cc/trees/channel_impl.h"
#include "cc/trees/channel_main.h"
#include "cc/trees/proxy_common.h"
#include "cc/trees/proxy_impl.h"
#include "cc/trees/proxy_main.h"
#include "cc/trees/thread_proxy.h"

namespace base {
class SingleThreadTaskRunner;
}

namespace cc {
class ChannelImpl;
class ChannelMain;
class ProxyImpl;
class ProxyMain;
class ThreadProxy;

// An implementation of ChannelMain and ChannelImpl that sends commands between
// ProxyMain and ProxyImpl across thread boundaries.
//
// LayerTreeHost creates ThreadedChannel and passes the ownership to ProxyMain.
// The object life cycle and communication across threads is as follows:
//
//
//           Main Thread              |               Impl Thread
//   LayerTreeHost->InitializeProxy   |
//               |                    |
//        ProxyMain->Start()          |
//               |              ThreadedChannel
// ---------------------------------------------------------------------------
//  ChannelMain::InitializeImpl ---PostTask---> ThreadedChannel::
//                                                   InitializeImplOnImplThread
//                                                          |
//                                                   ProxyImpl::Create
//                                                          |
//                                                   ProxyImpl->Initialize()
//                                                          .
//                                                          .
//                                          ProxyImpl::ScheduledActionBegin
//                                                     OutputSurfaceCreation
//                                                          |
//                                         ChannelImpl::RequestNewOutputSurface
// ----------------------------------------------------------------------------
//                                                          |
// ProxyMain->RequestNewOutputSurface()<----PostTask--------
//              .
//              .
// ProxyMain->LayerTreeHostClosed
//              |
// ---------------------------------------------------------------------------
// ChannelMain::SetLayerTreeClosedOnImpl---PostTask---> ProxyImpl->
//                                                        SetLayerTreeClosed
// ----------------------------------------------------------------------------

class CC_EXPORT ThreadedChannel : public ChannelMain, public ChannelImpl {
 public:
  static scoped_ptr<ThreadedChannel> Create(
      // TODO(khushalsagar): Make this ProxyMain* and write the initialization
      // sequence. Currently ThreadProxy implements both so we pass the pointer
      // and set ProxyImpl.
      ThreadProxy* thread_proxy,
      TaskRunnerProvider* task_runner_provider);

  ~ThreadedChannel() override;

  // ChannelMain Implementation
  void SetThrottleFrameProductionOnImpl(bool throttle) override;
  void UpdateTopControlsStateOnImpl(TopControlsState constraints,
                                    TopControlsState current,
                                    bool animate) override;
  void InitializeOutputSurfaceOnImpl(OutputSurface* output_surface) override;
  void MainThreadHasStoppedFlingingOnImpl() override;
  void SetInputThrottledUntilCommitOnImpl(bool is_throttled) override;
  void SetDeferCommitsOnImpl(bool defer_commits) override;
  void SetNeedsCommitOnImpl() override;
  void BeginMainFrameAbortedOnImpl(
      CommitEarlyOutReason reason,
      base::TimeTicks main_thread_start_time) override;
  void SetNeedsRedrawOnImpl(const gfx::Rect& damage_rect) override;
  void SetVisibleOnImpl(bool visible) override;

  // Blocking calls to ProxyImpl
  void FinishAllRenderingOnImpl(CompletionEvent* completion) override;
  void ReleaseOutputSurfaceOnImpl(CompletionEvent* completion) override;
  void FinishGLOnImpl(CompletionEvent* completion) override;
  void MainFrameWillHappenOnImplForTesting(
      CompletionEvent* completion,
      bool* main_frame_will_happen) override;
  void StartCommitOnImpl(CompletionEvent* completion,
                         LayerTreeHost* layer_tree_host,
                         base::TimeTicks main_thread_start_time,
                         bool hold_commit_for_activation) override;
  void InitializeImplOnImpl(CompletionEvent* completion,
                            LayerTreeHost* layer_tree_host) override;
  void LayerTreeHostClosedOnImpl(CompletionEvent* completion) override;

  // ChannelImpl Implementation
  void DidCompleteSwapBuffers() override;
  void SetRendererCapabilitiesMainCopy(
      const RendererCapabilities& capabilities) override;
  void BeginMainFrameNotExpectedSoon() override;
  void DidCommitAndDrawFrame() override;
  void SetAnimationEvents(scoped_ptr<AnimationEventsVector> queue) override;
  void DidLoseOutputSurface() override;
  void RequestNewOutputSurface() override;
  void DidInitializeOutputSurface(
      bool success,
      const RendererCapabilities& capabilities) override;
  void DidCompletePageScaleAnimation() override;
  void PostFrameTimingEventsOnMain(
      scoped_ptr<FrameTimingTracker::CompositeTimingSet> composite_events,
      scoped_ptr<FrameTimingTracker::MainFrameTimingSet> main_frame_events)
      override;
  void BeginMainFrame(
      scoped_ptr<BeginMainFrameAndCommitState> begin_main_frame_state) override;

 protected:
  ThreadedChannel(ThreadProxy* thread_proxy,
                  TaskRunnerProvider* task_runner_provider);

 private:
  base::SingleThreadTaskRunner* MainThreadTaskRunner() const;
  base::SingleThreadTaskRunner* ImplThreadTaskRunner() const;

  ProxyMain* proxy_main_;

  ProxyImpl* proxy_impl_;

  TaskRunnerProvider* task_runner_provider_;

  scoped_refptr<base::SingleThreadTaskRunner> main_task_runner_;

  scoped_refptr<base::SingleThreadTaskRunner> impl_task_runner_;

  DISALLOW_COPY_AND_ASSIGN(ThreadedChannel);
};

}  // namespace cc

#endif  // CC_TREES_THREADED_CHANNEL_H_
