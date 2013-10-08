// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_LAYERS_QUAD_SINK_H_
#define CC_LAYERS_QUAD_SINK_H_

#include "base/memory/scoped_ptr.h"
#include "cc/base/cc_export.h"

namespace cc {

class DrawQuad;
class SharedQuadState;

struct AppendQuadsData;

class CC_EXPORT QuadSink {
 public:
  virtual ~QuadSink() {}

  // Call this to add a SharedQuadState before appending quads that refer to it.
  // Returns a pointer to the given SharedQuadState for convenience, that can be
  // set on the quads to append.
  virtual SharedQuadState* UseSharedQuadState(
      scoped_ptr<SharedQuadState> shared_quad_state) = 0;

  // Returns true if the quad is added to the list, and false if the quad is
  // entirely culled.
  virtual bool Append(scoped_ptr<DrawQuad> draw_quad,
                      AppendQuadsData* append_quads_data) = 0;
};

}  // namespace cc

#endif  // CC_LAYERS_QUAD_SINK_H_
