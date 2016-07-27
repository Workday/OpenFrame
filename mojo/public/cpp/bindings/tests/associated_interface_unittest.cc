// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>

#include "base/bind.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/threading/thread.h"
#include "mojo/message_pump/message_pump_mojo.h"
#include "mojo/public/cpp/bindings/associated_binding.h"
#include "mojo/public/cpp/bindings/associated_group.h"
#include "mojo/public/cpp/bindings/associated_interface_ptr.h"
#include "mojo/public/cpp/bindings/associated_interface_ptr_info.h"
#include "mojo/public/cpp/bindings/associated_interface_request.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/bindings/lib/multiplex_router.h"
#include "mojo/public/interfaces/bindings/tests/test_associated_interfaces.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace mojo {
namespace test {
namespace {

using mojo::internal::AssociatedInterfacePtrInfoHelper;
using mojo::internal::AssociatedInterfaceRequestHelper;
using mojo::internal::MultiplexRouter;
using mojo::internal::ScopedInterfaceEndpointHandle;

class IntegerSenderImpl : public IntegerSender {
 public:
  explicit IntegerSenderImpl(AssociatedInterfaceRequest<IntegerSender> request)
      : binding_(this, request.Pass()) {}

  ~IntegerSenderImpl() override {}

  void set_notify_send_method_called(
      const base::Callback<void(int32_t)>& callback) {
    notify_send_method_called_ = callback;
  }

  void Echo(int32_t value, const EchoCallback& callback) override {
    callback.Run(value);
  }
  void Send(int32_t value) override { notify_send_method_called_.Run(value); }

  AssociatedBinding<IntegerSender>* binding() { return &binding_; }

  void set_connection_error_handler(const Closure& handler) {
    binding_.set_connection_error_handler(handler);
  }

 private:
  AssociatedBinding<IntegerSender> binding_;
  base::Callback<void(int32_t)> notify_send_method_called_;
};

class IntegerSenderConnectionImpl : public IntegerSenderConnection {
 public:
  explicit IntegerSenderConnectionImpl(
      InterfaceRequest<IntegerSenderConnection> request)
      : binding_(this, request.Pass()) {}

  ~IntegerSenderConnectionImpl() override {}

  void GetSender(AssociatedInterfaceRequest<IntegerSender> sender) override {
    IntegerSenderImpl* sender_impl = new IntegerSenderImpl(sender.Pass());
    sender_impl->set_connection_error_handler(
        [sender_impl]() { delete sender_impl; });
  }

  void AsyncGetSender(const AsyncGetSenderCallback& callback) override {
    AssociatedInterfaceRequest<IntegerSender> request;
    AssociatedInterfacePtrInfo<IntegerSender> ptr_info;
    binding_.associated_group()->CreateAssociatedInterface(
        AssociatedGroup::WILL_PASS_PTR, &ptr_info, &request);
    GetSender(request.Pass());
    callback.Run(ptr_info.Pass());
  }

 private:
  Binding<IntegerSenderConnection> binding_;
};

class AssociatedInterfaceTest : public testing::Test {
 public:
  AssociatedInterfaceTest() : loop_(common::MessagePumpMojo::Create()) {}
  ~AssociatedInterfaceTest() override { loop_.RunUntilIdle(); }

  void PumpMessages() { loop_.RunUntilIdle(); }

  template <typename T>
  AssociatedInterfacePtrInfo<T> EmulatePassingAssociatedPtrInfo(
      AssociatedInterfacePtrInfo<T> ptr_info,
      scoped_refptr<MultiplexRouter> target) {
    ScopedInterfaceEndpointHandle handle =
        AssociatedInterfacePtrInfoHelper::PassHandle(&ptr_info);
    CHECK(!handle.is_local());

    ScopedInterfaceEndpointHandle new_handle =
        target->CreateLocalEndpointHandle(handle.release());

    AssociatedInterfacePtrInfo<T> result;
    AssociatedInterfacePtrInfoHelper::SetHandle(&result, new_handle.Pass());
    result.set_version(ptr_info.version());
    return result.Pass();
  }

