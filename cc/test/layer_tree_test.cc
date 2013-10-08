// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/test/layer_tree_test.h"

#include "base/command_line.h"
#include "cc/animation/animation.h"
#include "cc/animation/animation_registrar.h"
#include "cc/animation/layer_animation_controller.h"
#include "cc/animation/timing_function.h"
#include "cc/base/switches.h"
#include "cc/input/input_handler.h"
#include "cc/layers/content_layer.h"
#include "cc/layers/layer.h"
#include "cc/layers/layer_impl.h"
#include "cc/test/animation_test_common.h"
#include "cc/test/fake_context_provider.h"
#include "cc/test/fake_layer_tree_host_client.h"
#include "cc/test/fake_output_surface.h"
#include "cc/test/occlusion_tracker_test_common.h"
#include "cc/test/tiled_layer_test_common.h"
#include "cc/trees/layer_tree_host_impl.h"
#include "cc/trees/single_thread_proxy.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "ui/gfx/size_conversions.h"

namespace cc {

TestHooks::TestHooks() {}

TestHooks::~TestHooks() {}

bool TestHooks::PrepareToDrawOnThread(LayerTreeHostImpl* host_impl,
                                      LayerTreeHostImpl::FrameData* frame_data,
                                      bool result) {
  return true;
}

bool TestHooks::CanActivatePendingTree(LayerTreeHostImpl* host_impl) {
  return true;
}

bool TestHooks::CanActivatePendingTreeIfNeeded(LayerTreeHostImpl* host_impl) {
  return true;
}

// Adapts LayerTreeHostImpl for test. Runs real code, then invokes test hooks.
class LayerTreeHostImplForTesting : public LayerTreeHostImpl {
 public:
  static scoped_ptr<LayerTreeHostImplForTesting> Create(
      TestHooks* test_hooks,
      const LayerTreeSettings& settings,
      LayerTreeHostImplClient* host_impl_client,
      Proxy* proxy,
      RenderingStatsInstrumentation* stats_instrumentation) {
    return make_scoped_ptr(
        new LayerTreeHostImplForTesting(test_hooks,
                                        settings,
                                        host_impl_client,
                                        proxy,
                                        stats_instrumentation));
  }

 protected:
  LayerTreeHostImplForTesting(
      TestHooks* test_hooks,
      const LayerTreeSettings& settings,
      LayerTreeHostImplClient* host_impl_client,
      Proxy* proxy,
      RenderingStatsInstrumentation* stats_instrumentation)
      : LayerTreeHostImpl(settings,
                          host_impl_client,
                          proxy,
                          stats_instrumentation),
        test_hooks_(test_hooks) {}

  virtual void BeginCommit() OVERRIDE {
    LayerTreeHostImpl::BeginCommit();
    test_hooks_->BeginCommitOnThread(this);
  }

  virtual void CommitComplete() OVERRIDE {
    LayerTreeHostImpl::CommitComplete();
    test_hooks_->CommitCompleteOnThread(this);

    if (!settings().impl_side_painting) {
      test_hooks_->WillActivateTreeOnThread(this);
      test_hooks_->DidActivateTreeOnThread(this);
    }
  }

  virtual bool PrepareToDraw(FrameData* frame, gfx::Rect damage_rect) OVERRIDE {
    bool result = LayerTreeHostImpl::PrepareToDraw(frame, damage_rect);
    if (!test_hooks_->PrepareToDrawOnThread(this, frame, result))
      result = false;
    return result;
  }

  virtual void DrawLayers(FrameData* frame,
                          base::TimeTicks frame_begin_time) OVERRIDE {
    LayerTreeHostImpl::DrawLayers(frame, frame_begin_time);
    test_hooks_->DrawLayersOnThread(this);
  }

  virtual bool SwapBuffers(const LayerTreeHostImpl::FrameData& frame) OVERRIDE {
    bool result = LayerTreeHostImpl::SwapBuffers(frame);
    test_hooks_->SwapBuffersOnThread(this, result);
    return result;
  }

  virtual void OnSwapBuffersComplete(const CompositorFrameAck* ack) OVERRIDE {
    LayerTreeHostImpl::OnSwapBuffersComplete(ack);
    test_hooks_->SwapBuffersCompleteOnThread(this);
  }

  virtual void ActivatePendingTreeIfNeeded() OVERRIDE {
    if (!pending_tree())
      return;

    if (!test_hooks_->CanActivatePendingTreeIfNeeded(this))
      return;

    LayerTreeHostImpl::ActivatePendingTreeIfNeeded();
  }

