// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/gpu/client/gpu_memory_buffer_impl_io_surface.h"

#include "base/logging.h"
#include "content/common/gpu/gpu_memory_buffer_factory_io_surface.h"
#include "ui/gfx/buffer_format_util.h"
#include "ui/gfx/mac/io_surface_manager.h"

namespace content {
namespace {

uint32_t LockFlags(gfx::BufferUsage usage) {
  switch (usage) {
    case gfx::BufferUsage::GPU_READ_CPU_READ_WRITE:
      return kIOSurfaceLockAvoidSync;
    case gfx::BufferUsage::GPU_READ:
    case gfx::BufferUsage::SCANOUT:
    case gfx::BufferUsage::GPU_READ_CPU_READ_WRITE_PERSISTENT:
      return 0;
  }
  NOTREACHED();
  return 0;
}

void FreeIOSurfaceForTesting(gfx::GpuMemoryBufferId id) {
  gfx::IOSurfaceManager::GetInstance()->UnregisterIOSurface(id, 0);
}

}  // namespace

GpuMemoryBufferImplIOSurface::GpuMemoryBufferImplIOSurface(
    gfx::GpuMemoryBufferId id,
    const gfx::Size& size,
    gfx::BufferFormat format,
    const DestructionCallback& callback,
    IOSurfaceRef io_surface,
    uint32_t lock_flags)
    : GpuMemoryBufferImpl(id, size, format, callback),
      io_surface_(io_surface),
      lock_flags_(lock_flags) {}

GpuMemoryBufferImplIOSurface::~GpuMemoryBufferImplIOSurface() {
}

// static
scoped_ptr<GpuMemoryBufferImplIOSurface>
GpuMemoryBufferImplIOSurface::CreateFromHandle(
    const gfx::GpuMemoryBufferHandle& handle,
    const gfx::Size& size,
    gfx::BufferFormat format,
    gfx::BufferUsage usage,
    const DestructionCallback& callback) {
  base::ScopedCFTypeRef<IOSurfaceRef> io_surface(
      gfx::IOSurfaceManager::GetInstance()->AcquireIOSurface(handle.id));
  if (!io_surface)
    return nullptr;

  return make_scoped_ptr(
      new GpuMemoryBufferImplIOSurface(handle.id, size, format, callback,
                                       io_surface.release(), LockFlags(usage)));
}

// static
bool GpuMemoryBufferImplIOSurface::IsConfigurationSupported(
    gfx::BufferFormat format,
    gfx::BufferUsage usage) {
  return GpuMemoryBufferFactoryIOSurface::
      IsGpuMemoryBufferConfigurationSupported(format, usage);
}

// static
base::Closure GpuMemoryBufferImplIOSurface::AllocateForTesting(
    const gfx::Size& size,
    gfx::BufferFormat format,
    gfx::BufferUsage usage,
    gfx::GpuMemoryBufferHandle* handle) {
  base::ScopedCFTypeRef<IOSurfaceRef> io_surface(
      gfx::IOSurfaceManager::CreateIOSurface(size, format));
  DCHECK(io_surface);
  gfx::GpuMemoryBufferId kBufferId(1);
  bool rv = gfx::IOSurfaceManager::GetInstance()->RegisterIOSurface(
      kBufferId, 0, io_surface);
  DCHECK(rv);
  handle->type = gfx::IO_SURFACE_BUFFER;
  handle->id = kBufferId;
  return base::Bind(&FreeIOSurfaceForTesting, kBufferId);
}

bool GpuMemoryBufferImplIOSurface::Map() {
  DCHECK(!mapped_);
  IOReturn status = IOSurfaceLock(io_surface_, lock_flags_, NULL);
  DCHECK_NE(status, kIOReturnCannotLock);
  mapped_ = true;
  return true;
}

void* GpuMemoryBufferImplIOSurface::memory(size_t plane) {
  DCHECK(mapped_);
  DCHECK_LT(plane, gfx::NumberOfPlanesForBufferFormat(format_));
  return IOSurfaceGetBaseAddressOfPlane(io_surface_, plane);
}

void GpuMemoryBufferImplIOSurface::Unmap() {
  DCHECK(mapped_);
  IOSurfaceUnlock(io_surface_, lock_flags_, NULL);
  mapped_ = false;
}

int GpuMemoryBufferImplIOSurface::stride(size_t plane) const {
  DCHECK_LT(plane, gfx::NumberOfPlanesForBufferFormat(format_));
  return IOSurfaceGetBytesPerRowOfPlane(io_surface_, plane);
}

gfx::GpuMemoryBufferHandle GpuMemoryBufferImplIOSurface::GetHandle() const {
  gfx::GpuMemoryBufferHandle handle;
  handle.type = gfx::IO_SURFACE_BUFFER;
  handle.id = id_;
  return handle;
}

}  // namespace content