  template <typename T>
  AssociatedInterfaceRequest<T> EmulatePassingAssociatedRequest(
      AssociatedInterfaceRequest<T> request,
      scoped_refptr<MultiplexRouter> target) {
    ScopedInterfaceEndpointHandle handle =
        AssociatedInterfaceRequestHelper::PassHandle(&request);
    CHECK(!handle.is_local());

    ScopedInterfaceEndpointHandle new_handle =
        target->CreateLocalEndpointHandle(handle.release());

    AssociatedInterfaceRequest<T> result;
    AssociatedInterfaceRequestHelper::SetHandle(&result, new_handle.Pass());
    return result.Pass();
  }

  // Okay to call from any thread.
  void QuitRunLoop(base::RunLoop* run_loop) {
    if (loop_.task_runner()->BelongsToCurrentThread()) {
      run_loop->Quit();
    } else {
      loop_.PostTask(
          FROM_HERE,
          base::Bind(&AssociatedInterfaceTest::QuitRunLoop,
                     base::Unretained(this), base::Unretained(run_loop)));
    }
  }

 private:
  base::MessageLoop loop_;
};

TEST_F(AssociatedInterfaceTest, InterfacesAtBothEnds) {
  // Bind to the same pipe two associated interfaces, whose implementation lives
  // at different ends. Test that the two don't interfere with each other.

  MessagePipe pipe;
  scoped_refptr<MultiplexRouter> router0(
      new MultiplexRouter(true, pipe.handle0.Pass()));
  scoped_refptr<MultiplexRouter> router1(
      new MultiplexRouter(false, pipe.handle1.Pass()));

  AssociatedInterfaceRequest<IntegerSender> request;
  AssociatedInterfacePtrInfo<IntegerSender> ptr_info;

  router0->CreateAssociatedGroup()->CreateAssociatedInterface(
      AssociatedGroup::WILL_PASS_PTR, &ptr_info, &request);
  ptr_info = EmulatePassingAssociatedPtrInfo(ptr_info.Pass(), router1);

  IntegerSenderImpl impl0(request.Pass());
  AssociatedInterfacePtr<IntegerSender> ptr0;
  ptr0.Bind(ptr_info.Pass());

  router0->CreateAssociatedGroup()->CreateAssociatedInterface(
      AssociatedGroup::WILL_PASS_REQUEST, &ptr_info, &request);
  request = EmulatePassingAssociatedRequest(request.Pass(), router1);

  IntegerSenderImpl impl1(request.Pass());
  AssociatedInterfacePtr<IntegerSender> ptr1;
  ptr1.Bind(ptr_info.Pass());

  bool ptr0_callback_run = false;
  ptr0->Echo(123, [&ptr0_callback_run](int32_t value) {
    EXPECT_EQ(123, value);
    ptr0_callback_run = true;
  });

  bool ptr1_callback_run = false;
  ptr1->Echo(456, [&ptr1_callback_run](int32_t value) {
    EXPECT_EQ(456, value);
    ptr1_callback_run = true;
  });

  PumpMessages();
  EXPECT_TRUE(ptr0_callback_run);
  EXPECT_TRUE(ptr1_callback_run);

  bool ptr0_error_callback_run = false;
  ptr0.set_connection_error_handler(
      [&ptr0_error_callback_run]() { ptr0_error_callback_run = true; });

  impl0.binding()->Close();
  PumpMessages();
  EXPECT_TRUE(ptr0_error_callback_run);

  bool impl1_error_callback_run = false;
  impl1.binding()->set_connection_error_handler(
      [&impl1_error_callback_run]() { impl1_error_callback_run = true; });

  ptr1.reset();
  PumpMessages();
  EXPECT_TRUE(impl1_error_callback_run);
}

class TestSender {
 public:
  TestSender()
      : sender_thread_("TestSender"),
        next_sender_(nullptr),
        max_value_to_send_(-1) {
    base::Thread::Options thread_options;
    thread_options.message_pump_factory =
        base::Bind(&common::MessagePumpMojo::Create);
    sender_thread_.StartWithOptions(thread_options);
  }

