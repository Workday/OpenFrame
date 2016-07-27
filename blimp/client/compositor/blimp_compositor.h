// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BLIMP_CLIENT_COMPOSITOR_BLIMP_COMPOSITOR_H_
#define BLIMP_CLIENT_COMPOSITOR_BLIMP_COMPOSITOR_H_

#include <vector>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "blimp/client/blimp_client_export.h"
#include "blimp/client/compositor/render_widget_message_processor.h"
#include "cc/layers/layer_settings.h"
#include "cc/trees/layer_tree_host_client.h"
#include "cc/trees/layer_tree_settings.h"
#include "cc/trees/remote_proto_channel.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/native_widget_types.h"

namespace base {
class SingleThreadTaskRunner;
class Thread;
}

namespace cc {
class LayerTreeHost;
}

namespace blimp {

class BlimpMessage;

// BlimpCompositor provides the basic framework and setup to host a
// LayerTreeHost.  The class that owns the LayerTreeHost is usually called the
// compositor, but the LayerTreeHost does the compositing work.  The rendering
// surface this compositor draws to is defined by the gfx::AcceleratedWidget set
// by SetAcceleratedWidget().  This class should only be accessed from the main
// thread.  Any interaction with the compositing thread should happen through
// the LayerTreeHost or RemoteChannelImpl.
class BLIMP_CLIENT_EXPORT BlimpCompositor
    : public cc::LayerTreeHostClient,
      public cc::RemoteProtoChannel,
      public RenderWidgetMessageProcessor::RenderWidgetMessageDelegate {

 public:
  ~BlimpCompositor() override;

  // Default layer settings for all Blimp layer instances.
  static cc::LayerSettings LayerSettings();

  // Sets whether or not this compositor actually draws to the output surface.
  // Setting this to false will make the compositor drop all of its resources
  // and the output surface.  Setting it to true again will rebuild the output
  // surface from the gfx::AcceleratedWidget (see SetAcceleratedWidget).
  void SetVisible(bool visible);

  // Sets the size of the viewport on the compositor.
  // TODO(dtrainor): Should this be set from the engine all the time?
  void SetSize(const gfx::Size& size);

  // Lets this compositor know that it can draw to |widget|.  This means that,
  // if this compositor is visible, it will build an output surface and GL
  // context around |widget| and will draw to it.  ReleaseAcceleratedWidget()
  // *must* be called before SetAcceleratedWidget() is called with the same
  // gfx::AcceleratedWidget on another compositor.
  void SetAcceleratedWidget(gfx::AcceleratedWidget widget);

  // Releases the internally stored gfx::AcceleratedWidget and the associated
  // output surface.  This must be called before calling
  // SetAcceleratedWidget() with the same gfx::AcceleratedWidget on another
  // compositor.
  void ReleaseAcceleratedWidget();

 protected:
  // |dp_to_px| is the scale factor required to move from dp (device pixels) to
  // px.  See https://developer.android.com/guide/practices/screens_support.html
  // for more details.
  explicit BlimpCompositor(float dp_to_px);

  // Populates the cc::LayerTreeSettings used by the cc::LayerTreeHost.  Can be
  // overridden to provide custom settings parameters.
  virtual void GenerateLayerTreeSettings(cc::LayerTreeSettings* settings);

 private:
  // LayerTreeHostClient implementation.
  void WillBeginMainFrame() override;
  void DidBeginMainFrame() override;
  void BeginMainFrame(const cc::BeginFrameArgs& args) override;
  void BeginMainFrameNotExpectedSoon() override;
  void UpdateLayerTreeHost() override;
  void ApplyViewportDeltas(const gfx::Vector2dF& inner_delta,
                           const gfx::Vector2dF& outer_delta,
                           const gfx::Vector2dF& elastic_overscroll_delta,
                           float page_scale,
                           float top_controls_delta) override;
  void RequestNewOutputSurface() override;
  void DidInitializeOutputSurface() override;
  void DidFailToInitializeOutputSurface() override;
  void WillCommit() override;
  void DidCommit() override;
  void DidCommitAndDrawFrame() override;
  void DidCompleteSwapBuffers() override;
  void DidCompletePageScaleAnimation() override;
  void RecordFrameTimingEvents(
      scoped_ptr<cc::FrameTimingTracker::CompositeTimingSet> composite_events,
      scoped_ptr<cc::FrameTimingTracker::MainFrameTimingSet> main_frame_events)
      override;

  // RemoteProtoChannel implementation.
  void SetProtoReceiver(ProtoReceiver* receiver) override;
  void SendCompositorProto(const cc::proto::CompositorMessage& proto) override;

  // RenderWidgetMessageDelegate implementation.
  void OnRenderWidgetInitialized() override;
  void OnCompositorMessageReceived(
      scoped_ptr<cc::proto::CompositorMessage> message) override;

  // Helper method to build the internal CC compositor instance from |message|.
  void CreateLayerTreeHost(scoped_ptr<cc::proto::CompositorMessage> message);

  // Creates (if necessary) and returns a TaskRunner for a thread meant to run
  // compositor rendering.
  void HandlePendingOutputSurfaceRequest();
  scoped_refptr<base::SingleThreadTaskRunner> GetCompositorTaskRunner();

  gfx::Size viewport_size_;

  // The scale factor used to convert dp units (device independent pixels) to
  // pixels.
  float device_scale_factor_;
  scoped_ptr<cc::LayerTreeHost> host_;
  scoped_ptr<cc::LayerTreeSettings> settings_;

  // Lazily created thread that will run the compositor rendering tasks.
  scoped_ptr<base::Thread> compositor_thread_;

  gfx::AcceleratedWidget window_;

  // Whether or not |host_| should be visible.  This is stored in case |host_|
  // is null when SetVisible() is called or if we don't have a
  // gfx::AcceleratedWidget to build an output surface from.
  bool host_should_be_visible_;

  // Whether there is an OutputSurface request pending from the current
  // |host_|. Becomes |true| if RequestNewOutputSurface is called, and |false|
  // if |host_| is deleted or we succeed in creating *and* initializing an
  // OutputSurface (which is essentially the contract with cc).
  bool output_surface_request_pending_;

  // To be notified of any incoming compositor protos that are specifically sent
  // to |render_widget_id_|.
  cc::RemoteProtoChannel::ProtoReceiver* remote_proto_channel_receiver_;

  // The bridge to the network layer that does the proto/RenderWidget id work.
  // TODO(dtrainor): Move this to a higher level once we start dealing with
  // multiple tabs.
  RenderWidgetMessageProcessor render_widget_processor_;

  DISALLOW_COPY_AND_ASSIGN(BlimpCompositor);
};

}  // namespace blimp

#endif  // BLIMP_CLIENT_COMPOSITOR_BLIMP_COMPOSITOR_H_