  virtual void ActivatePendingTree() OVERRIDE {
    if (!test_hooks_->CanActivatePendingTree(this))
      return;

    test_hooks_->WillActivateTreeOnThread(this);
    LayerTreeHostImpl::ActivatePendingTree();
    DCHECK(!pending_tree());
    test_hooks_->DidActivateTreeOnThread(this);
  }

  virtual bool InitializeRenderer(scoped_ptr<OutputSurface> output_surface)
      OVERRIDE {
    bool success = LayerTreeHostImpl::InitializeRenderer(output_surface.Pass());
    test_hooks_->InitializedRendererOnThread(this, success);
    return success;
  }

  virtual void SetVisible(bool visible) OVERRIDE {
    LayerTreeHostImpl::SetVisible(visible);
    test_hooks_->DidSetVisibleOnImplTree(this, visible);
  }

  virtual void AnimateLayers(base::TimeTicks monotonic_time,
                             base::Time wall_clock_time) OVERRIDE {
    test_hooks_->WillAnimateLayers(this, monotonic_time);
    LayerTreeHostImpl::AnimateLayers(monotonic_time, wall_clock_time);
    test_hooks_->AnimateLayers(this, monotonic_time);
  }

  virtual void UpdateAnimationState(bool start_ready_animations) OVERRIDE {
    LayerTreeHostImpl::UpdateAnimationState(start_ready_animations);
    bool has_unfinished_animation = false;
    AnimationRegistrar::AnimationControllerMap::const_iterator iter =
        active_animation_controllers().begin();
    for (; iter != active_animation_controllers().end(); ++iter) {
      if (iter->second->HasActiveAnimation()) {
        has_unfinished_animation = true;
        break;
      }
    }
    test_hooks_->UpdateAnimationState(this, has_unfinished_animation);
  }

  virtual base::TimeDelta LowFrequencyAnimationInterval() const OVERRIDE {
    return base::TimeDelta::FromMilliseconds(16);
  }

 private:
  TestHooks* test_hooks_;
};

// Adapts LayerTreeHost for test. Injects LayerTreeHostImplForTesting.
class LayerTreeHostForTesting : public cc::LayerTreeHost {
 public:
  static scoped_ptr<LayerTreeHostForTesting> Create(
      TestHooks* test_hooks,
      cc::LayerTreeHostClient* host_client,
      const cc::LayerTreeSettings& settings,
      scoped_refptr<base::SingleThreadTaskRunner> impl_task_runner) {
    scoped_ptr<LayerTreeHostForTesting> layer_tree_host(
        new LayerTreeHostForTesting(test_hooks, host_client, settings));
    bool success = layer_tree_host->Initialize(impl_task_runner);
    EXPECT_TRUE(success);
    return layer_tree_host.Pass();
  }

  virtual scoped_ptr<cc::LayerTreeHostImpl> CreateLayerTreeHostImpl(
      cc::LayerTreeHostImplClient* host_impl_client) OVERRIDE {
    return LayerTreeHostImplForTesting::Create(
        test_hooks_,
        settings(),
        host_impl_client,
        proxy(),
        rendering_stats_instrumentation()).PassAs<cc::LayerTreeHostImpl>();
  }

  virtual void SetNeedsCommit() OVERRIDE {
    if (!test_started_)
      return;
    LayerTreeHost::SetNeedsCommit();
  }

  void set_test_started(bool started) { test_started_ = started; }

  virtual void DidDeferCommit() OVERRIDE {
    test_hooks_->DidDeferCommit();
  }

 private:
  LayerTreeHostForTesting(TestHooks* test_hooks,
                          cc::LayerTreeHostClient* client,
                          const cc::LayerTreeSettings& settings)
      : LayerTreeHost(client, settings),
        test_hooks_(test_hooks),
        test_started_(false) {}

  TestHooks* test_hooks_;
  bool test_started_;
};

// Implementation of LayerTreeHost callback interface.
class LayerTreeHostClientForTesting : public LayerTreeHostClient {
 public:
  static scoped_ptr<LayerTreeHostClientForTesting> Create(
      TestHooks* test_hooks) {
    return make_scoped_ptr(new LayerTreeHostClientForTesting(test_hooks));
  }
  virtual ~LayerTreeHostClientForTesting() {}

  virtual void WillBeginFrame() OVERRIDE { test_hooks_->WillBeginFrame(); }

  virtual void DidBeginFrame() OVERRIDE { test_hooks_->DidBeginFrame(); }

