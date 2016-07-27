// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>

#include "base/message_loop/message_loop.h"
#include "mojo/message_pump/message_pump_mojo.h"
#include "mojo/public/cpp/bindings/lib/connector.h"
#include "mojo/public/cpp/bindings/lib/message_builder.h"
#include "mojo/public/cpp/bindings/tests/message_queue.h"
#include "mojo/public/cpp/system/macros.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace mojo {
namespace test {
namespace {

class MessageAccumulator : public MessageReceiver {
 public:
  MessageAccumulator() {}

  bool Accept(Message* message) override {
    queue_.Push(message);
    return true;
  }

  bool IsEmpty() const { return queue_.IsEmpty(); }

  void Pop(Message* message) { queue_.Pop(message); }

 private:
  MessageQueue queue_;
};

class ConnectorDeletingMessageAccumulator : public MessageAccumulator {
 public:
  ConnectorDeletingMessageAccumulator(internal::Connector** connector)
      : connector_(connector) {}

  bool Accept(Message* message) override {
    delete *connector_;
    *connector_ = nullptr;
    return MessageAccumulator::Accept(message);
  }

 private:
  internal::Connector** connector_;
};

class ReentrantMessageAccumulator : public MessageAccumulator {
 public:
  ReentrantMessageAccumulator(internal::Connector* connector)
      : connector_(connector), number_of_calls_(0) {}

  bool Accept(Message* message) override {
    if (!MessageAccumulator::Accept(message))
      return false;
    number_of_calls_++;
    if (number_of_calls_ == 1) {
      return connector_->WaitForIncomingMessage(MOJO_DEADLINE_INDEFINITE);
    }
    return true;
  }

  int number_of_calls() { return number_of_calls_; }

 private:
  internal::Connector* connector_;
  int number_of_calls_;
};

class ConnectorTest : public testing::Test {
 public:
  ConnectorTest() : loop_(common::MessagePumpMojo::Create()) {}

  void SetUp() override {
    CreateMessagePipe(nullptr, &handle0_, &handle1_);
  }

  void TearDown() override {}

  void AllocMessage(const char* text, Message* message) {
    size_t payload_size = strlen(text) + 1;  // Plus null terminator.
    internal::MessageBuilder builder(1, payload_size);
    memcpy(builder.buffer()->Allocate(payload_size), text, payload_size);

    builder.message()->MoveTo(message);
  }

  void PumpMessages() { loop_.RunUntilIdle(); }

 protected:
  ScopedMessagePipeHandle handle0_;
  ScopedMessagePipeHandle handle1_;

