// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/ref_counted.h"
#include "mojo/edk/system/test_utils.h"
#include "mojo/public/c/system/core.h"
#include "mojo/public/c/system/types.h"

namespace mojo {
namespace edk {
namespace {

const MojoHandleSignals kAllSignals = MOJO_HANDLE_SIGNAL_READABLE |
                                      MOJO_HANDLE_SIGNAL_WRITABLE |
                                      MOJO_HANDLE_SIGNAL_PEER_CLOSED;
static const char kHelloWorld[] = "hello world";

class MessagePipeTest : public test::MojoSystemTest {
 public:
  MessagePipeTest() {
    CHECK_EQ(MOJO_RESULT_OK, MojoCreateMessagePipe(nullptr, &pipe0_, &pipe1_));
  }

  ~MessagePipeTest() override {
    if (pipe0_ != MOJO_HANDLE_INVALID)
      CHECK_EQ(MOJO_RESULT_OK, MojoClose(pipe0_));
    if (pipe1_ != MOJO_HANDLE_INVALID)
      CHECK_EQ(MOJO_RESULT_OK, MojoClose(pipe1_));
  }

  MojoResult WriteMessage(MojoHandle message_pipe_handle,
                          const void* bytes,
                          uint32_t num_bytes) {
    return MojoWriteMessage(message_pipe_handle, bytes, num_bytes, nullptr, 0,
                            MOJO_WRITE_MESSAGE_FLAG_NONE);
  }

  MojoResult ReadMessage(MojoHandle message_pipe_handle,
                         void* bytes,
                         uint32_t* num_bytes,
                         bool may_discard = false) {
    return MojoReadMessage(message_pipe_handle, bytes, num_bytes, nullptr, 0,
                           may_discard ? MOJO_READ_MESSAGE_FLAG_MAY_DISCARD :
                                         MOJO_READ_MESSAGE_FLAG_NONE);
  }

  MojoHandle pipe0_, pipe1_;

