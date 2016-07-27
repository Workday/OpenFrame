// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/mus/gles2/mojo_gpu_memory_buffer_manager.h"

#include "base/logging.h"
#include "components/mus/gles2/mojo_gpu_memory_buffer.h"

namespace mus {

MojoGpuMemoryBufferManager::MojoGpuMemoryBufferManager() {}

MojoGpuMemoryBufferManager::~MojoGpuMemoryBufferManager() {}

scoped_ptr<gfx::GpuMemoryBuffer>
MojoGpuMemoryBufferManager::AllocateGpuMemoryBuffer(const gfx::Size& size,
                                                    gfx::BufferFormat format,
                                                    gfx::BufferUsage usage) {
  return MojoGpuMemoryBufferImpl::Create(size, format, usage);
}

scoped_ptr<gfx::GpuMemoryBuffer>
MojoGpuMemoryBufferManager::CreateGpuMemoryBufferFromHandle(
    const gfx::GpuMemoryBufferHandle& handle,
    const gfx::Size& size,
    gfx::BufferFormat format) {
  NOTIMPLEMENTED();
  return nullptr;
}

gfx::GpuMemoryBuffer*
MojoGpuMemoryBufferManager::GpuMemoryBufferFromClientBuffer(
    ClientBuffer buffer) {
  return MojoGpuMemoryBufferImpl::FromClientBuffer(buffer);
}

void MojoGpuMemoryBufferManager::SetDestructionSyncToken(
    gfx::GpuMemoryBuffer* buffer,
    const gpu::SyncToken& sync_token) {
  NOTIMPLEMENTED();
}

}  // namespace mus
