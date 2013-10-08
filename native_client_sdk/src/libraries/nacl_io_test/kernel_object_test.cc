// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>

#include <map>
#include <string>

#include "nacl_io/kernel_handle.h"
#include "nacl_io/kernel_object.h"
#include "nacl_io/mount.h"
#include "nacl_io/path.h"

#include "gtest/gtest.h"

using namespace nacl_io;

namespace {

class MountNodeRefMock : public MountNode {
 public:
  MountNodeRefMock(Mount* mnt) : MountNode(mnt) {}
};

class MountRefMock : public Mount {
 public:
  MountRefMock() {}
  ~MountRefMock() {}

 public:
  Error Access(const Path& path, int a_mode) { return ENOSYS; }
  Error Open(const Path& path, int mode, ScopedMountNode* out_node) {
    out_node->reset(NULL);
    return ENOSYS;
  }
  Error Unlink(const Path& path) { return 0; }
  Error Mkdir(const Path& path, int permissions) { return 0; }
  Error Rmdir(const Path& path) { return 0; }
  Error Remove(const Path& path) { return 0; }
};

class KernelHandleRefMock : public KernelHandle {
 public:
  KernelHandleRefMock(const ScopedMount& mnt, const ScopedMountNode& node)
      : KernelHandle(mnt, node) {}

  ~KernelHandleRefMock() {}
};

class KernelObjectTest : public ::testing::Test {
 public:
  KernelObjectTest() {
    proxy = new KernelObject;
    mnt.reset(new MountRefMock());
    node.reset(new MountNodeRefMock(mnt.get()));
  }

  ~KernelObjectTest() {
    // mnt is ref-counted, it doesn't need to be explicitly deleted.
    node.reset(NULL);
    mnt.reset(NULL);
    delete proxy;
  }

  KernelObject* proxy;
  ScopedMount mnt;
  ScopedMountNode node;
};

}  // namespace

#include <nacl_io/mount_mem.h>
#include <nacl_io/mount_http.h>

TEST_F(KernelObjectTest, Referencing) {
  // The mount and node should have 1 ref count at this point
  EXPECT_EQ(1, mnt->RefCount());
  EXPECT_EQ(1, node->RefCount());

  // Pass the mount and node into a KernelHandle
  KernelHandle* raw_handle = new KernelHandleRefMock(mnt, node);
  ScopedKernelHandle handle_a(raw_handle);

  // The mount and node should have 1 ref count at this point
  EXPECT_EQ(1, handle_a->RefCount());
  EXPECT_EQ(2, mnt->RefCount());
  EXPECT_EQ(2, node->RefCount());

  ScopedKernelHandle handle_b = handle_a;

  // There should be two references to the KernelHandle, the mount and node
  // should be unchanged.
  EXPECT_EQ(2, handle_a->RefCount());
  EXPECT_EQ(2, handle_b->RefCount());
  EXPECT_EQ(handle_a.get(), handle_b.get());
  EXPECT_EQ(2, mnt->RefCount());
  EXPECT_EQ(2, node->RefCount());

  // Allocating an FD should cause the KernelProxy to ref the handle and
  // the node and mount should be unchanged.
  int fd1 = proxy->AllocateFD(handle_a);
  EXPECT_EQ(3, handle_a->RefCount());
  EXPECT_EQ(2, mnt->RefCount());
  EXPECT_EQ(2, node->RefCount());

  // If we "dup" the handle, we should bump the ref count on the handle
  int fd2 = proxy->AllocateFD(handle_b);
  EXPECT_EQ(4, handle_a->RefCount());
  EXPECT_EQ(2, mnt->RefCount());
  EXPECT_EQ(2, node->RefCount());

  // Handles are expected to come out in order
  EXPECT_EQ(0, fd1);
  EXPECT_EQ(1, fd2);

  // Now we "free" the handles, since the proxy should hold them.
  handle_a.reset(NULL);
  handle_b.reset(NULL);
  EXPECT_EQ(2, mnt->RefCount());
  EXPECT_EQ(2, node->RefCount());

  // We should find the handle by either fd
  EXPECT_EQ(0, proxy->AcquireHandle(fd1, &handle_a));
  EXPECT_EQ(0, proxy->AcquireHandle(fd2, &handle_b));
  EXPECT_EQ(raw_handle, handle_a.get());
  EXPECT_EQ(raw_handle, handle_b.get());

  EXPECT_EQ(4, handle_a->RefCount());
  EXPECT_EQ(2, mnt->RefCount());
  EXPECT_EQ(2, node->RefCount());

  // A non existent fd should fail, and handleA should decrement as handleB
  // is released by the call.
  EXPECT_EQ(EBADF, proxy->AcquireHandle(-1, &handle_b));
  EXPECT_EQ(NULL, handle_b.get());
  EXPECT_EQ(3, handle_a->RefCount());

  EXPECT_EQ(EBADF, proxy->AcquireHandle(100, &handle_b));
  EXPECT_EQ(NULL, handle_b.get());

  // Now only the KernelProxy should reference the KernelHandle in the
  // FD to KernelHandle Map.
  handle_a.reset();
  handle_b.reset();

  EXPECT_EQ(2, raw_handle->RefCount());
  EXPECT_EQ(2, mnt->RefCount());
  EXPECT_EQ(2, node->RefCount());
  proxy->FreeFD(fd2);
  EXPECT_EQ(1, raw_handle->RefCount());
  EXPECT_EQ(2, mnt->RefCount());
  EXPECT_EQ(2, node->RefCount());

  proxy->FreeFD(fd1);
  EXPECT_EQ(1, mnt->RefCount());
  EXPECT_EQ(1, node->RefCount());
}

TEST_F(KernelObjectTest, FreeAndReassignFD) {
  // The mount and node should have 1 ref count at this point
  EXPECT_EQ(1, mnt->RefCount());
  EXPECT_EQ(1, node->RefCount());

  KernelHandle* raw_handle = new KernelHandleRefMock(mnt, node);
  ScopedKernelHandle handle(raw_handle);

  EXPECT_EQ(2, mnt->RefCount());
  EXPECT_EQ(2, node->RefCount());
  EXPECT_EQ(1, raw_handle->RefCount());

  proxy->AllocateFD(handle);
  EXPECT_EQ(2, mnt->RefCount());
  EXPECT_EQ(2, node->RefCount());
  EXPECT_EQ(2, raw_handle->RefCount());

  proxy->FreeAndReassignFD(5, handle);
  EXPECT_EQ(2, mnt->RefCount());
  EXPECT_EQ(2, node->RefCount());
  EXPECT_EQ(3, raw_handle->RefCount());

  handle.reset();
  EXPECT_EQ(2, raw_handle->RefCount());

  proxy->AcquireHandle(5, &handle);
  EXPECT_EQ(3, raw_handle->RefCount());
  EXPECT_EQ(raw_handle, handle.get());
}

