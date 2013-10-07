// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "nacl_io/mount_node.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/stat.h>

#include <algorithm>
#include <string>

#include "nacl_io/kernel_wrap_real.h"
#include "nacl_io/mount.h"
#include "nacl_io/osmman.h"
#include "sdk_util/auto_lock.h"

namespace nacl_io {

static const int USR_ID = 1001;
static const int GRP_ID = 1002;

MountNode::MountNode(Mount* mount) : mount_(mount) {
  memset(&stat_, 0, sizeof(stat_));
  stat_.st_gid = GRP_ID;
  stat_.st_uid = USR_ID;

  // Mount should normally never be NULL, but may be null in tests.
  // If NULL, at least set the inode to a valid (nonzero) value.
  if (mount_)
    mount_->OnNodeCreated(this);
  else
    stat_.st_ino = 1;
}

MountNode::~MountNode() {}

Error MountNode::Init(int perm) {
  stat_.st_mode |= perm;
  return 0;
}

void MountNode::Destroy() {
  if (mount_) {
    mount_->OnNodeDestroyed(this);
  }
}

// Declared in EventEmitter, default to regular files which always return
// a ready of TRUE for read, write, or error.
uint32_t MountNode::GetEventStatus() {
  uint32_t val = POLLIN | POLLOUT | POLLERR;
  return val;
}


Error MountNode::FSync() { return 0; }

Error MountNode::FTruncate(off_t length) { return EINVAL; }

Error MountNode::GetDents(size_t offs,
                          struct dirent* pdir,
                          size_t count,
                          int* out_bytes) {
  *out_bytes = 0;
  return ENOTDIR;
}

Error MountNode::GetStat(struct stat* pstat) {
  AUTO_LOCK(node_lock_);
  memcpy(pstat, &stat_, sizeof(stat_));
  return 0;
}

Error MountNode::Ioctl(int request, char* arg) { return EINVAL; }

Error MountNode::Read(size_t offs, void* buf, size_t count, int* out_bytes) {
  *out_bytes = 0;
  return EINVAL;
}

Error MountNode::Write(size_t offs,
                       const void* buf,
                       size_t count,
                       int* out_bytes) {
  *out_bytes = 0;
  return EINVAL;
}

Error MountNode::MMap(void* addr,
                      size_t length,
                      int prot,
                      int flags,
                      size_t offset,
                      void** out_addr) {
  *out_addr = NULL;

  // Never allow mmap'ing PROT_EXEC. The passthrough node supports this, but we
  // don't. Fortunately, glibc will fallback if this fails, so dlopen will
  // continue to work.
  if (prot & PROT_EXEC)
    return EPERM;

  // This default mmap support is just enough to make dlopen work.
  // This implementation just reads from the mount into the mmap'd memory area.
  void* new_addr = addr;
  int mmap_error = _real_mmap(
      &new_addr, length, prot | PROT_WRITE, flags | MAP_ANONYMOUS, -1, 0);
  if (new_addr == MAP_FAILED) {
    _real_munmap(new_addr, length);
    return mmap_error;
  }

  int bytes_read;
  Error read_error = Read(offset, new_addr, length, &bytes_read);
  if (read_error) {
    _real_munmap(new_addr, length);
    return read_error;
  }

  *out_addr = new_addr;
  return 0;
}

Error MountNode::Tcflush(int queue_selector) {
  return EINVAL;
}

Error MountNode::Tcgetattr(struct termios* termios_p) {
  return EINVAL;
}

Error MountNode::Tcsetattr(int optional_actions,
                           const struct termios *termios_p) {
  return EINVAL;
}

int MountNode::GetLinks() { return stat_.st_nlink; }

int MountNode::GetMode() { return stat_.st_mode & ~S_IFMT; }

Error MountNode::GetSize(size_t* out_size) {
  *out_size = stat_.st_size;
  return 0;
}

int MountNode::GetType() { return stat_.st_mode & S_IFMT; }

bool MountNode::IsaDir() { return (stat_.st_mode & S_IFDIR) != 0; }

bool MountNode::IsaFile() { return (stat_.st_mode & S_IFREG) != 0; }

bool MountNode::IsaTTY() { return (stat_.st_mode & S_IFCHR) != 0; }

Error MountNode::AddChild(const std::string& name,
                          const ScopedMountNode& node) {
  return ENOTDIR;
}

Error MountNode::RemoveChild(const std::string& name) { return ENOTDIR; }

Error MountNode::FindChild(const std::string& name, ScopedMountNode* out_node) {
  out_node->reset(NULL);
  return ENOTDIR;
}

int MountNode::ChildCount() { return 0; }

void MountNode::Link() { stat_.st_nlink++; }

void MountNode::Unlink() { stat_.st_nlink--; }

}  // namespace nacl_io

