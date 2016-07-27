// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <string>

#include "base/callback_helpers.h"
#include "blimp/common/proto/blimp_message.pb.h"
#include "blimp/net/blimp_message_pump.h"
#include "blimp/net/common.h"
#include "blimp/net/connection_error_observer.h"
#include "blimp/net/test_common.h"
#include "net/base/completion_callback.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/base/test_completion_callback.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::_;
using testing::DoAll;
using testing::InSequence;
using testing::NotNull;
using testing::Return;
using testing::SaveArg;

namespace blimp {
namespace {

class BlimpMessagePumpTest : public testing::Test {
 public:
  BlimpMessagePumpTest()
      : message1_(new BlimpMessage), message2_(new BlimpMessage) {
    message1_->set_type(BlimpMessage::INPUT);
    message2_->set_type(BlimpMessage::CONTROL);
    message_pump_.reset(new BlimpMessagePump(&reader_));
    message_pump_->set_error_observer(&error_observer_);
  }

  ~BlimpMessagePumpTest() override {}

 protected:
  scoped_ptr<BlimpMessage> message1_;
  scoped_ptr<BlimpMessage> message2_;

  testing::StrictMock<MockPacketReader> reader_;
  testing::StrictMock<MockConnectionErrorObserver> error_observer_;
  testing::StrictMock<MockBlimpMessageProcessor> receiver_;
  scoped_ptr<BlimpMessagePump> message_pump_;
};

// Reader completes reading one packet asynchronously.
TEST_F(BlimpMessagePumpTest, ReadPacket) {
  net::CompletionCallback read_packet_cb;
  EXPECT_CALL(reader_, ReadPacket(NotNull(), _));
  EXPECT_CALL(reader_, ReadPacket(NotNull(), _))
      .WillOnce(DoAll(FillBufferFromMessage<0>(message1_.get()),
                      SetBufferOffset<0>(message1_->ByteSize()),
                      SaveArg<1>(&read_packet_cb)))
      .RetiresOnSaturation();
  net::CompletionCallback process_msg_cb;
  EXPECT_CALL(receiver_, MockableProcessMessage(EqualsProto(*message1_), _))
      .WillOnce(SaveArg<1>(&process_msg_cb));
  message_pump_->SetMessageProcessor(&receiver_);
  ASSERT_FALSE(read_packet_cb.is_null());
  base::ResetAndReturn(&read_packet_cb).Run(net::OK);
  process_msg_cb.Run(net::OK);
}

// Reader completes reading two packets asynchronously.
TEST_F(BlimpMessagePumpTest, ReadTwoPackets) {
  net::CompletionCallback read_packet_cb;
  EXPECT_CALL(reader_, ReadPacket(NotNull(), _))
      .WillOnce(DoAll(FillBufferFromMessage<0>(message1_.get()),
                      SetBufferOffset<0>(message1_->ByteSize()),
                      SaveArg<1>(&read_packet_cb)))
      .WillOnce(DoAll(FillBufferFromMessage<0>(message2_.get()),
                      SetBufferOffset<0>(message2_->ByteSize()),
                      SaveArg<1>(&read_packet_cb)));
  net::CompletionCallback process_msg_cb;
  {
    InSequence s;
    EXPECT_CALL(receiver_, MockableProcessMessage(EqualsProto(*message1_), _))
        .WillOnce(SaveArg<1>(&process_msg_cb))
        .RetiresOnSaturation();
    EXPECT_CALL(receiver_, MockableProcessMessage(EqualsProto(*message2_), _));
  }
  message_pump_->SetMessageProcessor(&receiver_);
  ASSERT_FALSE(read_packet_cb.is_null());
  base::ResetAndReturn(&read_packet_cb).Run(net::OK);

  // Trigger next packet read
  process_msg_cb.Run(net::OK);
  ASSERT_FALSE(read_packet_cb.is_null());
  base::ResetAndReturn(&read_packet_cb).Run(net::OK);
}

// Reader completes reading two packets asynchronously.
// The first read succeeds, and the second fails.
TEST_F(BlimpMessagePumpTest, ReadTwoPacketsWithError) {
  net::CompletionCallback process_msg_cb;
  net::CompletionCallback read_packet_cb;
  EXPECT_CALL(reader_, ReadPacket(NotNull(), _))
      .WillOnce(DoAll(FillBufferFromMessage<0>(message1_.get()),
                      SetBufferOffset<0>(message1_->ByteSize()),
                      SaveArg<1>(&read_packet_cb)))
      .WillOnce(DoAll(FillBufferFromMessage<0>(message2_.get()),
                      SetBufferOffset<0>(message2_->ByteSize()),
                      SaveArg<1>(&read_packet_cb)));
  EXPECT_CALL(receiver_, MockableProcessMessage(EqualsProto(*message1_), _))
      .WillOnce(SaveArg<1>(&process_msg_cb));
  EXPECT_CALL(error_observer_, OnConnectionError(net::ERR_FAILED));

  message_pump_->SetMessageProcessor(&receiver_);
  ASSERT_FALSE(read_packet_cb.is_null());
  base::ResetAndReturn(&read_packet_cb).Run(net::OK);

  // Trigger next packet read
  process_msg_cb.Run(net::OK);
  ASSERT_FALSE(read_packet_cb.is_null());
  base::ResetAndReturn(&read_packet_cb).Run(net::ERR_FAILED);
}

// Reader completes reading one packet synchronously, but packet is invalid
TEST_F(BlimpMessagePumpTest, InvalidPacket) {
  net::CompletionCallback read_packet_cb;
  std::string test_msg("msg");
  EXPECT_CALL(reader_, ReadPacket(NotNull(), _))
      .WillOnce(DoAll(FillBufferFromString<0>(test_msg),
                      SetBufferOffset<0>(test_msg.size()),
                      SaveArg<1>(&read_packet_cb)));
  EXPECT_CALL(error_observer_, OnConnectionError(net::ERR_FAILED));

  message_pump_->SetMessageProcessor(&receiver_);
  ASSERT_FALSE(read_packet_cb.is_null());
  base::ResetAndReturn(&read_packet_cb).Run(net::OK);
}

}  // namespace

}  // namespace blimp
