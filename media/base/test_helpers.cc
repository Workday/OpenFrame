// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/base/test_helpers.h"

#include "base/bind.h"
#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "base/pickle.h"
#include "base/test/test_timeouts.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "media/base/audio_buffer.h"
#include "media/base/bind_to_loop.h"
#include "media/base/decoder_buffer.h"
#include "ui/gfx/rect.h"

using ::testing::_;
using ::testing::StrictMock;

namespace media {

// Utility mock for testing methods expecting Closures and PipelineStatusCBs.
class MockCallback : public base::RefCountedThreadSafe<MockCallback> {
 public:
  MockCallback();
  MOCK_METHOD0(Run, void());
  MOCK_METHOD1(RunWithStatus, void(PipelineStatus));

 protected:
  friend class base::RefCountedThreadSafe<MockCallback>;
  virtual ~MockCallback();

 private:
  DISALLOW_COPY_AND_ASSIGN(MockCallback);
};

MockCallback::MockCallback() {}
MockCallback::~MockCallback() {}

base::Closure NewExpectedClosure() {
  StrictMock<MockCallback>* callback = new StrictMock<MockCallback>();
  EXPECT_CALL(*callback, Run());
  return base::Bind(&MockCallback::Run, callback);
}

PipelineStatusCB NewExpectedStatusCB(PipelineStatus status) {
  StrictMock<MockCallback>* callback = new StrictMock<MockCallback>();
  EXPECT_CALL(*callback, RunWithStatus(status));
  return base::Bind(&MockCallback::RunWithStatus, callback);
}

WaitableMessageLoopEvent::WaitableMessageLoopEvent()
    : message_loop_(base::MessageLoop::current()),
      signaled_(false),
      status_(PIPELINE_OK) {
  DCHECK(message_loop_);
}

WaitableMessageLoopEvent::~WaitableMessageLoopEvent() {}

base::Closure WaitableMessageLoopEvent::GetClosure() {
  DCHECK_EQ(message_loop_, base::MessageLoop::current());
  return BindToLoop(message_loop_->message_loop_proxy(), base::Bind(
      &WaitableMessageLoopEvent::OnCallback, base::Unretained(this),
      PIPELINE_OK));
}

PipelineStatusCB WaitableMessageLoopEvent::GetPipelineStatusCB() {
  DCHECK_EQ(message_loop_, base::MessageLoop::current());
  return BindToLoop(message_loop_->message_loop_proxy(), base::Bind(
      &WaitableMessageLoopEvent::OnCallback, base::Unretained(this)));
}

void WaitableMessageLoopEvent::RunAndWait() {
  RunAndWaitForStatus(PIPELINE_OK);
}

void WaitableMessageLoopEvent::RunAndWaitForStatus(PipelineStatus expected) {
  DCHECK_EQ(message_loop_, base::MessageLoop::current());
  if (signaled_) {
    EXPECT_EQ(expected, status_);
    return;
  }

  base::Timer timer(false, false);
  timer.Start(FROM_HERE, TestTimeouts::action_timeout(), base::Bind(
      &WaitableMessageLoopEvent::OnTimeout, base::Unretained(this)));

  message_loop_->Run();
  EXPECT_TRUE(signaled_);
  EXPECT_EQ(expected, status_);
}

void WaitableMessageLoopEvent::OnCallback(PipelineStatus status) {
  DCHECK_EQ(message_loop_, base::MessageLoop::current());
  signaled_ = true;
  status_ = status;
  message_loop_->QuitWhenIdle();
}

void WaitableMessageLoopEvent::OnTimeout() {
  DCHECK_EQ(message_loop_, base::MessageLoop::current());
  ADD_FAILURE() << "Timed out waiting for message loop to quit";
  message_loop_->QuitWhenIdle();
}

static VideoDecoderConfig GetTestConfig(VideoCodec codec,
                                        gfx::Size coded_size,
                                        bool is_encrypted) {
  gfx::Rect visible_rect(coded_size.width(), coded_size.height());
  gfx::Size natural_size = coded_size;

  return VideoDecoderConfig(codec, VIDEO_CODEC_PROFILE_UNKNOWN,
      VideoFrame::YV12, coded_size, visible_rect, natural_size,
      NULL, 0, is_encrypted);
}

static const gfx::Size kNormalSize(320, 240);
static const gfx::Size kLargeSize(640, 480);

VideoDecoderConfig TestVideoConfig::Invalid() {
  return GetTestConfig(kUnknownVideoCodec, kNormalSize, false);
}

VideoDecoderConfig TestVideoConfig::Normal() {
  return GetTestConfig(kCodecVP8, kNormalSize, false);
}

VideoDecoderConfig TestVideoConfig::NormalEncrypted() {
  return GetTestConfig(kCodecVP8, kNormalSize, true);
}

VideoDecoderConfig TestVideoConfig::Large() {
  return GetTestConfig(kCodecVP8, kLargeSize, false);
}

VideoDecoderConfig TestVideoConfig::LargeEncrypted() {
  return GetTestConfig(kCodecVP8, kLargeSize, true);
}

gfx::Size TestVideoConfig::NormalCodedSize() {
  return kNormalSize;
}

gfx::Size TestVideoConfig::LargeCodedSize() {
  return kLargeSize;
}

template <class T>
scoped_refptr<AudioBuffer> MakeInterleavedAudioBuffer(
    SampleFormat format,
    int channels,
    T start,
    T increment,
    int frames,
    base::TimeDelta start_time,
    base::TimeDelta duration) {
  DCHECK(format == kSampleFormatU8 || format == kSampleFormatS16 ||
         format == kSampleFormatS32 || format == kSampleFormatF32);

  // Create a block of memory with values:
  //   start
  //   start + increment
  //   start + 2 * increment, ...
  // Since this is interleaved data, channel 0 data will be:
  //   start
  //   start + channels * increment
  //   start + 2 * channels * increment, ...
  int buffer_size = frames * channels * sizeof(T);
  scoped_ptr<uint8[]> memory(new uint8[buffer_size]);
  uint8* data[] = { memory.get() };
  T* buffer = reinterpret_cast<T*>(memory.get());
  for (int i = 0; i < frames * channels; ++i) {
    buffer[i] = start;
    start += increment;
  }
  return AudioBuffer::CopyFrom(
      format, channels, frames, data, start_time, duration);
}

template <class T>
scoped_refptr<AudioBuffer> MakePlanarAudioBuffer(
    SampleFormat format,
    int channels,
    T start,
    T increment,
    int frames,
    base::TimeDelta start_time,
    base::TimeDelta duration) {
  DCHECK(format == kSampleFormatPlanarF32 || format == kSampleFormatPlanarS16);

  // Create multiple blocks of data, one for each channel.
  // Values in channel 0 will be:
  //   start
  //   start + increment
  //   start + 2 * increment, ...
  // Values in channel 1 will be:
  //   start + frames * increment
  //   start + (frames + 1) * increment
  //   start + (frames + 2) * increment, ...
  int buffer_size = frames * sizeof(T);
  scoped_ptr<uint8*[]> data(new uint8*[channels]);
  scoped_ptr<uint8[]> memory(new uint8[channels * buffer_size]);
  for (int i = 0; i < channels; ++i) {
    data.get()[i] = memory.get() + i * buffer_size;
    T* buffer = reinterpret_cast<T*>(data.get()[i]);
    for (int j = 0; j < frames; ++j) {
      buffer[j] = start;
      start += increment;
    }
  }
  return AudioBuffer::CopyFrom(
      format, channels, frames, data.get(), start_time, duration);
}

// Instantiate all the types of MakeInterleavedAudioBuffer() and
// MakePlanarAudioBuffer() needed.

#define DEFINE_INTERLEAVED_INSTANCE(type)                               \
  template scoped_refptr<AudioBuffer> MakeInterleavedAudioBuffer<type>( \
      SampleFormat format,                                              \
      int channels,                                                     \
      type start,                                                       \
      type increment,                                                   \
      int frames,                                                       \
      base::TimeDelta start_time,                                       \
      base::TimeDelta duration)
DEFINE_INTERLEAVED_INSTANCE(uint8);
DEFINE_INTERLEAVED_INSTANCE(int16);
DEFINE_INTERLEAVED_INSTANCE(int32);
DEFINE_INTERLEAVED_INSTANCE(float);

#define DEFINE_PLANAR_INSTANCE(type)                               \
  template scoped_refptr<AudioBuffer> MakePlanarAudioBuffer<type>( \
      SampleFormat format,                                         \
      int channels,                                                \
      type start,                                                  \
      type increment,                                              \
      int frames,                                                  \
      base::TimeDelta start_time,                                  \
      base::TimeDelta duration);
DEFINE_PLANAR_INSTANCE(int16);
DEFINE_PLANAR_INSTANCE(float);

static const char kFakeVideoBufferHeader[] = "FakeVideoBufferForTest";

scoped_refptr<DecoderBuffer> CreateFakeVideoBufferForTest(
    const VideoDecoderConfig& config,
    base::TimeDelta timestamp, base::TimeDelta duration) {
  Pickle pickle;
  pickle.WriteString(kFakeVideoBufferHeader);
  pickle.WriteInt(config.coded_size().width());
  pickle.WriteInt(config.coded_size().height());
  pickle.WriteInt64(timestamp.InMilliseconds());

  scoped_refptr<DecoderBuffer> buffer = DecoderBuffer::CopyFrom(
      static_cast<const uint8*>(pickle.data()),
      static_cast<int>(pickle.size()));
  buffer->set_timestamp(timestamp);
  buffer->set_duration(duration);

  return buffer;
}

bool VerifyFakeVideoBufferForTest(
    const scoped_refptr<DecoderBuffer>& buffer,
    const VideoDecoderConfig& config) {
  // Check if the input |buffer| matches the |config|.
  PickleIterator pickle(Pickle(reinterpret_cast<const char*>(buffer->data()),
                               buffer->data_size()));
  std::string header;
  int width = 0;
  int height = 0;
  bool success = pickle.ReadString(&header) && pickle.ReadInt(&width) &&
                 pickle.ReadInt(&height);
  return (success && header == kFakeVideoBufferHeader &&
          width == config.coded_size().width() &&
          height == config.coded_size().height());
}

}  // namespace media