 private:
  MOJO_DISALLOW_COPY_AND_ASSIGN(MessagePipeTest);
};

TEST_F(MessagePipeTest, WriteData) {
  ASSERT_EQ(MOJO_RESULT_OK,
            WriteMessage(pipe0_, kHelloWorld, sizeof(kHelloWorld)));
}

// Tests:
//  - only default flags
//  - reading messages from a port
//    - when there are no/one/two messages available for that port
//    - with buffer size 0 (and null buffer) -- should get size
//    - with too-small buffer -- should get size
//    - also verify that buffers aren't modified when/where they shouldn't be
//  - writing messages to a port
//    - in the obvious scenarios (as above)
//    - to a port that's been closed
//  - writing a message to a port, closing the other (would be the source) port,
//    and reading it
TEST_F(MessagePipeTest, Basic) {
  int32_t buffer[2];
  const uint32_t kBufferSize = static_cast<uint32_t>(sizeof(buffer));
  uint32_t buffer_size;

  // Nothing to read yet on port 0.
  buffer[0] = 123;
  buffer[1] = 456;
  buffer_size = kBufferSize;
  ASSERT_EQ(MOJO_RESULT_SHOULD_WAIT, ReadMessage(pipe0_, buffer, &buffer_size));
  ASSERT_EQ(kBufferSize, buffer_size);
  ASSERT_EQ(123, buffer[0]);
  ASSERT_EQ(456, buffer[1]);

  // Ditto for port 1.
  buffer[0] = 123;
  buffer[1] = 456;
  buffer_size = kBufferSize;
  ASSERT_EQ(MOJO_RESULT_SHOULD_WAIT, ReadMessage(pipe1_, buffer, &buffer_size));

  // Write from port 1 (to port 0).
  buffer[0] = 789012345;
  buffer[1] = 0;
  ASSERT_EQ(MOJO_RESULT_OK, WriteMessage(pipe1_, buffer, sizeof(buffer[0])));

  MojoHandleSignalsState state;
  ASSERT_EQ(MOJO_RESULT_OK, MojoWait(pipe0_, MOJO_HANDLE_SIGNAL_READABLE,
                                     MOJO_DEADLINE_INDEFINITE, &state));

  // Read from port 0.
  buffer[0] = 123;
  buffer[1] = 456;
  buffer_size = kBufferSize;
  ASSERT_EQ(MOJO_RESULT_OK, ReadMessage(pipe0_, buffer, &buffer_size));
  ASSERT_EQ(static_cast<uint32_t>(sizeof(buffer[0])), buffer_size);
  ASSERT_EQ(789012345, buffer[0]);
  ASSERT_EQ(456, buffer[1]);

  // Read again from port 0 -- it should be empty.
  buffer_size = kBufferSize;
  ASSERT_EQ(MOJO_RESULT_SHOULD_WAIT, ReadMessage(pipe0_, buffer, &buffer_size));

  // Write two messages from port 0 (to port 1).
  buffer[0] = 123456789;
  buffer[1] = 0;
  ASSERT_EQ(MOJO_RESULT_OK, WriteMessage(pipe0_, buffer, sizeof(buffer[0])));
  buffer[0] = 234567890;
  buffer[1] = 0;
  ASSERT_EQ(MOJO_RESULT_OK, WriteMessage(pipe0_, buffer, sizeof(buffer[0])));

  ASSERT_EQ(MOJO_RESULT_OK, MojoWait(pipe1_, MOJO_HANDLE_SIGNAL_READABLE,
                                     MOJO_DEADLINE_INDEFINITE, &state));

  // Read from port 1 with buffer size 0 (should get the size of next message).
  // Also test that giving a null buffer is okay when the buffer size is 0.
  buffer_size = 0;
  ASSERT_EQ(MOJO_RESULT_RESOURCE_EXHAUSTED,
            ReadMessage(pipe1_, nullptr, &buffer_size));
  ASSERT_EQ(static_cast<uint32_t>(sizeof(buffer[0])), buffer_size);

  // Read from port 1 with buffer size 1 (too small; should get the size of next
  // message).
  buffer[0] = 123;
  buffer[1] = 456;
  buffer_size = 1;
  ASSERT_EQ(MOJO_RESULT_RESOURCE_EXHAUSTED,
            ReadMessage(pipe1_, buffer, &buffer_size));
  ASSERT_EQ(static_cast<uint32_t>(sizeof(buffer[0])), buffer_size);
  ASSERT_EQ(123, buffer[0]);
  ASSERT_EQ(456, buffer[1]);

  // Read from port 1.
  buffer[0] = 123;
  buffer[1] = 456;
  buffer_size = kBufferSize;
  ASSERT_EQ(MOJO_RESULT_OK, ReadMessage(pipe1_, buffer, &buffer_size));
  ASSERT_EQ(static_cast<uint32_t>(sizeof(buffer[0])), buffer_size);
  ASSERT_EQ(123456789, buffer[0]);
  ASSERT_EQ(456, buffer[1]);

  ASSERT_EQ(MOJO_RESULT_OK, MojoWait(pipe1_, MOJO_HANDLE_SIGNAL_READABLE,
                                     MOJO_DEADLINE_INDEFINITE, &state));

  // Read again from port 1.
  buffer[0] = 123;
  buffer[1] = 456;
  buffer_size = kBufferSize;
  ASSERT_EQ(MOJO_RESULT_OK, ReadMessage(pipe1_, buffer, &buffer_size));
  ASSERT_EQ(static_cast<uint32_t>(sizeof(buffer[0])), buffer_size);
  ASSERT_EQ(234567890, buffer[0]);
  ASSERT_EQ(456, buffer[1]);

  // Read again from port 1 -- it should be empty.
  buffer_size = kBufferSize;
  ASSERT_EQ(MOJO_RESULT_SHOULD_WAIT, ReadMessage(pipe1_, buffer, &buffer_size));

  // Write from port 0 (to port 1).
  buffer[0] = 345678901;
  buffer[1] = 0;
  ASSERT_EQ(MOJO_RESULT_OK, WriteMessage(pipe0_, buffer, sizeof(buffer[0])));

  // Close port 0.
  MojoClose(pipe0_);
  pipe0_ = MOJO_HANDLE_INVALID;

  ASSERT_EQ(MOJO_RESULT_OK, MojoWait(pipe1_, MOJO_HANDLE_SIGNAL_PEER_CLOSED,
                                     MOJO_DEADLINE_INDEFINITE, &state));

  // Try to write from port 1 (to port 0).
  buffer[0] = 456789012;
  buffer[1] = 0;
  ASSERT_EQ(MOJO_RESULT_FAILED_PRECONDITION,
            WriteMessage(pipe1_, buffer, sizeof(buffer[0])));

  // Read from port 1; should still get message (even though port 0 was closed).
  buffer[0] = 123;
  buffer[1] = 456;
  buffer_size = kBufferSize;
  ASSERT_EQ(MOJO_RESULT_OK, ReadMessage(pipe1_, buffer, &buffer_size));
  ASSERT_EQ(static_cast<uint32_t>(sizeof(buffer[0])), buffer_size);
  ASSERT_EQ(345678901, buffer[0]);
  ASSERT_EQ(456, buffer[1]);

  // Read again from port 1 -- it should be empty (and port 0 is closed).
  buffer_size = kBufferSize;
  ASSERT_EQ(MOJO_RESULT_FAILED_PRECONDITION,
            ReadMessage(pipe1_, buffer, &buffer_size));
}

TEST_F(MessagePipeTest, CloseWithQueuedIncomingMessages) {
  int32_t buffer[1];
  const uint32_t kBufferSize = static_cast<uint32_t>(sizeof(buffer));
  uint32_t buffer_size;

  // Write some messages from port 1 (to port 0).
  for (int32_t i = 0; i < 5; i++) {
    buffer[0] = i;
    ASSERT_EQ(MOJO_RESULT_OK, WriteMessage(pipe1_, buffer, kBufferSize));
  }

  MojoHandleSignalsState state;
  ASSERT_EQ(MOJO_RESULT_OK, MojoWait(pipe0_, MOJO_HANDLE_SIGNAL_READABLE,
                                     MOJO_DEADLINE_INDEFINITE, &state));

  // Port 0 shouldn't be empty.
  buffer_size = 0;
  ASSERT_EQ(MOJO_RESULT_RESOURCE_EXHAUSTED,
            ReadMessage(pipe0_, nullptr, &buffer_size));
  ASSERT_EQ(kBufferSize, buffer_size);

  // Close port 0 first, which should have outstanding (incoming) messages.
  MojoClose(pipe0_);
  MojoClose(pipe1_);
  pipe0_ = pipe1_ = MOJO_HANDLE_INVALID;
}

TEST_F(MessagePipeTest, DiscardMode) {
  int32_t buffer[2];
  const uint32_t kBufferSize = static_cast<uint32_t>(sizeof(buffer));
  uint32_t buffer_size;

  // Write from port 1 (to port 0).
  buffer[0] = 789012345;
  buffer[1] = 0;
  ASSERT_EQ(MOJO_RESULT_OK, WriteMessage(pipe1_, buffer, sizeof(buffer[0])));

  MojoHandleSignalsState state;
  ASSERT_EQ(MOJO_RESULT_OK, MojoWait(pipe0_, MOJO_HANDLE_SIGNAL_READABLE,
                                     MOJO_DEADLINE_INDEFINITE, &state));

  // Read/discard from port 0 (no buffer); get size.
  buffer_size = 0;
  ASSERT_EQ(MOJO_RESULT_RESOURCE_EXHAUSTED,
            ReadMessage(pipe0_, nullptr, &buffer_size, true));
  ASSERT_EQ(static_cast<uint32_t>(sizeof(buffer[0])), buffer_size);

  // Read again from port 0 -- it should be empty.
  buffer_size = kBufferSize;
  ASSERT_EQ(MOJO_RESULT_SHOULD_WAIT,
            ReadMessage(pipe0_, buffer, &buffer_size, true));

  // Write from port 1 (to port 0).
  buffer[0] = 890123456;
  buffer[1] = 0;
  ASSERT_EQ(MOJO_RESULT_OK,
            WriteMessage(pipe1_, buffer, sizeof(buffer[0])));

  ASSERT_EQ(MOJO_RESULT_OK, MojoWait(pipe0_, MOJO_HANDLE_SIGNAL_READABLE,
                                     MOJO_DEADLINE_INDEFINITE, &state));

  // Read from port 0 (buffer big enough).
  buffer[0] = 123;
  buffer[1] = 456;
  buffer_size = kBufferSize;
  ASSERT_EQ(MOJO_RESULT_OK, ReadMessage(pipe0_, buffer, &buffer_size, true));
  ASSERT_EQ(static_cast<uint32_t>(sizeof(buffer[0])), buffer_size);
  ASSERT_EQ(890123456, buffer[0]);
  ASSERT_EQ(456, buffer[1]);

  // Read again from port 0 -- it should be empty.
  buffer_size = kBufferSize;
  ASSERT_EQ(MOJO_RESULT_SHOULD_WAIT,
            ReadMessage(pipe0_, buffer, &buffer_size, true));

  // Write from port 1 (to port 0).
  buffer[0] = 901234567;
  buffer[1] = 0;
  ASSERT_EQ(MOJO_RESULT_OK, WriteMessage(pipe1_, buffer, sizeof(buffer[0])));

  ASSERT_EQ(MOJO_RESULT_OK, MojoWait(pipe0_, MOJO_HANDLE_SIGNAL_READABLE,
                                     MOJO_DEADLINE_INDEFINITE, &state));

  // Read/discard from port 0 (buffer too small); get size.
  buffer_size = 1;
  ASSERT_EQ(MOJO_RESULT_RESOURCE_EXHAUSTED,
            ReadMessage(pipe0_, buffer, &buffer_size, true));
  ASSERT_EQ(static_cast<uint32_t>(sizeof(buffer[0])), buffer_size);

  // Read again from port 0 -- it should be empty.
  buffer_size = kBufferSize;
  ASSERT_EQ(MOJO_RESULT_SHOULD_WAIT,
            ReadMessage(pipe0_, buffer, &buffer_size, true));

  // Write from port 1 (to port 0).
  buffer[0] = 123456789;
  buffer[1] = 0;
  ASSERT_EQ(MOJO_RESULT_OK, WriteMessage(pipe1_, buffer, sizeof(buffer[0])));

  ASSERT_EQ(MOJO_RESULT_OK, MojoWait(pipe0_, MOJO_HANDLE_SIGNAL_READABLE,
                                     MOJO_DEADLINE_INDEFINITE, &state));

  // Discard from port 0.
  buffer_size = 1;
  ASSERT_EQ(MOJO_RESULT_RESOURCE_EXHAUSTED,
            ReadMessage(pipe0_, nullptr, 0, true));

  // Read again from port 0 -- it should be empty.
  buffer_size = kBufferSize;
  ASSERT_EQ(MOJO_RESULT_SHOULD_WAIT,
            ReadMessage(pipe0_, buffer, &buffer_size, true));
}

TEST_F(MessagePipeTest, BasicWaiting) {
  MojoHandleSignalsState hss;

  int32_t buffer[1];
  const uint32_t kBufferSize = static_cast<uint32_t>(sizeof(buffer));
  uint32_t buffer_size;

  // Always writable (until the other port is closed).
  hss = MojoHandleSignalsState();
  ASSERT_EQ(MOJO_RESULT_OK, MojoWait(pipe0_, MOJO_HANDLE_SIGNAL_WRITABLE, 0,
                                     &hss));
  ASSERT_EQ(MOJO_HANDLE_SIGNAL_WRITABLE, hss.satisfied_signals);
  ASSERT_EQ(kAllSignals, hss.satisfiable_signals);
  hss = MojoHandleSignalsState();

  // Not yet readable.
  ASSERT_EQ(MOJO_RESULT_DEADLINE_EXCEEDED,
            MojoWait(pipe0_, MOJO_HANDLE_SIGNAL_READABLE, 0, &hss));
  ASSERT_EQ(MOJO_HANDLE_SIGNAL_WRITABLE, hss.satisfied_signals);
  ASSERT_EQ(kAllSignals, hss.satisfiable_signals);

  // The peer is not closed.
  hss = MojoHandleSignalsState();
  ASSERT_EQ(MOJO_RESULT_DEADLINE_EXCEEDED,
            MojoWait(pipe0_, MOJO_HANDLE_SIGNAL_PEER_CLOSED, 0, &hss));
  ASSERT_EQ(MOJO_HANDLE_SIGNAL_WRITABLE, hss.satisfied_signals);
  ASSERT_EQ(kAllSignals, hss.satisfiable_signals);

  // Write from port 0 (to port 1), to make port 1 readable.
  buffer[0] = 123456789;
  ASSERT_EQ(MOJO_RESULT_OK, WriteMessage(pipe0_, buffer, kBufferSize));

  // Port 1 should already be readable now.
  ASSERT_EQ(MOJO_RESULT_OK, MojoWait(pipe1_, MOJO_HANDLE_SIGNAL_READABLE,
                                     MOJO_DEADLINE_INDEFINITE, &hss));
  ASSERT_EQ(MOJO_HANDLE_SIGNAL_READABLE | MOJO_HANDLE_SIGNAL_WRITABLE,
            hss.satisfied_signals);
  ASSERT_EQ(kAllSignals, hss.satisfiable_signals);
  // ... and still writable.
  hss = MojoHandleSignalsState();
  ASSERT_EQ(MOJO_RESULT_OK, MojoWait(pipe1_, MOJO_HANDLE_SIGNAL_WRITABLE,
                                     MOJO_DEADLINE_INDEFINITE, &hss));
  ASSERT_EQ(MOJO_HANDLE_SIGNAL_READABLE | MOJO_HANDLE_SIGNAL_WRITABLE,
            hss.satisfied_signals);
  ASSERT_EQ(kAllSignals, hss.satisfiable_signals);

  // Close port 0.
  MojoClose(pipe0_);
  pipe0_ = MOJO_HANDLE_INVALID;

  // Port 1 should be signaled with peer closed.
  hss = MojoHandleSignalsState();
  ASSERT_EQ(MOJO_RESULT_OK, MojoWait(pipe1_, MOJO_HANDLE_SIGNAL_PEER_CLOSED,
                                     MOJO_DEADLINE_INDEFINITE, &hss));
  ASSERT_EQ(MOJO_HANDLE_SIGNAL_READABLE | MOJO_HANDLE_SIGNAL_PEER_CLOSED,
            hss.satisfied_signals);
  ASSERT_EQ(MOJO_HANDLE_SIGNAL_READABLE | MOJO_HANDLE_SIGNAL_PEER_CLOSED,
            hss.satisfiable_signals);

  // Port 1 should not be writable.
  hss = MojoHandleSignalsState();

  ASSERT_EQ(MOJO_RESULT_FAILED_PRECONDITION,
            MojoWait(pipe1_, MOJO_HANDLE_SIGNAL_WRITABLE,
                     MOJO_DEADLINE_INDEFINITE, &hss));
  ASSERT_EQ(MOJO_HANDLE_SIGNAL_READABLE | MOJO_HANDLE_SIGNAL_PEER_CLOSED,
            hss.satisfied_signals);
  ASSERT_EQ(MOJO_HANDLE_SIGNAL_READABLE | MOJO_HANDLE_SIGNAL_PEER_CLOSED,
            hss.satisfiable_signals);

  // But it should still be readable.
  hss = MojoHandleSignalsState();
  ASSERT_EQ(MOJO_RESULT_OK, MojoWait(pipe1_, MOJO_HANDLE_SIGNAL_READABLE,
                                     MOJO_DEADLINE_INDEFINITE, &hss));
  ASSERT_EQ(MOJO_HANDLE_SIGNAL_READABLE | MOJO_HANDLE_SIGNAL_PEER_CLOSED,
            hss.satisfied_signals);
  ASSERT_EQ(MOJO_HANDLE_SIGNAL_READABLE | MOJO_HANDLE_SIGNAL_PEER_CLOSED,
            hss.satisfiable_signals);

  // Read from port 1.
  buffer[0] = 0;
  buffer_size = kBufferSize;
  ASSERT_EQ(MOJO_RESULT_OK, ReadMessage(pipe1_, buffer, &buffer_size));
  ASSERT_EQ(123456789, buffer[0]);

  // Now port 1 should no longer be readable.
  hss = MojoHandleSignalsState();
  ASSERT_EQ(MOJO_RESULT_FAILED_PRECONDITION,
            MojoWait(pipe1_, MOJO_HANDLE_SIGNAL_READABLE,
                     MOJO_DEADLINE_INDEFINITE, &hss));
  ASSERT_EQ(MOJO_HANDLE_SIGNAL_PEER_CLOSED, hss.satisfied_signals);
  ASSERT_EQ(MOJO_HANDLE_SIGNAL_PEER_CLOSED, hss.satisfiable_signals);
}

}  // namespace
}  // namespace edk
}  // namespace mojo
