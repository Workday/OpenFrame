// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_RESOURCES_TRANSFERABLE_RESOURCE_H_
#define CC_RESOURCES_TRANSFERABLE_RESOURCE_H_

#include <vector>

#include "base/basictypes.h"
#include "cc/base/cc_export.h"
#include "gpu/command_buffer/common/mailbox.h"
#include "ui/gfx/size.h"

namespace cc {

struct CC_EXPORT TransferableResource {
  TransferableResource();
  ~TransferableResource();

  unsigned id;
  unsigned sync_point;
  uint32 format;
  uint32 filter;
  gfx::Size size;
  gpu::Mailbox mailbox;
};

typedef std::vector<TransferableResource> TransferableResourceArray;

}  // namespace cc

#endif  // CC_RESOURCES_TRANSFERABLE_RESOURCE_H_
