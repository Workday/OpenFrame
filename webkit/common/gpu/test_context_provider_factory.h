// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_COMMON_GPU_TEST_CONTEXT_PROVIDER_FACTORY_H_
#define WEBKIT_COMMON_GPU_TEST_CONTEXT_PROVIDER_FACTORY_H_

#include "base/memory/ref_counted.h"
#include "webkit/common/gpu/webkit_gpu_export.h"

namespace cc {
class ContextProvider;
}

namespace webkit {
namespace gpu {
class ContextProviderInProcess;

class WEBKIT_GPU_EXPORT TestContextProviderFactory {
 public:
  // The returned pointer is static and should not be deleted by the caller.
  static TestContextProviderFactory* GetInstance();

  scoped_refptr<cc::ContextProvider> OffscreenContextProviderForMainThread();

 private:
  TestContextProviderFactory();
  ~TestContextProviderFactory();

  scoped_refptr<webkit::gpu::ContextProviderInProcess> main_thread_;

  DISALLOW_COPY_AND_ASSIGN(TestContextProviderFactory);
};

}  // namespace gpu
}  // namespace webkit

#endif  // WEBKIT_COMMON_GPU_TEST_CONTEXT_PROVIDER_FACTORY_H_
