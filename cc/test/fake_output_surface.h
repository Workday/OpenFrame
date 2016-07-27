// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_TEST_FAKE_OUTPUT_SURFACE_H_
#define CC_TEST_FAKE_OUTPUT_SURFACE_H_

#include "base/callback.h"
#include "base/logging.h"
#include "base/time/time.h"
#include "cc/output/begin_frame_args.h"
#include "cc/output/compositor_frame.h"
#include "cc/output/managed_memory_policy.h"
#include "cc/output/output_surface.h"
#include "cc/output/software_output_device.h"
#include "cc/test/test_context_provider.h"
#include "cc/test/test_web_graphics_context_3d.h"

namespace cc {

class FakeOutputSurface : public OutputSurface {
 public:
  ~FakeOutputSurface() override;

  static scoped_ptr<FakeOutputSurface> Create3d() {
    return make_scoped_ptr(
        new FakeOutputSurface(TestContextProvider::Create(),
                              TestContextProvider::CreateWorker(), false));
  }

  static scoped_ptr<FakeOutputSurface> Create3d(
      scoped_refptr<ContextProvider> context_provider) {
    return make_scoped_ptr(new FakeOutputSurface(
        context_provider, TestContextProvider::CreateWorker(), false));
  }

  static scoped_ptr<FakeOutputSurface> Create3d(
      scoped_refptr<ContextProvider> context_provider,
      scoped_refptr<ContextProvider> worker_context_provider) {
    return make_scoped_ptr(new FakeOutputSurface(
        context_provider, worker_context_provider, false));
  }

  static scoped_ptr<FakeOutputSurface> Create3d(
      scoped_ptr<TestWebGraphicsContext3D> context) {
    return make_scoped_ptr(
        new FakeOutputSurface(TestContextProvider::Create(std::move(context)),
                              TestContextProvider::CreateWorker(), false));
  }

  static scoped_ptr<FakeOutputSurface> CreateSoftware(
      scoped_ptr<SoftwareOutputDevice> software_device) {
    return make_scoped_ptr(
        new FakeOutputSurface(std::move(software_device), false));
  }

  static scoped_ptr<FakeOutputSurface> CreateDelegating3d() {
    return make_scoped_ptr(
        new FakeOutputSurface(TestContextProvider::Create(),
                              TestContextProvider::CreateWorker(), true));
  }

  static scoped_ptr<FakeOutputSurface> CreateDelegating3d(
      scoped_refptr<TestContextProvider> context_provider) {
    return make_scoped_ptr(new FakeOutputSurface(
        context_provider, TestContextProvider::CreateWorker(), true));
  }

  static scoped_ptr<FakeOutputSurface> CreateDelegating3d(
      scoped_ptr<TestWebGraphicsContext3D> context) {
    return make_scoped_ptr(
        new FakeOutputSurface(TestContextProvider::Create(std::move(context)),
                              TestContextProvider::CreateWorker(), true));
  }

  static scoped_ptr<FakeOutputSurface> CreateDelegatingSoftware(
      scoped_ptr<SoftwareOutputDevice> software_device) {
    return make_scoped_ptr(
        new FakeOutputSurface(std::move(software_device), true));
  }

  static scoped_ptr<FakeOutputSurface> CreateNoRequireSyncPoint(
      scoped_ptr<TestWebGraphicsContext3D> context) {
    scoped_ptr<FakeOutputSurface> surface(Create3d(std::move(context)));
    surface->capabilities_.delegated_sync_points_required = false;
    return surface;
  }

  static scoped_ptr<FakeOutputSurface> CreateOffscreen(
      scoped_ptr<TestWebGraphicsContext3D> context) {
    scoped_ptr<FakeOutputSurface> surface(new FakeOutputSurface(
        TestContextProvider::Create(std::move(context)), false));
    surface->capabilities_.uses_default_gl_framebuffer = false;
    return surface;
  }

  void set_max_frames_pending(int max) {
    capabilities_.max_frames_pending = max;
  }

  CompositorFrame& last_sent_frame() { return last_sent_frame_; }
  size_t num_sent_frames() { return num_sent_frames_; }

  void SwapBuffers(CompositorFrame* frame) override;

  OutputSurfaceClient* client() { return client_; }
  bool BindToClient(OutputSurfaceClient* client) override;

  void set_framebuffer(unsigned framebuffer) { framebuffer_ = framebuffer; }
  void BindFramebuffer() override;

  void SetTreeActivationCallback(const base::Closure& callback);

  const TransferableResourceArray& resources_held_by_parent() {
    return resources_held_by_parent_;
  }

  void ReturnResource(unsigned id, CompositorFrameAck* ack);

  bool HasExternalStencilTest() const override;

  bool SurfaceIsSuspendForRecycle() const override;

  OverlayCandidateValidator* GetOverlayCandidateValidator() const override;
  void SetOverlayCandidateValidator(OverlayCandidateValidator* validator) {
    overlay_candidate_validator_ = validator;
  }

  void set_has_external_stencil_test(bool has_test) {
    has_external_stencil_test_ = has_test;
  }

  void set_suspended_for_recycle(bool suspended) {
    suspended_for_recycle_ = suspended;
  }

  void SetMemoryPolicyToSetAtBind(
      scoped_ptr<ManagedMemoryPolicy> memory_policy_to_set_at_bind);

  gfx::Rect last_swap_rect() const {
    return last_swap_rect_;
  }

 protected:
  FakeOutputSurface(
      scoped_refptr<ContextProvider> context_provider,
      bool delegated_rendering);

  FakeOutputSurface(scoped_refptr<ContextProvider> context_provider,
                    scoped_refptr<ContextProvider> worker_context_provider,
                    bool delegated_rendering);

  FakeOutputSurface(scoped_ptr<SoftwareOutputDevice> software_device,
                    bool delegated_rendering);

  FakeOutputSurface(
      scoped_refptr<ContextProvider> context_provider,
      scoped_ptr<SoftwareOutputDevice> software_device,
      bool delegated_rendering);

  OutputSurfaceClient* client_;
  CompositorFrame last_sent_frame_;
  size_t num_sent_frames_;
  bool has_external_stencil_test_;
  bool suspended_for_recycle_;
  unsigned framebuffer_;
  TransferableResourceArray resources_held_by_parent_;
  scoped_ptr<ManagedMemoryPolicy> memory_policy_to_set_at_bind_;
  OverlayCandidateValidator* overlay_candidate_validator_;
  gfx::Rect last_swap_rect_;
};

}  // namespace cc

#endif  // CC_TEST_FAKE_OUTPUT_SURFACE_H_
