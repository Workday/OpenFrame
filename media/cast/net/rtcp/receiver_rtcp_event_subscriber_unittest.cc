// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/test/simple_test_tick_clock.h"
#include "base/time/tick_clock.h"
#include "media/cast/cast_environment.h"
#include "media/cast/logging/logging_defines.h"
#include "media/cast/net/rtcp/receiver_rtcp_event_subscriber.h"
#include "media/cast/test/fake_single_thread_task_runner.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace media {
namespace cast {

namespace {

const size_t kMaxEventEntries = 10u;
const int64 kDelayMs = 20L;

}  // namespace

class ReceiverRtcpEventSubscriberTest : public ::testing::Test {
 protected:
  ReceiverRtcpEventSubscriberTest()
      : testing_clock_(new base::SimpleTestTickClock()),
        task_runner_(new test::FakeSingleThreadTaskRunner(testing_clock_)),
        cast_environment_(new CastEnvironment(
            scoped_ptr<base::TickClock>(testing_clock_).Pass(),
            task_runner_,
            task_runner_,
            task_runner_)) {}

  ~ReceiverRtcpEventSubscriberTest() override {}

  void TearDown() final {
    if (event_subscriber_) {
      cast_environment_->logger()->Unsubscribe(event_subscriber_.get());
      event_subscriber_.reset();
    }
  }

  void Init(EventMediaType type) {
    event_subscriber_.reset(
        new ReceiverRtcpEventSubscriber(kMaxEventEntries, type));
    cast_environment_->logger()->Subscribe(event_subscriber_.get());
  }

  void InsertEvents() {
    // Video events
    scoped_ptr<FrameEvent> playout_event(new FrameEvent());
    playout_event->timestamp = testing_clock_->NowTicks();
    playout_event->type = FRAME_PLAYOUT;
    playout_event->media_type = VIDEO_EVENT;
    playout_event->rtp_timestamp = 100u;
    playout_event->frame_id = 2u;
    playout_event->delay_delta = base::TimeDelta::FromMilliseconds(kDelayMs);
    cast_environment_->logger()->DispatchFrameEvent(playout_event.Pass());

    scoped_ptr<FrameEvent> decode_event(new FrameEvent());
    decode_event->timestamp = testing_clock_->NowTicks();
    decode_event->type = FRAME_DECODED;
    decode_event->media_type = VIDEO_EVENT;
    decode_event->rtp_timestamp = 200u;
    decode_event->frame_id = 1u;
    cast_environment_->logger()->DispatchFrameEvent(decode_event.Pass());

    scoped_ptr<PacketEvent> receive_event(new PacketEvent());
    receive_event->timestamp = testing_clock_->NowTicks();
    receive_event->type = PACKET_RECEIVED;
    receive_event->media_type = VIDEO_EVENT;
    receive_event->rtp_timestamp = 200u;
    receive_event->frame_id = 2u;
    receive_event->packet_id = 1u;
    receive_event->max_packet_id = 10u;
    receive_event->size = 1024u;
    cast_environment_->logger()->DispatchPacketEvent(receive_event.Pass());

    // Audio events
    playout_event.reset(new FrameEvent());
    playout_event->timestamp = testing_clock_->NowTicks();
    playout_event->type = FRAME_PLAYOUT;
    playout_event->media_type = AUDIO_EVENT;
    playout_event->rtp_timestamp = 300u;
    playout_event->frame_id = 4u;
    playout_event->delay_delta = base::TimeDelta::FromMilliseconds(kDelayMs);
    cast_environment_->logger()->DispatchFrameEvent(playout_event.Pass());

    decode_event.reset(new FrameEvent());
    decode_event->timestamp = testing_clock_->NowTicks();
    decode_event->type = FRAME_DECODED;
    decode_event->media_type = AUDIO_EVENT;
    decode_event->rtp_timestamp = 400u;
    decode_event->frame_id = 3u;
    cast_environment_->logger()->DispatchFrameEvent(decode_event.Pass());

    receive_event.reset(new PacketEvent());
    receive_event->timestamp = testing_clock_->NowTicks();
    receive_event->type = PACKET_RECEIVED;
    receive_event->media_type = AUDIO_EVENT;
    receive_event->rtp_timestamp = 400u;
    receive_event->frame_id = 5u;
    receive_event->packet_id = 1u;
    receive_event->max_packet_id = 10u;
    receive_event->size = 128u;
    cast_environment_->logger()->DispatchPacketEvent(receive_event.Pass());

    // Unrelated events
    scoped_ptr<FrameEvent> encode_event(new FrameEvent());
    encode_event->timestamp = testing_clock_->NowTicks();
    encode_event->type = FRAME_ENCODED;
    encode_event->media_type = VIDEO_EVENT;
    encode_event->rtp_timestamp = 100u;
    encode_event->frame_id = 1u;
    cast_environment_->logger()->DispatchFrameEvent(encode_event.Pass());

    encode_event.reset(new FrameEvent());
    encode_event->timestamp = testing_clock_->NowTicks();
    encode_event->type = FRAME_ENCODED;
    encode_event->media_type = AUDIO_EVENT;
    encode_event->rtp_timestamp = 100u;
    encode_event->frame_id = 1u;
    cast_environment_->logger()->DispatchFrameEvent(encode_event.Pass());
  }

  base::SimpleTestTickClock* testing_clock_;  // Owned by CastEnvironment.
  scoped_refptr<test::FakeSingleThreadTaskRunner> task_runner_;
  scoped_refptr<CastEnvironment> cast_environment_;
  scoped_ptr<ReceiverRtcpEventSubscriber> event_subscriber_;
};

TEST_F(ReceiverRtcpEventSubscriberTest, LogVideoEvents) {
  Init(VIDEO_EVENT);

  InsertEvents();
  ReceiverRtcpEventSubscriber::RtcpEvents rtcp_events;
  event_subscriber_->GetRtcpEventsWithRedundancy(&rtcp_events);
  EXPECT_EQ(3u, rtcp_events.size());
}

TEST_F(ReceiverRtcpEventSubscriberTest, LogAudioEvents) {
  Init(AUDIO_EVENT);

  InsertEvents();
  ReceiverRtcpEventSubscriber::RtcpEvents rtcp_events;
  event_subscriber_->GetRtcpEventsWithRedundancy(&rtcp_events);
  EXPECT_EQ(3u, rtcp_events.size());
}

TEST_F(ReceiverRtcpEventSubscriberTest, DropEventsWhenSizeExceeded) {
  Init(VIDEO_EVENT);

  for (uint32 i = 1u; i <= 10u; ++i) {
    scoped_ptr<FrameEvent> decode_event(new FrameEvent());
    decode_event->timestamp = testing_clock_->NowTicks();
    decode_event->type = FRAME_DECODED;
    decode_event->media_type = VIDEO_EVENT;
    decode_event->rtp_timestamp = i * 10;
    decode_event->frame_id = i;
    cast_environment_->logger()->DispatchFrameEvent(decode_event.Pass());
  }

  ReceiverRtcpEventSubscriber::RtcpEvents rtcp_events;
  event_subscriber_->GetRtcpEventsWithRedundancy(&rtcp_events);
  EXPECT_EQ(10u, rtcp_events.size());
}

}  // namespace cast
}  // namespace media