 private:
  base::MessageLoop loop_;
};

TEST_F(ConnectorTest, Basic) {
  internal::Connector connector0(handle0_.Pass(),
                                 internal::Connector::SINGLE_THREADED_SEND);
  internal::Connector connector1(handle1_.Pass(),
                                 internal::Connector::SINGLE_THREADED_SEND);

  const char kText[] = "hello world";

  Message message;
  AllocMessage(kText, &message);

  connector0.Accept(&message);

  MessageAccumulator accumulator;
  connector1.set_incoming_receiver(&accumulator);

  PumpMessages();

  ASSERT_FALSE(accumulator.IsEmpty());

  Message message_received;
  accumulator.Pop(&message_received);

  EXPECT_EQ(
      std::string(kText),
      std::string(reinterpret_cast<const char*>(message_received.payload())));
}

TEST_F(ConnectorTest, Basic_Synchronous) {
  internal::Connector connector0(handle0_.Pass(),
                                 internal::Connector::SINGLE_THREADED_SEND);
  internal::Connector connector1(handle1_.Pass(),
                                 internal::Connector::SINGLE_THREADED_SEND);

  const char kText[] = "hello world";

  Message message;
  AllocMessage(kText, &message);

  connector0.Accept(&message);

  MessageAccumulator accumulator;
  connector1.set_incoming_receiver(&accumulator);

  connector1.WaitForIncomingMessage(MOJO_DEADLINE_INDEFINITE);

  ASSERT_FALSE(accumulator.IsEmpty());

  Message message_received;
  accumulator.Pop(&message_received);

  EXPECT_EQ(
      std::string(kText),
      std::string(reinterpret_cast<const char*>(message_received.payload())));
}

TEST_F(ConnectorTest, Basic_EarlyIncomingReceiver) {
  internal::Connector connector0(handle0_.Pass(),
                                 internal::Connector::SINGLE_THREADED_SEND);
  internal::Connector connector1(handle1_.Pass(),
                                 internal::Connector::SINGLE_THREADED_SEND);

  MessageAccumulator accumulator;
  connector1.set_incoming_receiver(&accumulator);

  const char kText[] = "hello world";

  Message message;
  AllocMessage(kText, &message);

  connector0.Accept(&message);

  PumpMessages();

  ASSERT_FALSE(accumulator.IsEmpty());

  Message message_received;
  accumulator.Pop(&message_received);

  EXPECT_EQ(
      std::string(kText),
      std::string(reinterpret_cast<const char*>(message_received.payload())));
}

TEST_F(ConnectorTest, Basic_TwoMessages) {
  internal::Connector connector0(handle0_.Pass(),
                                 internal::Connector::SINGLE_THREADED_SEND);
  internal::Connector connector1(handle1_.Pass(),
                                 internal::Connector::SINGLE_THREADED_SEND);

  const char* kText[] = {"hello", "world"};

  for (size_t i = 0; i < MOJO_ARRAYSIZE(kText); ++i) {
    Message message;
    AllocMessage(kText[i], &message);

    connector0.Accept(&message);
  }

  MessageAccumulator accumulator;
  connector1.set_incoming_receiver(&accumulator);

  PumpMessages();

  for (size_t i = 0; i < MOJO_ARRAYSIZE(kText); ++i) {
    ASSERT_FALSE(accumulator.IsEmpty());

    Message message_received;
    accumulator.Pop(&message_received);

    EXPECT_EQ(
        std::string(kText[i]),
        std::string(reinterpret_cast<const char*>(message_received.payload())));
  }
}

TEST_F(ConnectorTest, Basic_TwoMessages_Synchronous) {
  internal::Connector connector0(handle0_.Pass(),
                                 internal::Connector::SINGLE_THREADED_SEND);
  internal::Connector connector1(handle1_.Pass(),
                                 internal::Connector::SINGLE_THREADED_SEND);

  const char* kText[] = {"hello", "world"};

  for (size_t i = 0; i < MOJO_ARRAYSIZE(kText); ++i) {
    Message message;
    AllocMessage(kText[i], &message);

    connector0.Accept(&message);
  }

  MessageAccumulator accumulator;
  connector1.set_incoming_receiver(&accumulator);

  connector1.WaitForIncomingMessage(MOJO_DEADLINE_INDEFINITE);

  ASSERT_FALSE(accumulator.IsEmpty());

  Message message_received;
  accumulator.Pop(&message_received);

  EXPECT_EQ(
      std::string(kText[0]),
      std::string(reinterpret_cast<const char*>(message_received.payload())));

  ASSERT_TRUE(accumulator.IsEmpty());
}

TEST_F(ConnectorTest, WriteToClosedPipe) {
  internal::Connector connector0(handle0_.Pass(),
                                 internal::Connector::SINGLE_THREADED_SEND);

  const char kText[] = "hello world";

  Message message;
  AllocMessage(kText, &message);

  // Close the other end of the pipe.
  handle1_.reset();

  // Not observed yet because we haven't spun the message loop yet.
  EXPECT_FALSE(connector0.encountered_error());

  // Write failures are not reported.
  bool ok = connector0.Accept(&message);
  EXPECT_TRUE(ok);

  // Still not observed.
  EXPECT_FALSE(connector0.encountered_error());

  // Spin the message loop, and then we should start observing the closed pipe.
  PumpMessages();

  EXPECT_TRUE(connector0.encountered_error());
}

TEST_F(ConnectorTest, MessageWithHandles) {
  internal::Connector connector0(handle0_.Pass(),
                                 internal::Connector::SINGLE_THREADED_SEND);
  internal::Connector connector1(handle1_.Pass(),
                                 internal::Connector::SINGLE_THREADED_SEND);

  const char kText[] = "hello world";

  Message message1;
  AllocMessage(kText, &message1);

  MessagePipe pipe;
  message1.mutable_handles()->push_back(pipe.handle0.release());

  connector0.Accept(&message1);

  // The message should have been transferred, releasing the handles.
  EXPECT_TRUE(message1.handles()->empty());

  MessageAccumulator accumulator;
  connector1.set_incoming_receiver(&accumulator);

  PumpMessages();

  ASSERT_FALSE(accumulator.IsEmpty());

  Message message_received;
  accumulator.Pop(&message_received);

  EXPECT_EQ(
      std::string(kText),
      std::string(reinterpret_cast<const char*>(message_received.payload())));
  ASSERT_EQ(1U, message_received.handles()->size());

  // Now send a message to the transferred handle and confirm it's sent through
  // to the orginal pipe.
  // TODO(vtl): Do we need a better way of "downcasting" the handle types?
  ScopedMessagePipeHandle smph;
  smph.reset(MessagePipeHandle(message_received.handles()->front().value()));
  message_received.mutable_handles()->front() = Handle();
  // |smph| now owns this handle.

  internal::Connector connector_received(
      smph.Pass(), internal::Connector::SINGLE_THREADED_SEND);
  internal::Connector connector_original(
      pipe.handle1.Pass(), internal::Connector::SINGLE_THREADED_SEND);

  Message message2;
  AllocMessage(kText, &message2);

  connector_received.Accept(&message2);
  connector_original.set_incoming_receiver(&accumulator);
  PumpMessages();

  ASSERT_FALSE(accumulator.IsEmpty());

  accumulator.Pop(&message_received);

  EXPECT_EQ(
      std::string(kText),
      std::string(reinterpret_cast<const char*>(message_received.payload())));
}

TEST_F(ConnectorTest, WaitForIncomingMessageWithError) {
  internal::Connector connector0(handle0_.Pass(),
                                 internal::Connector::SINGLE_THREADED_SEND);
  // Close the other end of the pipe.
  handle1_.reset();
  ASSERT_FALSE(connector0.WaitForIncomingMessage(MOJO_DEADLINE_INDEFINITE));
}

TEST_F(ConnectorTest, WaitForIncomingMessageWithDeletion) {
  internal::Connector connector0(handle0_.Pass(),
                                 internal::Connector::SINGLE_THREADED_SEND);
  internal::Connector* connector1 = new internal::Connector(
      handle1_.Pass(), internal::Connector::SINGLE_THREADED_SEND);

  const char kText[] = "hello world";

  Message message;
  AllocMessage(kText, &message);

  connector0.Accept(&message);

  ConnectorDeletingMessageAccumulator accumulator(&connector1);
  connector1->set_incoming_receiver(&accumulator);

  connector1->WaitForIncomingMessage(MOJO_DEADLINE_INDEFINITE);

  ASSERT_FALSE(connector1);
  ASSERT_FALSE(accumulator.IsEmpty());

  Message message_received;
  accumulator.Pop(&message_received);

  EXPECT_EQ(
      std::string(kText),
      std::string(reinterpret_cast<const char*>(message_received.payload())));
}

TEST_F(ConnectorTest, WaitForIncomingMessageWithReentrancy) {
  internal::Connector connector0(handle0_.Pass(),
                                 internal::Connector::SINGLE_THREADED_SEND);
  internal::Connector connector1(handle1_.Pass(),
                                 internal::Connector::SINGLE_THREADED_SEND);

  const char* kText[] = {"hello", "world"};

  for (size_t i = 0; i < MOJO_ARRAYSIZE(kText); ++i) {
    Message message;
    AllocMessage(kText[i], &message);

    connector0.Accept(&message);
  }

  ReentrantMessageAccumulator accumulator(&connector1);
  connector1.set_incoming_receiver(&accumulator);

  PumpMessages();

  for (size_t i = 0; i < MOJO_ARRAYSIZE(kText); ++i) {
    ASSERT_FALSE(accumulator.IsEmpty());

    Message message_received;
    accumulator.Pop(&message_received);

    EXPECT_EQ(
        std::string(kText[i]),
        std::string(reinterpret_cast<const char*>(message_received.payload())));
  }

  ASSERT_EQ(2, accumulator.number_of_calls());
}

TEST_F(ConnectorTest, RaiseError) {
  internal::Connector connector0(handle0_.Pass(),
                                 internal::Connector::SINGLE_THREADED_SEND);
  bool error_handler_called0 = false;
  connector0.set_connection_error_handler(
      [&error_handler_called0]() { error_handler_called0 = true; });

  internal::Connector connector1(handle1_.Pass(),
                                 internal::Connector::SINGLE_THREADED_SEND);
  bool error_handler_called1 = false;
  connector1.set_connection_error_handler(
      [&error_handler_called1]() { error_handler_called1 = true; });

  const char kText[] = "hello world";

  Message message;
  AllocMessage(kText, &message);

  connector0.Accept(&message);
  connector0.RaiseError();

  MessageAccumulator accumulator;
  connector1.set_incoming_receiver(&accumulator);

  PumpMessages();

  // Messages sent prior to RaiseError() still arrive at the other end.
  ASSERT_FALSE(accumulator.IsEmpty());

  Message message_received;
  accumulator.Pop(&message_received);

  EXPECT_EQ(
      std::string(kText),
      std::string(reinterpret_cast<const char*>(message_received.payload())));

  PumpMessages();

  // Connection error handler is called at both sides.
  EXPECT_TRUE(error_handler_called0);
  EXPECT_TRUE(error_handler_called1);

  // The error flag is set at both sides.
  EXPECT_TRUE(connector0.encountered_error());
  EXPECT_TRUE(connector1.encountered_error());

  // The message pipe handle is valid at both sides.
  EXPECT_TRUE(connector0.is_valid());
  EXPECT_TRUE(connector1.is_valid());
}

}  // namespace
}  // namespace test
}  // namespace mojo
