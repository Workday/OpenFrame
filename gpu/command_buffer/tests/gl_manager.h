// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_COMMAND_BUFFER_TESTS_GL_MANAGER_H_
#define GPU_COMMAND_BUFFER_TESTS_GL_MANAGER_H_

#include "base/containers/scoped_ptr_hash_map.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "gpu/command_buffer/client/gpu_control.h"
#include "gpu/command_buffer/common/gles2_cmd_utils.h"
#include "gpu/command_buffer/service/feature_info.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/gpu_memory_buffer.h"

namespace base {
class CommandLine;
}

namespace gfx {

class GLContext;
class GLShareGroup;
class GLSurface;

}

namespace gpu {

class CommandBufferService;
class GpuScheduler;
class SyncPointClient;
class SyncPointOrderData;
class SyncPointManager;
class TransferBuffer;

namespace gles2 {

class ContextGroup;
class MailboxManager;
class GLES2Decoder;
class GLES2CmdHelper;
class GLES2Implementation;
class ImageManager;
class ShareGroup;

};

class GLManager : private GpuControl {
 public:
  struct Options {
    Options();
    // The size of the backbuffer.
    gfx::Size size;
    // If not null will have a corresponding sync point manager.
    SyncPointManager* sync_point_manager;
    // If not null will share resources with this context.
    GLManager* share_group_manager;
    // If not null will share a mailbox manager with this context.
    GLManager* share_mailbox_manager;
    // If not null will create a virtual manager based on this context.
    GLManager* virtual_manager;
    // Whether or not glBindXXX generates a resource.
    bool bind_generates_resource;
    // Whether or not the context is auto-lost when GL_OUT_OF_MEMORY occurs.
    bool lose_context_when_out_of_memory;
    // Whether or not it's ok to lose the context.
    bool context_lost_allowed;
    gles2::ContextType context_type;
    // Force shader name hashing for all context types.
    bool force_shader_name_hashing;
  };
  GLManager();
  ~GLManager() override;

  static scoped_ptr<gfx::GpuMemoryBuffer> CreateGpuMemoryBuffer(
      const gfx::Size& size,
      gfx::BufferFormat format);

  void Initialize(const Options& options);
  void InitializeWithCommandLine(const Options& options,
                                 base::CommandLine* command_line);
  void Destroy();

  bool IsInitialized() const { return gles2_implementation() != nullptr; }

  void MakeCurrent();

  void SetSurface(gfx::GLSurface* surface);

  gles2::GLES2Decoder* decoder() const {
    return decoder_.get();
  }

  gles2::MailboxManager* mailbox_manager() const {
    return mailbox_manager_.get();
  }

  gfx::GLShareGroup* share_group() const {
    return share_group_.get();
  }

  gles2::GLES2Implementation* gles2_implementation() const {
    return gles2_implementation_.get();
  }

  gfx::GLContext* context() {
    return context_.get();
  }

  const gpu::gles2::FeatureInfo::Workarounds& workarounds() const;

  // GpuControl implementation.
  Capabilities GetCapabilities() override;
  int32 CreateImage(ClientBuffer buffer,
                    size_t width,
                    size_t height,
                    unsigned internalformat) override;
  void DestroyImage(int32 id) override;
  int32 CreateGpuMemoryBufferImage(size_t width,
                                   size_t height,
                                   unsigned internalformat,
                                   unsigned usage) override;
  uint32 InsertSyncPoint() override;
  uint32 InsertFutureSyncPoint() override;
  void RetireSyncPoint(uint32 sync_point) override;
  void SignalSyncPoint(uint32 sync_point,
                       const base::Closure& callback) override;
  void SignalQuery(uint32 query, const base::Closure& callback) override;
  void SetLock(base::Lock*) override;
  bool IsGpuChannelLost() override;
  gpu::CommandBufferNamespace GetNamespaceID() const override;
  uint64_t GetCommandBufferID() const override;
  uint64_t GenerateFenceSyncRelease() override;
  bool IsFenceSyncRelease(uint64_t release) override;
  bool IsFenceSyncFlushed(uint64_t release) override;
  bool IsFenceSyncFlushReceived(uint64_t release) override;
  void SignalSyncToken(const gpu::SyncToken& sync_token,
                       const base::Closure& callback) override;
  bool CanWaitUnverifiedSyncToken(const gpu::SyncToken* sync_token) override;

 private:
  void PumpCommands();
  bool GetBufferChanged(int32 transfer_buffer_id);
  void SetupBaseContext();
  void OnFenceSyncRelease(uint64_t release);
  bool OnWaitFenceSync(gpu::CommandBufferNamespace namespace_id,
                       uint64_t command_buffer_id,
                       uint64_t release);

  SyncPointManager* sync_point_manager_;  // Non-owning.

  scoped_refptr<SyncPointOrderData> sync_point_order_data_;
  scoped_ptr<SyncPointClient> sync_point_client_;
  scoped_refptr<gles2::MailboxManager> mailbox_manager_;
  scoped_refptr<gfx::GLShareGroup> share_group_;
  scoped_ptr<CommandBufferService> command_buffer_;
  scoped_ptr<gles2::GLES2Decoder> decoder_;
  scoped_ptr<GpuScheduler> gpu_scheduler_;
  scoped_refptr<gfx::GLSurface> surface_;
  scoped_refptr<gfx::GLContext> context_;
  scoped_ptr<gles2::GLES2CmdHelper> gles2_helper_;
  scoped_ptr<TransferBuffer> transfer_buffer_;
  scoped_ptr<gles2::GLES2Implementation> gles2_implementation_;
  bool context_lost_allowed_;

  const uint64_t command_buffer_id_;
  uint64_t next_fence_sync_release_;

  // Used on Android to virtualize GL for all contexts.
  static int use_count_;
  static scoped_refptr<gfx::GLShareGroup>* base_share_group_;
  static scoped_refptr<gfx::GLSurface>* base_surface_;
  static scoped_refptr<gfx::GLContext>* base_context_;
};

}  // namespace gpu

#endif  // GPU_COMMAND_BUFFER_TESTS_GL_MANAGER_H_
