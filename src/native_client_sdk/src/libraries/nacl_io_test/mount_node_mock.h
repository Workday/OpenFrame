// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBRARIES_NACL_IO_TEST_MOUNT_NODE_MOCK_H_
#define LIBRARIES_NACL_IO_TEST_MOUNT_NODE_MOCK_H_

#include "gmock/gmock.h"

#include "nacl_io/mount.h"

class MountNodeMock : public nacl_io::MountNode {
 public:
  typedef nacl_io::Error Error;
  typedef nacl_io::ScopedMountNode ScopedMountNode;

  explicit MountNodeMock(nacl_io::Mount*);
  virtual ~MountNodeMock();

  MOCK_METHOD1(Init, Error(int));
  MOCK_METHOD0(Destroy, void());
  MOCK_METHOD0(FSync, Error());
  MOCK_METHOD1(FTruncate, Error(off_t));
  MOCK_METHOD4(GetDents, Error(size_t, struct dirent*, size_t, int*));
  MOCK_METHOD1(GetStat, Error(struct stat*));
  MOCK_METHOD2(Ioctl, Error(int, char*));
  MOCK_METHOD4(Read, Error(size_t, void*, size_t, int*));
  MOCK_METHOD4(Write, Error(size_t, const void*, size_t, int*));
  MOCK_METHOD6(MMap, Error(void*, size_t, int, int, size_t, void**));
  MOCK_METHOD0(GetLinks, int());
  MOCK_METHOD0(GetMode, int());
  MOCK_METHOD0(GetType, int());
  MOCK_METHOD1(GetSize, Error(size_t*));
  MOCK_METHOD0(IsaDir, bool());
  MOCK_METHOD0(IsaFile, bool());
  MOCK_METHOD0(IsaTTY, bool());
  MOCK_METHOD0(ChildCount, int());
  MOCK_METHOD2(AddChild, Error(const std::string&, const ScopedMountNode&));
  MOCK_METHOD1(RemoveChild, Error(const std::string&));
  MOCK_METHOD2(FindChild, Error(const std::string&, ScopedMountNode*));
  MOCK_METHOD0(Link, void());
  MOCK_METHOD0(Unlink, void());
};

#endif  // LIBRARIES_NACL_IO_TEST_MOUNT_NODE_MOCK_H_