  virtual void Animate(double monotonic_time) OVERRIDE {
    test_hooks_->Animate(base::TimeTicks::FromInternalValue(
        monotonic_time * base::Time::kMicrosecondsPerSecond));
  }

  virtual void Layout() OVERRIDE {
    test_hooks_->Layout();
  }

  virtual void ApplyScrollAndScale(gfx::Vector2d scroll_delta,
                                   float scale) OVERRIDE {
    test_hooks_->ApplyScrollAndScale(scroll_delta, scale);
  }

  virtual scoped_ptr<OutputSurface> CreateOutputSurface(bool fallback)
      OVERRIDE {
    return test_hooks_->CreateOutputSurface(fallback);
  }

  virtual void DidInitializeOutputSurface(bool succeeded) OVERRIDE {
    test_hooks_->DidInitializeOutputSurface(succeeded);
  }

  virtual void DidFailToInitializeOutputSurface() OVERRIDE {
    test_hooks_->DidFailToInitializeOutputSurface();
  }

  virtual void WillCommit() OVERRIDE { test_hooks_->WillCommit(); }

  virtual void DidCommit() OVERRIDE {
    test_hooks_->DidCommit();
  }

  virtual void DidCommitAndDrawFrame() OVERRIDE {
    test_hooks_->DidCommitAndDrawFrame();
  }

  virtual void DidCompleteSwapBuffers() OVERRIDE {
    test_hooks_->DidCompleteSwapBuffers();
  }

  virtual void ScheduleComposite() OVERRIDE {
    test_hooks_->ScheduleComposite();
  }

  virtual scoped_refptr<cc::ContextProvider>
      OffscreenContextProviderForMainThread() OVERRIDE {
    return test_hooks_->OffscreenContextProviderForMainThread();
  }

  virtual scoped_refptr<cc::ContextProvider>
      OffscreenContextProviderForCompositorThread() OVERRIDE {
    return test_hooks_->OffscreenContextProviderForCompositorThread();
  }

 private:
  explicit LayerTreeHostClientForTesting(TestHooks* test_hooks)
      : test_hooks_(test_hooks) {}

  TestHooks* test_hooks_;
};

LayerTreeTest::LayerTreeTest()
    : beginning_(false),
      end_when_begin_returns_(false),
      timed_out_(false),
      scheduled_(false),
      schedule_when_set_visible_true_(false),
      started_(false),
      ended_(false),
      delegating_renderer_(false),
      timeout_seconds_(0),
      weak_factory_(this) {
  main_thread_weak_ptr_ = weak_factory_.GetWeakPtr();

  // Tests should timeout quickly unless --cc-layer-tree-test-no-timeout was
  // specified (for running in a debugger).
  CommandLine* command_line = CommandLine::ForCurrentProcess();
  if (!command_line->HasSwitch(switches::kCCLayerTreeTestNoTimeout))
    timeout_seconds_ = 5;
}

LayerTreeTest::~LayerTreeTest() {}

void LayerTreeTest::EndTest() {
  // For the case where we EndTest during BeginTest(), set a flag to indicate
  // that the test should end the second BeginTest regains control.
  ended_ = true;

  if (beginning_) {
    end_when_begin_returns_ = true;
  } else if (proxy()) {
    // Racy timeouts and explicit EndTest calls might have cleaned up
    // the tree host. Should check proxy first.
    proxy()->MainThreadTaskRunner()->PostTask(
        FROM_HERE,
        base::Bind(&LayerTreeTest::RealEndTest, main_thread_weak_ptr_));
  }
}

void LayerTreeTest::EndTestAfterDelay(int delay_milliseconds) {
  proxy()->MainThreadTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(&LayerTreeTest::EndTest, main_thread_weak_ptr_));
}

void LayerTreeTest::PostAddAnimationToMainThread(
    Layer* layer_to_receive_animation) {
  proxy()->MainThreadTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(&LayerTreeTest::DispatchAddAnimation,
                 main_thread_weak_ptr_,
                 base::Unretained(layer_to_receive_animation)));
}

void LayerTreeTest::PostAddInstantAnimationToMainThread(
    Layer* layer_to_receive_animation) {
  proxy()->MainThreadTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(&LayerTreeTest::DispatchAddInstantAnimation,
                 main_thread_weak_ptr_,
                 base::Unretained(layer_to_receive_animation)));
}

