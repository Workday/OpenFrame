// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <deque>

#include "base/bind.h"
#include "base/message_loop/message_loop.h"
#include "base/strings/stringprintf.h"
#include "media/base/audio_buffer.h"
#include "media/base/decoder_buffer.h"
#include "media/base/mock_filters.h"
#include "media/base/test_data_util.h"
#include "media/base/test_helpers.h"
#include "media/ffmpeg/ffmpeg_common.h"
#include "media/filters/ffmpeg_audio_decoder.h"
#include "media/filters/ffmpeg_glue.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::_;
using ::testing::StrictMock;

namespace media {

ACTION_P(InvokeReadPacket, test) {
  test->ReadPacket(arg0);
}

class FFmpegAudioDecoderTest : public testing::Test {
 public:
  FFmpegAudioDecoderTest()
      : decoder_(new FFmpegAudioDecoder(message_loop_.message_loop_proxy())),
        demuxer_(new StrictMock<MockDemuxerStream>(DemuxerStream::AUDIO)) {
    FFmpegGlue::InitializeFFmpeg();

    vorbis_extradata_ = ReadTestDataFile("vorbis-extradata");

    // Refer to media/test/data/README for details on vorbis test data.
    for (int i = 0; i < 4; ++i) {
      scoped_refptr<DecoderBuffer> buffer =
          ReadTestDataFile(base::StringPrintf("vorbis-packet-%d", i));

      if (i < 3) {
        buffer->set_timestamp(base::TimeDelta());
      } else {
        buffer->set_timestamp(base::TimeDelta::FromMicroseconds(2902));
      }

      buffer->set_duration(base::TimeDelta());
      encoded_audio_.push_back(buffer);
    }

    // Push in an EOS buffer.
    encoded_audio_.push_back(DecoderBuffer::CreateEOSBuffer());
  }

  virtual ~FFmpegAudioDecoderTest() {}

  void Initialize() {
    AudioDecoderConfig config(kCodecVorbis,
                              kSampleFormatPlanarF32,
                              CHANNEL_LAYOUT_STEREO,
                              44100,
                              vorbis_extradata_->data(),
                              vorbis_extradata_->data_size(),
                              false);  // Not encrypted.
    demuxer_->set_audio_decoder_config(config);
    decoder_->Initialize(demuxer_.get(),
                         NewExpectedStatusCB(PIPELINE_OK),
                         base::Bind(&MockStatisticsCB::OnStatistics,
                                    base::Unretained(&statistics_cb_)));

    message_loop_.RunUntilIdle();
  }

  void ReadPacket(const DemuxerStream::ReadCB& read_cb) {
    CHECK(!encoded_audio_.empty()) << "ReadPacket() called too many times";

    scoped_refptr<DecoderBuffer> buffer(encoded_audio_.front());
    DemuxerStream::Status status =
        buffer.get() ? DemuxerStream::kOk : DemuxerStream::kAborted;
    encoded_audio_.pop_front();
    read_cb.Run(status, buffer);
  }

  void Read() {
    decoder_->Read(base::Bind(
        &FFmpegAudioDecoderTest::DecodeFinished, base::Unretained(this)));
    message_loop_.RunUntilIdle();
  }

  void DecodeFinished(AudioDecoder::Status status,
                      const scoped_refptr<AudioBuffer>& buffer) {
    decoded_audio_.push_back(buffer);
  }

  void ExpectDecodedAudio(size_t i, int64 timestamp, int64 duration) {
    EXPECT_LT(i, decoded_audio_.size());
    EXPECT_EQ(timestamp, decoded_audio_[i]->timestamp().InMicroseconds());
    EXPECT_EQ(duration, decoded_audio_[i]->duration().InMicroseconds());
    EXPECT_FALSE(decoded_audio_[i]->end_of_stream());
  }

  void ExpectEndOfStream(size_t i) {
    EXPECT_LT(i, decoded_audio_.size());
    EXPECT_TRUE(decoded_audio_[i]->end_of_stream());
  }

  base::MessageLoop message_loop_;
  scoped_ptr<FFmpegAudioDecoder> decoder_;
  scoped_ptr<StrictMock<MockDemuxerStream> > demuxer_;
  MockStatisticsCB statistics_cb_;

  scoped_refptr<DecoderBuffer> vorbis_extradata_;

  std::deque<scoped_refptr<DecoderBuffer> > encoded_audio_;
  std::deque<scoped_refptr<AudioBuffer> > decoded_audio_;
};

TEST_F(FFmpegAudioDecoderTest, Initialize) {
  Initialize();

  const AudioDecoderConfig& config = demuxer_->audio_decoder_config();
  EXPECT_EQ(config.bits_per_channel(), decoder_->bits_per_channel());
  EXPECT_EQ(config.channel_layout(), decoder_->channel_layout());
  EXPECT_EQ(config.samples_per_second(), decoder_->samples_per_second());
}

TEST_F(FFmpegAudioDecoderTest, ProduceAudioSamples) {
  Initialize();

  // Vorbis requires N+1 packets to produce audio data for N packets.
  //
  // This will should result in the demuxer receiving three reads for two
  // requests to produce audio samples.
  EXPECT_CALL(*demuxer_, Read(_))
      .Times(5)
      .WillRepeatedly(InvokeReadPacket(this));
  EXPECT_CALL(statistics_cb_, OnStatistics(_))
      .Times(4);

  Read();
  Read();
  Read();

  ASSERT_EQ(3u, decoded_audio_.size());
  ExpectDecodedAudio(0, 0, 2902);
  ExpectDecodedAudio(1, 2902, 13061);
  ExpectDecodedAudio(2, 15963, 23220);

  // Call one more time to trigger EOS.
  Read();
  ASSERT_EQ(4u, decoded_audio_.size());
  ExpectEndOfStream(3);
}

TEST_F(FFmpegAudioDecoderTest, ReadAbort) {
  Initialize();

  encoded_audio_.clear();
  encoded_audio_.push_back(NULL);

  EXPECT_CALL(*demuxer_, Read(_))
      .WillOnce(InvokeReadPacket(this));
  Read();

  EXPECT_EQ(decoded_audio_.size(), 1u);
  EXPECT_TRUE(decoded_audio_[0].get() ==  NULL);
}

}  // namespace media
