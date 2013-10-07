// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_PEPPER_PPB_UMA_PRIVATE_IMPL_H_
#define CONTENT_RENDERER_PEPPER_PPB_UMA_PRIVATE_IMPL_H_

#include "ppapi/c/private/ppb_uma_private.h"

namespace content {

class PPB_UMA_Private_Impl {
 public:
  static const PPB_UMA_Private* GetInterface();
};

}  // namespace content

#endif  // CONTENT_RENDERER_PEPPER_PPB_UMA_PRIVATE_IMPL_H_