void LayerTreeTest::PostSetNeedsCommitToMainThread() {
  proxy()->MainThreadTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(&LayerTreeTest::DispatchSetNeedsCommit,
                 main_thread_weak_ptr_));
}

void LayerTreeTest::PostAcquireLayerTextures() {
  proxy()->MainThreadTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(&LayerTreeTest::DispatchAcquireLayerTextures,
                 main_thread_weak_ptr_));
}

void LayerTreeTest::PostSetNeedsRedrawToMainThread() {
  proxy()->MainThreadTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(&LayerTreeTest::DispatchSetNeedsRedraw,
                 main_thread_weak_ptr_));
}

void LayerTreeTest::PostSetNeedsRedrawRectToMainThread(gfx::Rect damage_rect) {
  proxy()->MainThreadTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(&LayerTreeTest::DispatchSetNeedsRedrawRect,
                 main_thread_weak_ptr_, damage_rect));
}

void LayerTreeTest::PostSetVisibleToMainThread(bool visible) {
  proxy()->MainThreadTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(&LayerTreeTest::DispatchSetVisible,
                 main_thread_weak_ptr_,
                 visible));
}

void LayerTreeTest::DoBeginTest() {
  client_ = LayerTreeHostClientForTesting::Create(this);

  DCHECK(!impl_thread_ || impl_thread_->message_loop_proxy().get());
  layer_tree_host_ = LayerTreeHostForTesting::Create(
      this,
      client_.get(),
      settings_,
      impl_thread_ ? impl_thread_->message_loop_proxy() : NULL);
  ASSERT_TRUE(layer_tree_host_);

  started_ = true;
  beginning_ = true;
  SetupTree();
  layer_tree_host_->SetLayerTreeHostClientReady();
  BeginTest();
  beginning_ = false;
  if (end_when_begin_returns_)
    RealEndTest();

  // Allow commits to happen once BeginTest() has had a chance to post tasks
  // so that those tasks will happen before the first commit.
  if (layer_tree_host_) {
    static_cast<LayerTreeHostForTesting*>(layer_tree_host_.get())->
        set_test_started(true);
  }
}

void LayerTreeTest::SetupTree() {
  if (!layer_tree_host_->root_layer()) {
    scoped_refptr<Layer> root_layer = Layer::Create();
    root_layer->SetAnchorPoint(gfx::PointF());
    root_layer->SetBounds(gfx::Size(1, 1));
    root_layer->SetIsDrawable(true);
    layer_tree_host_->SetRootLayer(root_layer);
  }

  gfx::Size root_bounds = layer_tree_host_->root_layer()->bounds();
  gfx::Size device_root_bounds = gfx::ToCeiledSize(
      gfx::ScaleSize(root_bounds, layer_tree_host_->device_scale_factor()));
  layer_tree_host_->SetViewportSize(device_root_bounds);
}

void LayerTreeTest::Timeout() {
  timed_out_ = true;
  EndTest();
}

void LayerTreeTest::ScheduleComposite() {
  if (!started_ || scheduled_)
    return;
  scheduled_ = true;
  proxy()->MainThreadTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(&LayerTreeTest::DispatchComposite, main_thread_weak_ptr_));
}

void LayerTreeTest::RealEndTest() {
  if (layer_tree_host_ && proxy()->CommitPendingForTesting()) {
    proxy()->MainThreadTaskRunner()->PostTask(
        FROM_HERE,
        base::Bind(&LayerTreeTest::RealEndTest, main_thread_weak_ptr_));
    return;
  }

  base::MessageLoop::current()->Quit();
}

void LayerTreeTest::DispatchAddInstantAnimation(
    Layer* layer_to_receive_animation) {
  DCHECK(!proxy() || proxy()->IsMainThread());

  if (layer_to_receive_animation) {
    AddOpacityTransitionToLayer(layer_to_receive_animation,
                                0,
                                0,
                                0.5,
                                false);
  }
}

void LayerTreeTest::DispatchAddAnimation(Layer* layer_to_receive_animation) {
  DCHECK(!proxy() || proxy()->IsMainThread());

  if (layer_to_receive_animation) {
    AddOpacityTransitionToLayer(layer_to_receive_animation,
                                0.000001,
                                0,
                                0.5,
                                true);
  }
}

void LayerTreeTest::DispatchSetNeedsCommit() {
  DCHECK(!proxy() || proxy()->IsMainThread());

  if (layer_tree_host_)
    layer_tree_host_->SetNeedsCommit();
}

