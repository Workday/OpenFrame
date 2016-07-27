// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_MOJO_SRC_MOJO_EDK_SYSTEM_CORE_TEST_BASE_H_
#define THIRD_PARTY_MOJO_SRC_MOJO_EDK_SYSTEM_CORE_TEST_BASE_H_

#include "mojo/public/c/system/types.h"
#include "mojo/public/cpp/system/macros.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/mojo/src/mojo/edk/embedder/simple_platform_support.h"
#include "third_party/mojo/src/mojo/edk/system/mutex.h"

namespace mojo {
namespace system {

class Core;
class Awakable;

namespace test {

class CoreTestBase_MockHandleInfo;

class CoreTestBase : public testing::Test {
 public:
  using MockHandleInfo = CoreTestBase_MockHandleInfo;

  CoreTestBase();
  ~CoreTestBase() override;

  void SetUp() override;
  void TearDown() override;

 protected:
  // |info| must remain alive until the returned handle is closed.
  MojoHandle CreateMockHandle(MockHandleInfo* info);

  Core* core() { return core_; }

 private:
  embedder::SimplePlatformSupport platform_support_;
  Core* core_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(CoreTestBase);
};

class CoreTestBase_MockHandleInfo {
 public:
  CoreTestBase_MockHandleInfo();
  ~CoreTestBase_MockHandleInfo();

  unsigned GetCtorCallCount() const;
  unsigned GetDtorCallCount() const;
  unsigned GetCloseCallCount() const;
  unsigned GetWriteMessageCallCount() const;
  unsigned GetReadMessageCallCount() const;
  unsigned GetWriteDataCallCount() const;
  unsigned GetBeginWriteDataCallCount() const;
  unsigned GetEndWriteDataCallCount() const;
  unsigned GetReadDataCallCount() const;
  unsigned GetBeginReadDataCallCount() const;
  unsigned GetEndReadDataCallCount() const;
  unsigned GetAddAwakableCallCount() const;
  unsigned GetRemoveAwakableCallCount() const;
  unsigned GetCancelAllAwakablesCallCount() const;

  size_t GetAddedAwakableSize() const;
  Awakable* GetAddedAwakableAt(unsigned i) const;

  // For use by |MockDispatcher|:
  void IncrementCtorCallCount();
  void IncrementDtorCallCount();
  void IncrementCloseCallCount();
  void IncrementWriteMessageCallCount();
  void IncrementReadMessageCallCount();
  void IncrementWriteDataCallCount();
  void IncrementBeginWriteDataCallCount();
  void IncrementEndWriteDataCallCount();
  void IncrementReadDataCallCount();
  void IncrementBeginReadDataCallCount();
  void IncrementEndReadDataCallCount();
  void IncrementAddAwakableCallCount();
  void IncrementRemoveAwakableCallCount();
  void IncrementCancelAllAwakablesCallCount();

  void AllowAddAwakable(bool alllow);
  bool IsAddAwakableAllowed() const;
  void AwakableWasAdded(Awakable*);

 private:
  mutable Mutex mutex_;
  unsigned ctor_call_count_ MOJO_GUARDED_BY(mutex_);
  unsigned dtor_call_count_ MOJO_GUARDED_BY(mutex_);
  unsigned close_call_count_ MOJO_GUARDED_BY(mutex_);
  unsigned write_message_call_count_ MOJO_GUARDED_BY(mutex_);
  unsigned read_message_call_count_ MOJO_GUARDED_BY(mutex_);
  unsigned write_data_call_count_ MOJO_GUARDED_BY(mutex_);
  unsigned begin_write_data_call_count_ MOJO_GUARDED_BY(mutex_);
  unsigned end_write_data_call_count_ MOJO_GUARDED_BY(mutex_);
  unsigned read_data_call_count_ MOJO_GUARDED_BY(mutex_);
  unsigned begin_read_data_call_count_ MOJO_GUARDED_BY(mutex_);
  unsigned end_read_data_call_count_ MOJO_GUARDED_BY(mutex_);
  unsigned add_awakable_call_count_ MOJO_GUARDED_BY(mutex_);
  unsigned remove_awakable_call_count_ MOJO_GUARDED_BY(mutex_);
  unsigned cancel_all_awakables_call_count_ MOJO_GUARDED_BY(mutex_);

  bool add_awakable_allowed_ MOJO_GUARDED_BY(mutex_);
  std::vector<Awakable*> added_awakables_ MOJO_GUARDED_BY(mutex_);

  MOJO_DISALLOW_COPY_AND_ASSIGN(CoreTestBase_MockHandleInfo);
};

}  // namespace test
}  // namespace system
}  // namespace mojo

#endif  // THIRD_PARTY_MOJO_SRC_MOJO_EDK_SYSTEM_CORE_TEST_BASE_H_
