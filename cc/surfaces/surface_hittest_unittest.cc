// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/output/compositor_frame.h"
#include "cc/surfaces/surface.h"
#include "cc/surfaces/surface_factory.h"
#include "cc/surfaces/surface_factory_client.h"
#include "cc/surfaces/surface_hittest.h"
#include "cc/surfaces/surface_id_allocator.h"
#include "cc/surfaces/surface_manager.h"
#include "cc/test/surface_hittest_test_helpers.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/geometry/size.h"

namespace cc {

namespace {

struct TestCase {
  SurfaceId input_surface_id;
  gfx::Point input_point;
  SurfaceId expected_output_surface_id;
  gfx::Point expected_output_point;
};

void RunTests(SurfaceManager* manager, TestCase* tests, size_t test_count) {
  SurfaceHittest hittest(manager);
  for (size_t i = 0; i < test_count; ++i) {
    const TestCase& test = tests[i];
    gfx::Point point(test.input_point);
    gfx::Transform transform;
    EXPECT_EQ(test.expected_output_surface_id,
              hittest.GetTargetSurfaceAtPoint(test.input_surface_id, point,
                                              &transform));
    transform.TransformPoint(&point);
    EXPECT_EQ(test.expected_output_point, point);

    // Verify that GetTransformToTargetSurface returns true and returns the same
    // transform as returned by GetTargetSurfaceAtPoint.
    gfx::Transform target_transform;
    EXPECT_TRUE(hittest.GetTransformToTargetSurface(
        test.input_surface_id, test.expected_output_surface_id,
        &target_transform));
    EXPECT_EQ(transform, target_transform);
  }
}

}  // namespace

using namespace test;

// This test verifies that hit testing on a surface that does not exist does
// not crash.
TEST(SurfaceHittestTest, Hittest_BadCompositorFrameDoesNotCrash) {
  SurfaceManager manager;
  EmptySurfaceFactoryClient client;
  SurfaceFactory factory(&manager, &client);

  // Creates a root surface.
  gfx::Rect root_rect(300, 300);
  RenderPass* root_pass = nullptr;
  scoped_ptr<CompositorFrame> root_frame =
      CreateCompositorFrame(root_rect, &root_pass);

  // Add a reference to a non-existant child surface on the root surface.
  SurfaceIdAllocator child_allocator(3);
  SurfaceId child_surface_id;
  child_surface_id.id = 0xdeadbeef;
  gfx::Rect child_rect(200, 200);
  CreateSurfaceDrawQuad(root_pass,
                        gfx::Transform(),
                        root_rect,
                        child_rect,
                        child_surface_id);

  // Submit the root frame.
  SurfaceIdAllocator root_allocator(2);
  SurfaceId root_surface_id = root_allocator.GenerateId();
  factory.Create(root_surface_id);
  factory.SubmitCompositorFrame(root_surface_id, std::move(root_frame),
                                SurfaceFactory::DrawCallback());

  {
    SurfaceHittest hittest(&manager);
    // It is expected this test will complete without crashes.
    gfx::Transform transform;
    EXPECT_EQ(root_surface_id,
              hittest.GetTargetSurfaceAtPoint(
                  root_surface_id, gfx::Point(100, 100), &transform));
  }

  factory.Destroy(root_surface_id);
}

TEST(SurfaceHittestTest, Hittest_SingleSurface) {
  SurfaceManager manager;
  EmptySurfaceFactoryClient client;
  SurfaceFactory factory(&manager, &client);

  // Creates a root surface.
  gfx::Rect root_rect(300, 300);
  RenderPass* root_pass = nullptr;
  scoped_ptr<CompositorFrame> root_frame =
      CreateCompositorFrame(root_rect, &root_pass);

  // Submit the root frame.
  SurfaceIdAllocator root_allocator(2);
  SurfaceId root_surface_id = root_allocator.GenerateId();
  factory.Create(root_surface_id);
  factory.SubmitCompositorFrame(root_surface_id, std::move(root_frame),
                                SurfaceFactory::DrawCallback());
  TestCase tests[] = {
    {
      root_surface_id,
      gfx::Point(100, 100),
      root_surface_id,
      gfx::Point(100, 100)
    },
  };

  RunTests(&manager, tests, arraysize(tests));

  factory.Destroy(root_surface_id);
}

TEST(SurfaceHittestTest, Hittest_ChildSurface) {
  SurfaceManager manager;
  EmptySurfaceFactoryClient client;
  SurfaceFactory factory(&manager, &client);

  // Creates a root surface.
  gfx::Rect root_rect(300, 300);
  RenderPass* root_pass = nullptr;
  scoped_ptr<CompositorFrame> root_frame =
      CreateCompositorFrame(root_rect, &root_pass);

  // Add a reference to the child surface on the root surface.
  SurfaceIdAllocator child_allocator(3);
  SurfaceId child_surface_id = child_allocator.GenerateId();
  gfx::Rect child_rect(200, 200);
  CreateSurfaceDrawQuad(root_pass,
                        gfx::Transform(1.0f, 0.0f, 0.0f, 50.0f,
                                        0.0f, 1.0f, 0.0f, 50.0f,
                                        0.0f, 0.0f, 1.0f, 0.0f,
                                        0.0f, 0.0f, 0.0f, 1.0f),
                        root_rect,
                        child_rect,
                        child_surface_id);

  // Submit the root frame.
  SurfaceIdAllocator root_allocator(2);
  SurfaceId root_surface_id = root_allocator.GenerateId();
  factory.Create(root_surface_id);
  factory.SubmitCompositorFrame(root_surface_id, std::move(root_frame),
                                SurfaceFactory::DrawCallback());

  // Creates a child surface.
  RenderPass* child_pass = nullptr;
  scoped_ptr<CompositorFrame> child_frame =
      CreateCompositorFrame(child_rect, &child_pass);

  // Add a solid quad in the child surface.
  gfx::Rect child_solid_quad_rect(100, 100);
  CreateSolidColorDrawQuad(
      child_pass,
      gfx::Transform(1.0f, 0.0f, 0.0f, 50.0f,
                     0.0f, 1.0f, 0.0f, 50.0f,
                     0.0f, 0.0f, 1.0f, 0.0f,
                     0.0f, 0.0f, 0.0f, 1.0f),
      root_rect, child_solid_quad_rect);

  // Submit the frame.
  factory.Create(child_surface_id);
  factory.SubmitCompositorFrame(child_surface_id, std::move(child_frame),
                                SurfaceFactory::DrawCallback());

  TestCase tests[] = {
    {
      root_surface_id,
      gfx::Point(10, 10),
      root_surface_id,
      gfx::Point(10, 10)
    },
    {
      root_surface_id,
      gfx::Point(99, 99),
      root_surface_id,
      gfx::Point(99, 99)
    },
    {
      root_surface_id,
      gfx::Point(100, 100),
      child_surface_id,
      gfx::Point(50, 50)
    },
    {
      root_surface_id,
      gfx::Point(199, 199),
      child_surface_id,
      gfx::Point(149, 149)
    },
    {
      root_surface_id,
      gfx::Point(200, 200),
      root_surface_id,
      gfx::Point(200, 200)
    },
    {
      root_surface_id,
      gfx::Point(290, 290),
      root_surface_id,
      gfx::Point(290, 290)
    }
  };

  RunTests(&manager, tests, arraysize(tests));

  // Submit another root frame, with a slightly perturbed child Surface.
  root_frame = CreateCompositorFrame(root_rect, &root_pass);
  CreateSurfaceDrawQuad(root_pass,
                        gfx::Transform(1.0f, 0.0f, 0.0f, 75.0f,
                                        0.0f, 1.0f, 0.0f, 75.0f,
                                        0.0f, 0.0f, 1.0f, 0.0f,
                                        0.0f, 0.0f, 0.0f, 1.0f),
                        root_rect,
                        child_rect,
                        child_surface_id);
  factory.SubmitCompositorFrame(root_surface_id, std::move(root_frame),
                                SurfaceFactory::DrawCallback());

  // Verify that point (100, 100) no longer falls on the child surface.
  // Verify that the transform to the child surface's space has also shifted.
  {
    SurfaceHittest hittest(&manager);

    gfx::Point point(100, 100);
    gfx::Transform transform;
    EXPECT_EQ(root_surface_id,
              hittest.GetTargetSurfaceAtPoint(root_surface_id, point,
                                              &transform));
    transform.TransformPoint(&point);
    EXPECT_EQ(gfx::Point(100, 100), point);

    gfx::Point point_in_target_space(100, 100);
    gfx::Transform target_transform;
    EXPECT_TRUE(hittest.GetTransformToTargetSurface(
        root_surface_id, child_surface_id, &target_transform));
    target_transform.TransformPoint(&point_in_target_space);
    EXPECT_NE(transform, target_transform);
    EXPECT_EQ(gfx::Point(25, 25), point_in_target_space);
  }

  factory.Destroy(root_surface_id);
  factory.Destroy(child_surface_id);
}

// This test verifies that hit testing will progress to the next quad if it
// encounters an invalid RenderPassDrawQuad for whatever reason.
TEST(SurfaceHittestTest, Hittest_InvalidRenderPassDrawQuad) {
  SurfaceManager manager;
  EmptySurfaceFactoryClient client;
  SurfaceFactory factory(&manager, &client);

  // Creates a root surface.
  gfx::Rect root_rect(300, 300);
  RenderPass* root_pass = nullptr;
  scoped_ptr<CompositorFrame> root_frame =
      CreateCompositorFrame(root_rect, &root_pass);

  // Create a RenderPassDrawQuad to a non-existant RenderPass.
  CreateRenderPassDrawQuad(root_pass,
                           gfx::Transform(),
                           root_rect,
                           root_rect,
                           RenderPassId(1337, 1337));

  // Add a reference to the child surface on the root surface.
  SurfaceIdAllocator child_allocator(3);
  SurfaceId child_surface_id = child_allocator.GenerateId();
  gfx::Rect child_rect(200, 200);
  CreateSurfaceDrawQuad(root_pass,
                        gfx::Transform(1.0f, 0.0f, 0.0f, 50.0f,
                                       0.0f, 1.0f, 0.0f, 50.0f,
                                       0.0f, 0.0f, 1.0f, 0.0f,
                                       0.0f, 0.0f, 0.0f, 1.0f),
                        root_rect,
                        child_rect,
                        child_surface_id);

  // Submit the root frame.
  SurfaceIdAllocator root_allocator(2);
  SurfaceId root_surface_id = root_allocator.GenerateId();
  factory.Create(root_surface_id);
  factory.SubmitCompositorFrame(root_surface_id, std::move(root_frame),
                                SurfaceFactory::DrawCallback());

  // Creates a child surface.
  RenderPass* child_pass = nullptr;
  scoped_ptr<CompositorFrame> child_frame =
      CreateCompositorFrame(child_rect, &child_pass);

  // Add a solid quad in the child surface.
  gfx::Rect child_solid_quad_rect(100, 100);
  CreateSolidColorDrawQuad(child_pass,
                           gfx::Transform(1.0f, 0.0f, 0.0f, 50.0f,
                                         0.0f, 1.0f, 0.0f, 50.0f,
                                         0.0f, 0.0f, 1.0f, 0.0f,
                                         0.0f, 0.0f, 0.0f, 1.0f),
                           root_rect,
                           child_solid_quad_rect);

  // Submit the frame.
  factory.Create(child_surface_id);
  factory.SubmitCompositorFrame(child_surface_id, std::move(child_frame),
                                SurfaceFactory::DrawCallback());

  TestCase tests[] = {
    {
      root_surface_id,
      gfx::Point(10, 10),
      root_surface_id,
      gfx::Point(10, 10)
    },
    {
      root_surface_id,
      gfx::Point(99, 99),
      root_surface_id,
      gfx::Point(99, 99)
    },
    {
      root_surface_id,
      gfx::Point(100, 100),
      child_surface_id,
      gfx::Point(50, 50)
    },
    {
      root_surface_id,
      gfx::Point(199, 199),
      child_surface_id,
      gfx::Point(149, 149)
    },
    {
      root_surface_id,
      gfx::Point(200, 200),
      root_surface_id,
      gfx::Point(200, 200)
    },
    {
      root_surface_id,
      gfx::Point(290, 290),
      root_surface_id,
      gfx::Point(290, 290)
    }
  };

  RunTests(&manager, tests, arraysize(tests));

  factory.Destroy(root_surface_id);
  factory.Destroy(child_surface_id);
}

TEST(SurfaceHittestTest, Hittest_RenderPassDrawQuad) {
  SurfaceManager manager;
  EmptySurfaceFactoryClient client;
  SurfaceFactory factory(&manager, &client);

  // Create a CompostiorFrame with two RenderPasses.
  gfx::Rect root_rect(300, 300);
  RenderPassList render_pass_list;

  // Create a child RenderPass.
  RenderPassId child_render_pass_id(1, 3);
  gfx::Transform transform_to_root_target(1.0f, 0.0f, 0.0f, 50.0f,
                                          0.0f, 1.0f, 0.0f, 50.0f,
                                          0.0f, 0.0f, 1.0f, 0.0f,
                                          0.0f, 0.0f, 0.0f, 1.0f);
  CreateRenderPass(child_render_pass_id,
                   gfx::Rect(100, 100),
                   transform_to_root_target,
                   &render_pass_list);

  // Create the root RenderPass.
  RenderPassId root_render_pass_id(1, 2);
  CreateRenderPass(root_render_pass_id, root_rect, gfx::Transform(),
                   &render_pass_list);

  RenderPass* root_pass = nullptr;
  scoped_ptr<CompositorFrame> root_frame =
      CreateCompositorFrameWithRenderPassList(&render_pass_list);
  root_pass = root_frame->delegated_frame_data->render_pass_list.back().get();

  // Create a RenderPassDrawQuad.
  gfx::Rect render_pass_quad_rect(100, 100);
  CreateRenderPassDrawQuad(root_pass,
                           transform_to_root_target,
                           root_rect,
                           render_pass_quad_rect,
                           child_render_pass_id);

  // Add a solid quad in the child render pass.
  RenderPass* child_render_pass =
      root_frame->delegated_frame_data->render_pass_list.front().get();
  gfx::Rect child_solid_quad_rect(100, 100);
  CreateSolidColorDrawQuad(child_render_pass,
                           gfx::Transform(),
                           gfx::Rect(100, 100),
                           child_solid_quad_rect);

  // Submit the root frame.
  SurfaceIdAllocator root_allocator(1);
  SurfaceId root_surface_id = root_allocator.GenerateId();
  factory.Create(root_surface_id);
  factory.SubmitCompositorFrame(root_surface_id, std::move(root_frame),
                                SurfaceFactory::DrawCallback());

  TestCase tests[] = {
    // These tests just miss the RenderPassDrawQuad.
    {
      root_surface_id,
      gfx::Point(49, 49),
      root_surface_id,
      gfx::Point(49, 49)
    },
    {
      root_surface_id,
      gfx::Point(150, 150),
      root_surface_id,
      gfx::Point(150, 150)
    },
    // These tests just hit the boundaries of the
    // RenderPassDrawQuad.
    {
      root_surface_id,
      gfx::Point(50, 50),
      root_surface_id,
      gfx::Point(50, 50)
    },
    {
      root_surface_id,
      gfx::Point(149, 149),
      root_surface_id,
      gfx::Point(149, 149)
    },
    // These tests fall somewhere in the center of the
    // RenderPassDrawQuad.
    {
      root_surface_id,
      gfx::Point(99, 99),
      root_surface_id,
      gfx::Point(99, 99)
    },
    {
      root_surface_id,
      gfx::Point(100, 100),
      root_surface_id,
      gfx::Point(100, 100)
    }
  };

  RunTests(&manager, tests, arraysize(tests));

  factory.Destroy(root_surface_id);
}

}  // namespace cc
