// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/test/fake_layer_tree_host_client.h"

#include "cc/output/context_provider.h"
#include "cc/test/test_web_graphics_context_3d.h"

namespace cc {

FakeLayerTreeHostClient::FakeLayerTreeHostClient(RendererOptions options)
    : use_software_rendering_(options == DIRECT_SOFTWARE ||
                              options == DELEGATED_SOFTWARE),
      use_delegating_renderer_(options == DELEGATED_3D ||
                               options == DELEGATED_SOFTWARE) {}

FakeLayerTreeHostClient::~FakeLayerTreeHostClient() {}

scoped_ptr<OutputSurface> FakeLayerTreeHostClient::CreateOutputSurface(
    bool fallback) {
  if (use_software_rendering_) {
    if (use_delegating_renderer_) {
      return FakeOutputSurface::CreateDelegatingSoftware(
          make_scoped_ptr(new SoftwareOutputDevice)).PassAs<OutputSurface>();
    }

    return FakeOutputSurface::CreateSoftware(
        make_scoped_ptr(new SoftwareOutputDevice)).PassAs<OutputSurface>();
  }

  if (use_delegating_renderer_)
    return FakeOutputSurface::CreateDelegating3d().PassAs<OutputSurface>();

  return CreateFakeOutputSurface();
}

scoped_refptr<cc::ContextProvider> FakeLayerTreeHostClient::
    OffscreenContextProviderForMainThread() {
  if (!main_thread_contexts_.get() ||
      main_thread_contexts_->DestroyedOnMainThread()) {
    main_thread_contexts_ = FakeContextProvider::Create();
    if (!main_thread_contexts_->BindToCurrentThread())
      main_thread_contexts_ = NULL;
  }
  return main_thread_contexts_;
}

scoped_refptr<cc::ContextProvider> FakeLayerTreeHostClient::
    OffscreenContextProviderForCompositorThread() {
  if (!compositor_thread_contexts_.get() ||
      compositor_thread_contexts_->DestroyedOnMainThread())
    compositor_thread_contexts_ = FakeContextProvider::Create();
  return compositor_thread_contexts_;
}

}  // namespace cc