void LayerTreeTest::DispatchAcquireLayerTextures() {
  DCHECK(!proxy() || proxy()->IsMainThread());

  if (layer_tree_host_)
    layer_tree_host_->AcquireLayerTextures();
}

void LayerTreeTest::DispatchSetNeedsRedraw() {
  DCHECK(!proxy() || proxy()->IsMainThread());

  if (layer_tree_host_)
    layer_tree_host_->SetNeedsRedraw();
}

void LayerTreeTest::DispatchSetNeedsRedrawRect(gfx::Rect damage_rect) {
  DCHECK(!proxy() || proxy()->IsMainThread());

  if (layer_tree_host_)
    layer_tree_host_->SetNeedsRedrawRect(damage_rect);
}

void LayerTreeTest::DispatchSetVisible(bool visible) {
  DCHECK(!proxy() || proxy()->IsMainThread());

  if (!layer_tree_host_)
    return;

  layer_tree_host_->SetVisible(visible);

  // If the LTH is being made visible and a previous ScheduleComposite() was
  // deferred because the LTH was not visible, re-schedule the composite now.
  if (layer_tree_host_->visible() && schedule_when_set_visible_true_)
    ScheduleComposite();
}

void LayerTreeTest::DispatchComposite() {
  scheduled_ = false;

  if (!layer_tree_host_)
    return;

  // If the LTH is not visible, defer the composite until the LTH is made
  // visible.
  if (!layer_tree_host_->visible()) {
    schedule_when_set_visible_true_ = true;
    return;
  }

  schedule_when_set_visible_true_ = false;
  base::TimeTicks now = base::TimeTicks::Now();
  layer_tree_host_->Composite(now);
}

void LayerTreeTest::RunTest(bool threaded,
                            bool delegating_renderer,
                            bool impl_side_painting) {
  if (threaded) {
    impl_thread_.reset(new base::Thread("Compositor"));
    ASSERT_TRUE(impl_thread_->Start());
  }

  scoped_refptr<base::SingleThreadTaskRunner> main_task_runner_ =
      base::MessageLoopProxy::current();

  delegating_renderer_ = delegating_renderer;

  // Spend less time waiting for BeginFrame because the output is
  // mocked out.
  settings_.refresh_rate = 200.0;
  if (impl_side_painting) {
    DCHECK(threaded) <<
        "Don't run single thread + impl side painting, it doesn't exist.";
    settings_.impl_side_painting = true;
  }
  InitializeSettings(&settings_);

  main_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&LayerTreeTest::DoBeginTest, base::Unretained(this)));

  if (timeout_seconds_) {
    timeout_.Reset(base::Bind(&LayerTreeTest::Timeout, base::Unretained(this)));
    main_task_runner_->PostDelayedTask(
        FROM_HERE,
        timeout_.callback(),
        base::TimeDelta::FromSeconds(timeout_seconds_));
  }

  base::MessageLoop::current()->Run();
  if (layer_tree_host_ && layer_tree_host_->root_layer())
    layer_tree_host_->root_layer()->SetLayerTreeHost(NULL);
  layer_tree_host_.reset();

  timeout_.Cancel();

  ASSERT_FALSE(layer_tree_host_.get());
  client_.reset();
  if (timed_out_) {
    FAIL() << "Test timed out";
    return;
  }
  AfterTest();
}

scoped_ptr<OutputSurface> LayerTreeTest::CreateOutputSurface(bool fallback) {
  scoped_ptr<FakeOutputSurface> output_surface;
  if (delegating_renderer_)
    output_surface = FakeOutputSurface::CreateDelegating3d();
  else
    output_surface = FakeOutputSurface::Create3d();
  output_surface_ = output_surface.get();
  return output_surface.PassAs<OutputSurface>();
}

scoped_refptr<cc::ContextProvider> LayerTreeTest::
    OffscreenContextProviderForMainThread() {
  if (!main_thread_contexts_.get() ||
      main_thread_contexts_->DestroyedOnMainThread()) {
    main_thread_contexts_ = FakeContextProvider::Create();
    if (!main_thread_contexts_->BindToCurrentThread())
      main_thread_contexts_ = NULL;
  }
  return main_thread_contexts_;
}

scoped_refptr<cc::ContextProvider> LayerTreeTest::
    OffscreenContextProviderForCompositorThread() {
  if (!compositor_thread_contexts_.get() ||
      compositor_thread_contexts_->DestroyedOnMainThread())
    compositor_thread_contexts_ = FakeContextProvider::Create();
  return compositor_thread_contexts_;
}

}  // namespace cc
