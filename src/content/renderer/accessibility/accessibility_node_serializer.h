// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_ACCESSIBILITY_ACCESSIBILITY_NODE_SERIALIZER_H_
#define CONTENT_RENDERER_ACCESSIBILITY_ACCESSIBILITY_NODE_SERIALIZER_H_

#include "content/common/accessibility_node_data.h"
#include "third_party/WebKit/public/web/WebAccessibilityObject.h"

namespace content {

void SerializeAccessibilityNode(
    const WebKit::WebAccessibilityObject& src,
    AccessibilityNodeData* dst);

bool ShouldIncludeChildNode(
    const WebKit::WebAccessibilityObject& parent,
    const WebKit::WebAccessibilityObject& child);

}  // namespace content

#endif  // CONTENT_RENDERER_ACCESSIBILITY_ACCESSIBILITY_NODE_SERIALIZER_H_