  // The following three methods are called on the corresponding sender thread.
  void SetUp(AssociatedInterfacePtrInfo<IntegerSender> ptr_info,
             TestSender* next_sender,
             int32_t max_value_to_send) {
    CHECK(sender_thread_.task_runner()->BelongsToCurrentThread());

    ptr_.Bind(ptr_info.Pass());
    next_sender_ = next_sender ? next_sender : this;
    max_value_to_send_ = max_value_to_send;
  }

  void Send(int32_t value) {
    CHECK(sender_thread_.task_runner()->BelongsToCurrentThread());

    if (value > max_value_to_send_)
      return;

    ptr_->Send(value);

    next_sender_->sender_thread()->task_runner()->PostTask(
        FROM_HERE,
        base::Bind(&TestSender::Send, base::Unretained(next_sender_), ++value));
  }

  void TearDown() {
    CHECK(sender_thread_.task_runner()->BelongsToCurrentThread());

    ptr_.reset();
  }

  base::Thread* sender_thread() { return &sender_thread_; }

 private:
  base::Thread sender_thread_;
  TestSender* next_sender_;
  int32_t max_value_to_send_;

  AssociatedInterfacePtr<IntegerSender> ptr_;
};

class TestReceiver {
 public:
  TestReceiver() : receiver_thread_("TestReceiver"), max_value_to_receive_(-1) {
    base::Thread::Options thread_options;
    thread_options.message_pump_factory =
        base::Bind(&common::MessagePumpMojo::Create);
    receiver_thread_.StartWithOptions(thread_options);
  }

  void SetUp(AssociatedInterfaceRequest<IntegerSender> request0,
             AssociatedInterfaceRequest<IntegerSender> request1,
             int32_t max_value_to_receive,
             const base::Closure& notify_finish) {
    CHECK(receiver_thread_.task_runner()->BelongsToCurrentThread());

    impl0_.reset(new IntegerSenderImpl(request0.Pass()));
    impl0_->set_notify_send_method_called(
        base::Bind(&TestReceiver::SendMethodCalled, base::Unretained(this)));
    impl1_.reset(new IntegerSenderImpl(request1.Pass()));
    impl1_->set_notify_send_method_called(
        base::Bind(&TestReceiver::SendMethodCalled, base::Unretained(this)));

    max_value_to_receive_ = max_value_to_receive;
    notify_finish_ = notify_finish;
  }

  void TearDown() {
    CHECK(receiver_thread_.task_runner()->BelongsToCurrentThread());

    impl0_.reset();
    impl1_.reset();
  }

  base::Thread* receiver_thread() { return &receiver_thread_; }
  const std::vector<int32_t>& values() const { return values_; }

 private:
  void SendMethodCalled(int32_t value) {
    values_.push_back(value);

    if (value >= max_value_to_receive_)
      notify_finish_.Run();
  }

  base::Thread receiver_thread_;
  int32_t max_value_to_receive_;

  scoped_ptr<IntegerSenderImpl> impl0_;
  scoped_ptr<IntegerSenderImpl> impl1_;

  std::vector<int32_t> values_;

