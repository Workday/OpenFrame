// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/test/simple_test_tick_clock.h"
#include "base/time/tick_clock.h"
#include "media/cast/cast_environment.h"
#include "media/cast/logging/logging_defines.h"
#include "media/cast/logging/receiver_time_offset_estimator_impl.h"
#include "media/cast/test/fake_single_thread_task_runner.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace media {
namespace cast {

class ReceiverTimeOffsetEstimatorImplTest : public ::testing::Test {
 protected:
  ReceiverTimeOffsetEstimatorImplTest()
      : sender_clock_(new base::SimpleTestTickClock()),
        task_runner_(new test::FakeSingleThreadTaskRunner(sender_clock_)),
        cast_environment_(new CastEnvironment(
            scoped_ptr<base::TickClock>(sender_clock_).Pass(),
            task_runner_,
            task_runner_,
            task_runner_)) {
    cast_environment_->logger()->Subscribe(&estimator_);
  }

  ~ReceiverTimeOffsetEstimatorImplTest() override {
    cast_environment_->logger()->Unsubscribe(&estimator_);
  }

  void AdvanceClocks(base::TimeDelta time) {
    task_runner_->Sleep(time);
    receiver_clock_.Advance(time);
  }

  base::SimpleTestTickClock* sender_clock_;  // Owned by CastEnvironment.
  scoped_refptr<test::FakeSingleThreadTaskRunner> task_runner_;
  scoped_refptr<CastEnvironment> cast_environment_;
  base::SimpleTestTickClock receiver_clock_;
  ReceiverTimeOffsetEstimatorImpl estimator_;
};

// Suppose the true offset is 100ms.
// Event A occurred at sender time 20ms.
// Event B occurred at receiver time 130ms. (sender time 30ms)
// Event C occurred at sender time 60ms.
// Then the bound after all 3 events have arrived is [130-60=70, 130-20=110].
TEST_F(ReceiverTimeOffsetEstimatorImplTest, EstimateOffset) {
  int64 true_offset_ms = 100;
  receiver_clock_.Advance(base::TimeDelta::FromMilliseconds(true_offset_ms));

  base::TimeDelta lower_bound;
  base::TimeDelta upper_bound;

  EXPECT_FALSE(estimator_.GetReceiverOffsetBounds(&lower_bound, &upper_bound));

  RtpTimestamp rtp_timestamp = 0;
  uint32 frame_id = 0;

  AdvanceClocks(base::TimeDelta::FromMilliseconds(20));

  scoped_ptr<FrameEvent> encode_event(new FrameEvent());
  encode_event->timestamp = sender_clock_->NowTicks();
  encode_event->type = FRAME_ENCODED;
  encode_event->media_type = VIDEO_EVENT;
  encode_event->rtp_timestamp = rtp_timestamp;
  encode_event->frame_id = frame_id;
  encode_event->size = 1234;
  encode_event->key_frame = true;
  encode_event->target_bitrate = 5678;
  encode_event->encoder_cpu_utilization = 9.10;
  encode_event->idealized_bitrate_utilization = 11.12;
  cast_environment_->logger()->DispatchFrameEvent(encode_event.Pass());

  scoped_ptr<PacketEvent> send_event(new PacketEvent());
  send_event->timestamp = sender_clock_->NowTicks();
  send_event->type = PACKET_SENT_TO_NETWORK;
  send_event->media_type = VIDEO_EVENT;
  send_event->rtp_timestamp = rtp_timestamp;
  send_event->frame_id = frame_id;
  send_event->packet_id = 56;
  send_event->max_packet_id = 78;
  send_event->size = 1500;
  cast_environment_->logger()->DispatchPacketEvent(send_event.Pass());

  EXPECT_FALSE(estimator_.GetReceiverOffsetBounds(&lower_bound, &upper_bound));

  AdvanceClocks(base::TimeDelta::FromMilliseconds(10));
  scoped_ptr<FrameEvent> ack_sent_event(new FrameEvent());
  ack_sent_event->timestamp = receiver_clock_.NowTicks();
  ack_sent_event->type = FRAME_ACK_SENT;
  ack_sent_event->media_type = VIDEO_EVENT;
  ack_sent_event->rtp_timestamp = rtp_timestamp;
  ack_sent_event->frame_id = frame_id;
  cast_environment_->logger()->DispatchFrameEvent(ack_sent_event.Pass());

  scoped_ptr<PacketEvent> receive_event(new PacketEvent());
  receive_event->timestamp = receiver_clock_.NowTicks();
  receive_event->type = PACKET_RECEIVED;
  receive_event->media_type = VIDEO_EVENT;
  receive_event->rtp_timestamp = rtp_timestamp;
  receive_event->frame_id = frame_id;
  receive_event->packet_id = 56;
  receive_event->max_packet_id = 78;
  receive_event->size = 1500;
  cast_environment_->logger()->DispatchPacketEvent(receive_event.Pass());

  EXPECT_FALSE(estimator_.GetReceiverOffsetBounds(&lower_bound, &upper_bound));

  AdvanceClocks(base::TimeDelta::FromMilliseconds(30));
  scoped_ptr<FrameEvent> ack_event(new FrameEvent());
  ack_event->timestamp = sender_clock_->NowTicks();
  ack_event->type = FRAME_ACK_RECEIVED;
  ack_event->media_type = VIDEO_EVENT;
  ack_event->rtp_timestamp = rtp_timestamp;
  ack_event->frame_id = frame_id;
  cast_environment_->logger()->DispatchFrameEvent(ack_event.Pass());

  EXPECT_TRUE(estimator_.GetReceiverOffsetBounds(&lower_bound, &upper_bound));

  int64 lower_bound_ms = lower_bound.InMilliseconds();
  int64 upper_bound_ms = upper_bound.InMilliseconds();
  EXPECT_EQ(70, lower_bound_ms);
  EXPECT_EQ(110, upper_bound_ms);
  EXPECT_GE(true_offset_ms, lower_bound_ms);
  EXPECT_LE(true_offset_ms, upper_bound_ms);
}

// Same scenario as above, but event C arrives before event B. It doesn't mean
// event C occurred before event B.
TEST_F(ReceiverTimeOffsetEstimatorImplTest, EventCArrivesBeforeEventB) {
  int64 true_offset_ms = 100;
  receiver_clock_.Advance(base::TimeDelta::FromMilliseconds(true_offset_ms));

  base::TimeDelta lower_bound;
  base::TimeDelta upper_bound;

  EXPECT_FALSE(estimator_.GetReceiverOffsetBounds(&lower_bound, &upper_bound));

  RtpTimestamp rtp_timestamp = 0;
  uint32 frame_id = 0;

  AdvanceClocks(base::TimeDelta::FromMilliseconds(20));

  scoped_ptr<FrameEvent> encode_event(new FrameEvent());
  encode_event->timestamp = sender_clock_->NowTicks();
  encode_event->type = FRAME_ENCODED;
  encode_event->media_type = VIDEO_EVENT;
  encode_event->rtp_timestamp = rtp_timestamp;
  encode_event->frame_id = frame_id;
  encode_event->size = 1234;
  encode_event->key_frame = true;
  encode_event->target_bitrate = 5678;
  encode_event->encoder_cpu_utilization = 9.10;
  encode_event->idealized_bitrate_utilization = 11.12;
  cast_environment_->logger()->DispatchFrameEvent(encode_event.Pass());

  scoped_ptr<PacketEvent> send_event(new PacketEvent());
  send_event->timestamp = sender_clock_->NowTicks();
  send_event->type = PACKET_SENT_TO_NETWORK;
  send_event->media_type = VIDEO_EVENT;
  send_event->rtp_timestamp = rtp_timestamp;
  send_event->frame_id = frame_id;
  send_event->packet_id = 56;
  send_event->max_packet_id = 78;
  send_event->size = 1500;
  cast_environment_->logger()->DispatchPacketEvent(send_event.Pass());

  EXPECT_FALSE(estimator_.GetReceiverOffsetBounds(&lower_bound, &upper_bound));

  AdvanceClocks(base::TimeDelta::FromMilliseconds(10));
  base::TimeTicks event_b_time = receiver_clock_.NowTicks();
  AdvanceClocks(base::TimeDelta::FromMilliseconds(30));
  base::TimeTicks event_c_time = sender_clock_->NowTicks();

  scoped_ptr<FrameEvent> ack_event(new FrameEvent());
  ack_event->timestamp = event_c_time;
  ack_event->type = FRAME_ACK_RECEIVED;
  ack_event->media_type = VIDEO_EVENT;
  ack_event->rtp_timestamp = rtp_timestamp;
  ack_event->frame_id = frame_id;
  cast_environment_->logger()->DispatchFrameEvent(ack_event.Pass());

  EXPECT_FALSE(estimator_.GetReceiverOffsetBounds(&lower_bound, &upper_bound));

  scoped_ptr<PacketEvent> receive_event(new PacketEvent());
  receive_event->timestamp = event_b_time;
  receive_event->type = PACKET_RECEIVED;
  receive_event->media_type = VIDEO_EVENT;
  receive_event->rtp_timestamp = rtp_timestamp;
  receive_event->frame_id = frame_id;
  receive_event->packet_id = 56;
  receive_event->max_packet_id = 78;
  receive_event->size = 1500;
  cast_environment_->logger()->DispatchPacketEvent(receive_event.Pass());

  scoped_ptr<FrameEvent> ack_sent_event(new FrameEvent());
  ack_sent_event->timestamp = event_b_time;
  ack_sent_event->type = FRAME_ACK_SENT;
  ack_sent_event->media_type = VIDEO_EVENT;
  ack_sent_event->rtp_timestamp = rtp_timestamp;
  ack_sent_event->frame_id = frame_id;
  cast_environment_->logger()->DispatchFrameEvent(ack_sent_event.Pass());

  EXPECT_TRUE(estimator_.GetReceiverOffsetBounds(&lower_bound, &upper_bound));

  int64 lower_bound_ms = lower_bound.InMilliseconds();
  int64 upper_bound_ms = upper_bound.InMilliseconds();
  EXPECT_EQ(70, lower_bound_ms);
  EXPECT_EQ(110, upper_bound_ms);
  EXPECT_GE(true_offset_ms, lower_bound_ms);
  EXPECT_LE(true_offset_ms, upper_bound_ms);
}

TEST_F(ReceiverTimeOffsetEstimatorImplTest, MultipleIterations) {
  int64 true_offset_ms = 100;
  receiver_clock_.Advance(base::TimeDelta::FromMilliseconds(true_offset_ms));

  base::TimeDelta lower_bound;
  base::TimeDelta upper_bound;

  RtpTimestamp rtp_timestamp_a = 0;
  int frame_id_a = 0;
  RtpTimestamp rtp_timestamp_b = 90;
  int frame_id_b = 1;
  RtpTimestamp rtp_timestamp_c = 180;
  int frame_id_c = 2;

  // Frame 1 times: [20, 30+100, 60]
  // Frame 2 times: [30, 50+100, 55]
  // Frame 3 times: [77, 80+100, 110]
  // Bound should end up at [95, 103]
  // Events times in chronological order: 20, 30 x2, 50, 55, 60, 77, 80, 110
  AdvanceClocks(base::TimeDelta::FromMilliseconds(20));
  scoped_ptr<FrameEvent> encode_event(new FrameEvent());
  encode_event->timestamp = sender_clock_->NowTicks();
  encode_event->type = FRAME_ENCODED;
  encode_event->media_type = VIDEO_EVENT;
  encode_event->rtp_timestamp = rtp_timestamp_a;
  encode_event->frame_id = frame_id_a;
  encode_event->size = 1234;
  encode_event->key_frame = true;
  encode_event->target_bitrate = 5678;
  encode_event->encoder_cpu_utilization = 9.10;
  encode_event->idealized_bitrate_utilization = 11.12;
  cast_environment_->logger()->DispatchFrameEvent(encode_event.Pass());

  scoped_ptr<PacketEvent> send_event(new PacketEvent());
  send_event->timestamp = sender_clock_->NowTicks();
  send_event->type = PACKET_SENT_TO_NETWORK;
  send_event->media_type = VIDEO_EVENT;
  send_event->rtp_timestamp = rtp_timestamp_a;
  send_event->frame_id = frame_id_a;
  send_event->packet_id = 56;
  send_event->max_packet_id = 78;
  send_event->size = 1500;
  cast_environment_->logger()->DispatchPacketEvent(send_event.Pass());

  AdvanceClocks(base::TimeDelta::FromMilliseconds(10));
  encode_event.reset(new FrameEvent());
  encode_event->timestamp = sender_clock_->NowTicks();
  encode_event->type = FRAME_ENCODED;
  encode_event->media_type = VIDEO_EVENT;
  encode_event->rtp_timestamp = rtp_timestamp_b;
  encode_event->frame_id = frame_id_b;
  encode_event->size = 1234;
  encode_event->key_frame = true;
  encode_event->target_bitrate = 5678;
  encode_event->encoder_cpu_utilization = 9.10;
  encode_event->idealized_bitrate_utilization = 11.12;
  cast_environment_->logger()->DispatchFrameEvent(encode_event.Pass());

  send_event.reset(new PacketEvent());
  send_event->timestamp = sender_clock_->NowTicks();
  send_event->type = PACKET_SENT_TO_NETWORK;
  send_event->media_type = VIDEO_EVENT;
  send_event->rtp_timestamp = rtp_timestamp_b;
  send_event->frame_id = frame_id_b;
  send_event->packet_id = 56;
  send_event->max_packet_id = 78;
  send_event->size = 1500;
  cast_environment_->logger()->DispatchPacketEvent(send_event.Pass());

  scoped_ptr<FrameEvent> ack_sent_event(new FrameEvent());
  ack_sent_event->timestamp = receiver_clock_.NowTicks();
  ack_sent_event->type = FRAME_ACK_SENT;
  ack_sent_event->media_type = VIDEO_EVENT;
  ack_sent_event->rtp_timestamp = rtp_timestamp_a;
  ack_sent_event->frame_id = frame_id_a;
  cast_environment_->logger()->DispatchFrameEvent(ack_sent_event.Pass());

  AdvanceClocks(base::TimeDelta::FromMilliseconds(20));

  scoped_ptr<PacketEvent> receive_event(new PacketEvent());
  receive_event->timestamp = receiver_clock_.NowTicks();
  receive_event->type = PACKET_RECEIVED;
  receive_event->media_type = VIDEO_EVENT;
  receive_event->rtp_timestamp = rtp_timestamp_b;
  receive_event->frame_id = frame_id_b;
  receive_event->packet_id = 56;
  receive_event->max_packet_id = 78;
  receive_event->size = 1500;
  cast_environment_->logger()->DispatchPacketEvent(receive_event.Pass());

  ack_sent_event.reset(new FrameEvent());
  ack_sent_event->timestamp = receiver_clock_.NowTicks();
  ack_sent_event->type = FRAME_ACK_SENT;
  ack_sent_event->media_type = VIDEO_EVENT;
  ack_sent_event->rtp_timestamp = rtp_timestamp_b;
  ack_sent_event->frame_id = frame_id_b;
  cast_environment_->logger()->DispatchFrameEvent(ack_sent_event.Pass());

  AdvanceClocks(base::TimeDelta::FromMilliseconds(5));
  scoped_ptr<FrameEvent> ack_event(new FrameEvent());
  ack_event->timestamp = sender_clock_->NowTicks();
  ack_event->type = FRAME_ACK_RECEIVED;
  ack_event->media_type = VIDEO_EVENT;
  ack_event->rtp_timestamp = rtp_timestamp_b;
  ack_event->frame_id = frame_id_b;
  cast_environment_->logger()->DispatchFrameEvent(ack_event.Pass());

  AdvanceClocks(base::TimeDelta::FromMilliseconds(5));
  ack_event.reset(new FrameEvent());
  ack_event->timestamp = sender_clock_->NowTicks();
  ack_event->type = FRAME_ACK_RECEIVED;
  ack_event->media_type = VIDEO_EVENT;
  ack_event->rtp_timestamp = rtp_timestamp_a;
  ack_event->frame_id = frame_id_a;
  cast_environment_->logger()->DispatchFrameEvent(ack_event.Pass());

  AdvanceClocks(base::TimeDelta::FromMilliseconds(17));
  encode_event.reset(new FrameEvent());
  encode_event->timestamp = sender_clock_->NowTicks();
  encode_event->type = FRAME_ENCODED;
  encode_event->media_type = VIDEO_EVENT;
  encode_event->rtp_timestamp = rtp_timestamp_c;
  encode_event->frame_id = frame_id_c;
  encode_event->size = 1234;
  encode_event->key_frame = true;
  encode_event->target_bitrate = 5678;
  encode_event->encoder_cpu_utilization = 9.10;
  encode_event->idealized_bitrate_utilization = 11.12;
  cast_environment_->logger()->DispatchFrameEvent(encode_event.Pass());

  send_event.reset(new PacketEvent());
  send_event->timestamp = sender_clock_->NowTicks();
  send_event->type = PACKET_SENT_TO_NETWORK;
  send_event->media_type = VIDEO_EVENT;
  send_event->rtp_timestamp = rtp_timestamp_c;
  send_event->frame_id = frame_id_c;
  send_event->packet_id = 56;
  send_event->max_packet_id = 78;
  send_event->size = 1500;
  cast_environment_->logger()->DispatchPacketEvent(send_event.Pass());

  AdvanceClocks(base::TimeDelta::FromMilliseconds(3));
  receive_event.reset(new PacketEvent());
  receive_event->timestamp = receiver_clock_.NowTicks();
  receive_event->type = PACKET_RECEIVED;
  receive_event->media_type = VIDEO_EVENT;
  receive_event->rtp_timestamp = rtp_timestamp_c;
  receive_event->frame_id = frame_id_c;
  receive_event->packet_id = 56;
  receive_event->max_packet_id = 78;
  receive_event->size = 1500;
  cast_environment_->logger()->DispatchPacketEvent(receive_event.Pass());

  ack_sent_event.reset(new FrameEvent());
  ack_sent_event->timestamp = receiver_clock_.NowTicks();
  ack_sent_event->type = FRAME_ACK_SENT;
  ack_sent_event->media_type = VIDEO_EVENT;
  ack_sent_event->rtp_timestamp = rtp_timestamp_c;
  ack_sent_event->frame_id = frame_id_c;
  cast_environment_->logger()->DispatchFrameEvent(ack_sent_event.Pass());

  AdvanceClocks(base::TimeDelta::FromMilliseconds(30));
  ack_event.reset(new FrameEvent());
  ack_event->timestamp = sender_clock_->NowTicks();
  ack_event->type = FRAME_ACK_RECEIVED;
  ack_event->media_type = VIDEO_EVENT;
  ack_event->rtp_timestamp = rtp_timestamp_c;
  ack_event->frame_id = frame_id_c;
  cast_environment_->logger()->DispatchFrameEvent(ack_event.Pass());

  EXPECT_TRUE(estimator_.GetReceiverOffsetBounds(&lower_bound, &upper_bound));
  int64 lower_bound_ms = lower_bound.InMilliseconds();
  int64 upper_bound_ms = upper_bound.InMilliseconds();
  EXPECT_GT(lower_bound_ms, 90);
  EXPECT_LE(lower_bound_ms, true_offset_ms);
  EXPECT_LT(upper_bound_ms, 150);
  EXPECT_GT(upper_bound_ms, true_offset_ms);
}

}  // namespace cast
}  // namespace media