  base::Closure notify_finish_;
};

TEST_F(AssociatedInterfaceTest, MultiThreadAccess) {
  // Set up four associated interfaces on a message pipe. Use the inteface
  // pointers on four threads in parallel; run the interface implementations on
  // two threads. Test that multi-threaded access works.

  const int32_t kMaxValue = 1000;
  MessagePipe pipe;
  scoped_refptr<MultiplexRouter> router0(
      new MultiplexRouter(true, pipe.handle0.Pass()));
  scoped_refptr<MultiplexRouter> router1(
      new MultiplexRouter(false, pipe.handle1.Pass()));

  AssociatedInterfaceRequest<IntegerSender> requests[4];
  AssociatedInterfacePtrInfo<IntegerSender> ptr_infos[4];

  for (size_t i = 0; i < 4; ++i) {
    router0->CreateAssociatedGroup()->CreateAssociatedInterface(
        AssociatedGroup::WILL_PASS_PTR, &ptr_infos[i], &requests[i]);
    ptr_infos[i] =
        EmulatePassingAssociatedPtrInfo(ptr_infos[i].Pass(), router1);
  }

  TestSender senders[4];
  for (size_t i = 0; i < 4; ++i) {
    senders[i].sender_thread()->task_runner()->PostTask(
        FROM_HERE, base::Bind(&TestSender::SetUp, base::Unretained(&senders[i]),
                              base::Passed(&ptr_infos[i]), nullptr,
                              static_cast<int32_t>(kMaxValue * (i + 1) / 4)));
  }

  base::RunLoop run_loop;
  TestReceiver receivers[2];
  for (size_t i = 0; i < 2; ++i) {
    receivers[i].receiver_thread()->task_runner()->PostTask(
        FROM_HERE,
        base::Bind(
            &TestReceiver::SetUp, base::Unretained(&receivers[i]),
            base::Passed(&requests[2 * i]), base::Passed(&requests[2 * i + 1]),
            kMaxValue,
            base::Bind(&AssociatedInterfaceTest::QuitRunLoop,
                       base::Unretained(this), base::Unretained(&run_loop))));
  }

  for (size_t i = 0; i < 4; ++i) {
    senders[i].sender_thread()->task_runner()->PostTask(
        FROM_HERE, base::Bind(&TestSender::Send, base::Unretained(&senders[i]),
                              static_cast<int32_t>(kMaxValue * i / 4 + 1)));
  }

  run_loop.Run();

  for (size_t i = 0; i < 4; ++i) {
    base::RunLoop run_loop;
    senders[i].sender_thread()->task_runner()->PostTaskAndReply(
        FROM_HERE,
        base::Bind(&TestSender::TearDown, base::Unretained(&senders[i])),
        base::Bind(&AssociatedInterfaceTest::QuitRunLoop,
                   base::Unretained(this), base::Unretained(&run_loop)));
    run_loop.Run();
  }

  for (size_t i = 0; i < 2; ++i) {
    base::RunLoop run_loop;
    receivers[i].receiver_thread()->task_runner()->PostTaskAndReply(
        FROM_HERE,
        base::Bind(&TestReceiver::TearDown, base::Unretained(&receivers[i])),
        base::Bind(&AssociatedInterfaceTest::QuitRunLoop,
                   base::Unretained(this), base::Unretained(&run_loop)));
    run_loop.Run();
  }

  EXPECT_EQ(static_cast<size_t>(kMaxValue / 2), receivers[0].values().size());
  EXPECT_EQ(static_cast<size_t>(kMaxValue / 2), receivers[1].values().size());

  std::vector<int32_t> all_values;
  all_values.insert(all_values.end(), receivers[0].values().begin(),
                    receivers[0].values().end());
  all_values.insert(all_values.end(), receivers[1].values().begin(),
                    receivers[1].values().end());

  std::sort(all_values.begin(), all_values.end());
  for (size_t i = 0; i < all_values.size(); ++i)
    ASSERT_EQ(static_cast<int32_t>(i + 1), all_values[i]);
}

TEST_F(AssociatedInterfaceTest, FIFO) {
  // Set up four associated interfaces on a message pipe. Use the inteface
  // pointers on four threads; run the interface implementations on two threads.
  // Take turns to make calls using the four pointers. Test that FIFO-ness is
  // preserved.

  const int32_t kMaxValue = 100;
  MessagePipe pipe;
  scoped_refptr<MultiplexRouter> router0(
      new MultiplexRouter(true, pipe.handle0.Pass()));
  scoped_refptr<MultiplexRouter> router1(
      new MultiplexRouter(false, pipe.handle1.Pass()));

  AssociatedInterfaceRequest<IntegerSender> requests[4];
  AssociatedInterfacePtrInfo<IntegerSender> ptr_infos[4];

  for (size_t i = 0; i < 4; ++i) {
    router0->CreateAssociatedGroup()->CreateAssociatedInterface(
        AssociatedGroup::WILL_PASS_PTR, &ptr_infos[i], &requests[i]);
    ptr_infos[i] =
        EmulatePassingAssociatedPtrInfo(ptr_infos[i].Pass(), router1);
  }

  TestSender senders[4];
  for (size_t i = 0; i < 4; ++i) {
    senders[i].sender_thread()->task_runner()->PostTask(
        FROM_HERE,
        base::Bind(&TestSender::SetUp, base::Unretained(&senders[i]),
                   base::Passed(&ptr_infos[i]),
                   base::Unretained(&senders[(i + 1) % 4]), kMaxValue));
  }

  base::RunLoop run_loop;
  TestReceiver receivers[2];
  for (size_t i = 0; i < 2; ++i) {
    receivers[i].receiver_thread()->task_runner()->PostTask(
        FROM_HERE,
        base::Bind(
            &TestReceiver::SetUp, base::Unretained(&receivers[i]),
            base::Passed(&requests[2 * i]), base::Passed(&requests[2 * i + 1]),
            kMaxValue,
            base::Bind(&AssociatedInterfaceTest::QuitRunLoop,
                       base::Unretained(this), base::Unretained(&run_loop))));
  }

  senders[0].sender_thread()->task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&TestSender::Send, base::Unretained(&senders[0]), 1));

  run_loop.Run();

  for (size_t i = 0; i < 4; ++i) {
    base::RunLoop run_loop;
    senders[i].sender_thread()->task_runner()->PostTaskAndReply(
        FROM_HERE,
        base::Bind(&TestSender::TearDown, base::Unretained(&senders[i])),
        base::Bind(&AssociatedInterfaceTest::QuitRunLoop,
                   base::Unretained(this), base::Unretained(&run_loop)));
    run_loop.Run();
  }

  for (size_t i = 0; i < 2; ++i) {
    base::RunLoop run_loop;
    receivers[i].receiver_thread()->task_runner()->PostTaskAndReply(
        FROM_HERE,
        base::Bind(&TestReceiver::TearDown, base::Unretained(&receivers[i])),
        base::Bind(&AssociatedInterfaceTest::QuitRunLoop,
                   base::Unretained(this), base::Unretained(&run_loop)));
    run_loop.Run();
  }

  EXPECT_EQ(static_cast<size_t>(kMaxValue / 2), receivers[0].values().size());
  EXPECT_EQ(static_cast<size_t>(kMaxValue / 2), receivers[1].values().size());

  for (size_t i = 0; i < 2; ++i) {
    for (size_t j = 1; j < receivers[i].values().size(); ++j)
      EXPECT_LT(receivers[i].values()[j - 1], receivers[i].values()[j]);
  }
}

TEST_F(AssociatedInterfaceTest, PassAssociatedInterfaces) {
  IntegerSenderConnectionPtr connection_ptr;
  IntegerSenderConnectionImpl connection(GetProxy(&connection_ptr));

  IntegerSenderAssociatedPtr sender0;
  connection_ptr->GetSender(
      GetProxy(&sender0, connection_ptr.associated_group()));

  int32_t echoed_value = 0;
  sender0->Echo(123, [&echoed_value](int32_t value) { echoed_value = value; });
  PumpMessages();
  EXPECT_EQ(123, echoed_value);

  IntegerSenderAssociatedPtr sender1;
  connection_ptr->AsyncGetSender(
      [&sender1](AssociatedInterfacePtrInfo<IntegerSender> ptr_info) {
        sender1.Bind(ptr_info.Pass());
      });
  PumpMessages();
  EXPECT_TRUE(sender1);

  sender1->Echo(456, [&echoed_value](int32_t value) { echoed_value = value; });
  PumpMessages();
  EXPECT_EQ(456, echoed_value);
}

}  // namespace
}  // namespace test
}  // namespace mojo
