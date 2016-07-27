// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/synchronization/waitable_event.h"
#include "chromecast/base/metrics/cast_metrics_test_helper.h"
#include "chromecast/media/audio/cast_audio_manager.h"
#include "chromecast/media/audio/cast_audio_output_stream.h"
#include "chromecast/media/base/media_message_loop.h"
#include "chromecast/media/cma/backend/media_pipeline_backend_default.h"
#include "chromecast/public/media/cast_decoder_buffer.h"
#include "chromecast/public/media/decoder_config.h"
#include "chromecast/public/media/decrypt_context.h"
#include "chromecast/public/media/media_pipeline_backend.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace chromecast {
namespace media {
namespace {
const char kDefaultDeviceId[] = "";

void RunUntilIdle(base::TaskRunner* task_runner) {
  base::WaitableEvent completion_event(false, false);
  task_runner->PostTask(FROM_HERE,
                        base::Bind(&base::WaitableEvent::Signal,
                                   base::Unretained(&completion_event)));
  completion_event.Wait();
}

class FakeAudioDecoder : public MediaPipelineBackend::AudioDecoder {
 public:
  enum PipelineStatus {
    PIPELINE_STATUS_OK,
    PIPELINE_STATUS_BUSY,
    PIPELINE_STATUS_ERROR,
    PIPELINE_STATUS_ASYNC_ERROR,
  };

  FakeAudioDecoder()
      : volume_(1.0f),
        pipeline_status_(PIPELINE_STATUS_OK),
        pending_push_(false),
        pushed_buffer_count_(0),
        last_buffer_(nullptr),
        delegate_(nullptr) {}
  ~FakeAudioDecoder() override {}

  // MediaPipelineBackend::AudioDecoder overrides.
  BufferStatus PushBuffer(CastDecoderBuffer* buffer) override {
    last_buffer_ = buffer;
    ++pushed_buffer_count_;

    switch (pipeline_status_) {
      case PIPELINE_STATUS_OK:
        return MediaPipelineBackend::kBufferSuccess;
      case PIPELINE_STATUS_BUSY:
        pending_push_ = true;
        return MediaPipelineBackend::kBufferPending;
      case PIPELINE_STATUS_ERROR:
        return MediaPipelineBackend::kBufferFailed;
      case PIPELINE_STATUS_ASYNC_ERROR:
        pending_push_ = true;
        delegate_->OnDecoderError(this);
        return MediaPipelineBackend::kBufferPending;
      default:
        NOTREACHED();
        return MediaPipelineBackend::kBufferFailed;
    }
  }
  void GetStatistics(Statistics* statistics) override {}
  bool SetConfig(const AudioConfig& config) override {
    config_ = config;
    return true;
  }
  bool SetVolume(float volume) override {
    volume_ = volume;
    return true;
  }
  RenderingDelay GetRenderingDelay() override { return RenderingDelay(); }

  const AudioConfig& config() const { return config_; }
  float volume() const { return volume_; }
  void set_pipeline_status(PipelineStatus status) {
    if (status == PIPELINE_STATUS_OK && pending_push_) {
      pending_push_ = false;
      delegate_->OnPushBufferComplete(this,
                                      MediaPipelineBackend::kBufferSuccess);
    }
    pipeline_status_ = status;
  }
  unsigned pushed_buffer_count() const { return pushed_buffer_count_; }
  CastDecoderBuffer* last_buffer() { return last_buffer_; }
  void set_delegate(MediaPipelineBackend::Delegate* delegate) {
    delegate_ = delegate;
  }

 private:
  AudioConfig config_;
  float volume_;

  PipelineStatus pipeline_status_;
  bool pending_push_;
  int pushed_buffer_count_;
  CastDecoderBuffer* last_buffer_;
  MediaPipelineBackend::Delegate* delegate_;
};

class FakeMediaPipelineBackend : public MediaPipelineBackend {
 public:
  enum State { kStateStopped, kStateRunning, kStatePaused };

  FakeMediaPipelineBackend() : state_(kStateStopped), audio_decoder_(nullptr) {}
  ~FakeMediaPipelineBackend() override {}

  // MediaPipelineBackend implementation:
  AudioDecoder* CreateAudioDecoder() override {
    DCHECK(!audio_decoder_);
    audio_decoder_ = new FakeAudioDecoder();
    return audio_decoder_;
  }
  VideoDecoder* CreateVideoDecoder() override {
    NOTREACHED();
    return nullptr;
  }

  bool Initialize(Delegate* delegate) override {
    audio_decoder_->set_delegate(delegate);
    return true;
  }
  bool Start(int64_t start_pts) override {
    EXPECT_EQ(kStateStopped, state_);
    state_ = kStateRunning;
    return true;
  }
  bool Stop() override {
    EXPECT_TRUE(state_ == kStateRunning || state_ == kStatePaused);
    state_ = kStateStopped;
    return true;
  }
  bool Pause() override {
    EXPECT_EQ(kStateRunning, state_);
    state_ = kStatePaused;
    return true;
  }
  bool Resume() override {
    EXPECT_EQ(kStatePaused, state_);
    state_ = kStateRunning;
    return true;
  }
  int64_t GetCurrentPts() override { return 0; }
  bool SetPlaybackRate(float rate) override { return true; }

  State state() const { return state_; }
  FakeAudioDecoder* decoder() const { return audio_decoder_; }

 private:
  State state_;
  FakeAudioDecoder* audio_decoder_;
};

class FakeAudioSourceCallback
    : public ::media::AudioOutputStream::AudioSourceCallback {
 public:
  FakeAudioSourceCallback() : error_(false) {}

  bool error() const { return error_; }

  // ::media::AudioOutputStream::AudioSourceCallback overrides.
  int OnMoreData(::media::AudioBus* audio_bus,
                 uint32 total_bytes_delay) override {
    audio_bus->Zero();
    return audio_bus->frames();
  }
  void OnError(::media::AudioOutputStream* stream) override { error_ = true; }

 private:
  bool error_;
};

class FakeAudioManager : public CastAudioManager {
 public:
  FakeAudioManager()
      : CastAudioManager(nullptr), media_pipeline_backend_(nullptr) {}
  ~FakeAudioManager() override {}

  // CastAudioManager overrides.
  scoped_ptr<MediaPipelineBackend> CreateMediaPipelineBackend(
      const MediaPipelineDeviceParams& params) override {
    DCHECK(media::MediaMessageLoop::GetTaskRunner()->BelongsToCurrentThread());
    DCHECK(!media_pipeline_backend_);

    scoped_ptr<FakeMediaPipelineBackend> backend(
        new FakeMediaPipelineBackend());
    // Cache the backend locally to be used by tests.
    media_pipeline_backend_ = backend.get();
    return backend.Pass();
  }
  void ReleaseOutputStream(::media::AudioOutputStream* stream) override {
    DCHECK(media_pipeline_backend_);
    media_pipeline_backend_ = nullptr;
    CastAudioManager::ReleaseOutputStream(stream);
  }

  // Returns the MediaPipelineBackend being used by the AudioOutputStream.
  // Note: here is a valid MediaPipelineBackend only while the stream is open.
  // Returns NULL at all other times.
  FakeMediaPipelineBackend* media_pipeline_backend() {
    return media_pipeline_backend_;
  }

 private:
  FakeMediaPipelineBackend* media_pipeline_backend_;
};

class CastAudioOutputStreamTest : public ::testing::Test {
 public:
  CastAudioOutputStreamTest()
      : format_(::media::AudioParameters::AUDIO_PCM_LINEAR),
        channel_layout_(::media::CHANNEL_LAYOUT_MONO),
        sample_rate_(::media::AudioParameters::kAudioCDSampleRate),
        bits_per_sample_(16),
        frames_per_buffer_(256) {}
  ~CastAudioOutputStreamTest() override {}

 protected:
  void SetUp() override {
    metrics::InitializeMetricsHelperForTesting();

    audio_manager_.reset(new FakeAudioManager);
    audio_task_runner_ = audio_manager_->GetTaskRunner();
    backend_task_runner_ = media::MediaMessageLoop::GetTaskRunner();
  }

  void TearDown() override {
    audio_manager_.reset();
  }

  ::media::AudioParameters GetAudioParams() {
    return ::media::AudioParameters(format_, channel_layout_, sample_rate_,
                                    bits_per_sample_, frames_per_buffer_);
  }

  FakeMediaPipelineBackend* GetBackend() {
    return audio_manager_->media_pipeline_backend();
  }

  FakeAudioDecoder* GetAudio() {
    FakeMediaPipelineBackend* backend = GetBackend();
    return (backend ? backend->decoder() : nullptr);
  }

  // Synchronous utility functions.
  ::media::AudioOutputStream* CreateStream() {
    ::media::AudioOutputStream* stream = nullptr;

    base::WaitableEvent completion_event(false, false);
    audio_task_runner_->PostTask(
        FROM_HERE,
        base::Bind(&CastAudioOutputStreamTest::CreateStreamOnAudioThread,
                   base::Unretained(this), GetAudioParams(), &stream,
                   &completion_event));
    completion_event.Wait();

    return stream;
  }
  bool OpenStream(::media::AudioOutputStream* stream) {
    DCHECK(stream);

    bool success = false;
    base::WaitableEvent completion_event(false, false);
    audio_task_runner_->PostTask(
        FROM_HERE,
        base::Bind(&CastAudioOutputStreamTest::OpenStreamOnAudioThread,
                   base::Unretained(this), stream, &success,
                   &completion_event));
    completion_event.Wait();

    // Drain the backend task runner so that appropriate states are set on
    // the backend pipeline devices.
    RunUntilIdle(backend_task_runner_.get());
    return success;
  }
  void CloseStream(::media::AudioOutputStream* stream) {
    audio_task_runner_->PostTask(FROM_HERE,
                                 base::Bind(&::media::AudioOutputStream::Close,
                                            base::Unretained(stream)));
    RunUntilIdle(audio_task_runner_.get());
    RunUntilIdle(backend_task_runner_.get());
    // Backend task runner may have posted more tasks to the audio task runner.
    // So we need to drain it once more.
    RunUntilIdle(audio_task_runner_.get());
  }
  void StartStream(
      ::media::AudioOutputStream* stream,
      ::media::AudioOutputStream::AudioSourceCallback* source_callback) {
    audio_task_runner_->PostTask(
        FROM_HERE, base::Bind(&::media::AudioOutputStream::Start,
                              base::Unretained(stream), source_callback));
    // Drain the audio task runner twice so that tasks posted by
    // media::AudioOutputStream::Start are run as well.
    RunUntilIdle(audio_task_runner_.get());
    RunUntilIdle(audio_task_runner_.get());
    // Drain the backend task runner so that appropriate states are set on
    // the backend pipeline devices.
    RunUntilIdle(backend_task_runner_.get());
    // Drain the audio task runner again to run the tasks posted by the
    // backend on audio task runner.
    RunUntilIdle(audio_task_runner_.get());
  }
  void StopStream(::media::AudioOutputStream* stream) {
    audio_task_runner_->PostTask(FROM_HERE,
                                 base::Bind(&::media::AudioOutputStream::Stop,
                                            base::Unretained(stream)));
    RunUntilIdle(audio_task_runner_.get());
    // Drain the backend task runner so that appropriate states are set on
    // the backend pipeline devices.
    RunUntilIdle(backend_task_runner_.get());
  }
  void SetStreamVolume(::media::AudioOutputStream* stream, double volume) {
    audio_task_runner_->PostTask(
        FROM_HERE, base::Bind(&::media::AudioOutputStream::SetVolume,
                              base::Unretained(stream), volume));
    RunUntilIdle(audio_task_runner_.get());
    // Drain the backend task runner so that appropriate states are set on
    // the backend pipeline devices.
    RunUntilIdle(backend_task_runner_.get());
  }
  double GetStreamVolume(::media::AudioOutputStream* stream) {
    double volume = 0.0;
    audio_task_runner_->PostTask(
        FROM_HERE, base::Bind(&::media::AudioOutputStream::GetVolume,
                              base::Unretained(stream), &volume));
    RunUntilIdle(audio_task_runner_.get());
    // No need to drain the backend task runner because getting the volume
    // does not involve posting any task to the backend.
    return volume;
  }

  void CreateStreamOnAudioThread(const ::media::AudioParameters& audio_params,
                                 ::media::AudioOutputStream** stream,
                                 base::WaitableEvent* completion_event) {
    DCHECK(audio_task_runner_->BelongsToCurrentThread());
    *stream = audio_manager_->MakeAudioOutputStream(GetAudioParams(),
                                                    kDefaultDeviceId);
    completion_event->Signal();
  }
  void OpenStreamOnAudioThread(::media::AudioOutputStream* stream,
                               bool* success,
                               base::WaitableEvent* completion_event) {
    DCHECK(audio_task_runner_->BelongsToCurrentThread());
    *success = stream->Open();
    completion_event->Signal();
  }

  scoped_ptr<FakeAudioManager> audio_manager_;
  scoped_refptr<base::SingleThreadTaskRunner> audio_task_runner_;
  scoped_refptr<base::SingleThreadTaskRunner> backend_task_runner_;

  // AudioParameters used to create AudioOutputStream.
  // Tests can modify these parameters before calling CreateStream.
  ::media::AudioParameters::Format format_;
  ::media::ChannelLayout channel_layout_;
  int sample_rate_;
  int bits_per_sample_;
  int frames_per_buffer_;
};

TEST_F(CastAudioOutputStreamTest, Format) {
  ::media::AudioParameters::Format format[] = {
      //::media::AudioParameters::AUDIO_PCM_LINEAR,
      ::media::AudioParameters::AUDIO_PCM_LOW_LATENCY};
  for (size_t i = 0; i < arraysize(format); ++i) {
    format_ = format[i];
    ::media::AudioOutputStream* stream = CreateStream();
    ASSERT_TRUE(stream);
    EXPECT_TRUE(OpenStream(stream));

    FakeAudioDecoder* audio_decoder = GetAudio();
    ASSERT_TRUE(audio_decoder);
    const AudioConfig& audio_config = audio_decoder->config();
    EXPECT_EQ(kCodecPCM, audio_config.codec);
    EXPECT_EQ(kSampleFormatS16, audio_config.sample_format);
    EXPECT_FALSE(audio_config.is_encrypted);

    CloseStream(stream);
  }
}

TEST_F(CastAudioOutputStreamTest, ChannelLayout) {
  ::media::ChannelLayout layout[] = {::media::CHANNEL_LAYOUT_MONO,
                                     ::media::CHANNEL_LAYOUT_STEREO};
  for (size_t i = 0; i < arraysize(layout); ++i) {
    channel_layout_ = layout[i];
    ::media::AudioOutputStream* stream = CreateStream();
    ASSERT_TRUE(stream);
    EXPECT_TRUE(OpenStream(stream));

    FakeAudioDecoder* audio_decoder = GetAudio();
    ASSERT_TRUE(audio_decoder);
    const AudioConfig& audio_config = audio_decoder->config();
    EXPECT_EQ(::media::ChannelLayoutToChannelCount(channel_layout_),
              audio_config.channel_number);

    CloseStream(stream);
  }
}

TEST_F(CastAudioOutputStreamTest, SampleRate) {
  sample_rate_ = ::media::AudioParameters::kAudioCDSampleRate;
  ::media::AudioOutputStream* stream = CreateStream();
  ASSERT_TRUE(stream);
  EXPECT_TRUE(OpenStream(stream));

  FakeAudioDecoder* audio_decoder = GetAudio();
  ASSERT_TRUE(audio_decoder);
  const AudioConfig& audio_config = audio_decoder->config();
  EXPECT_EQ(sample_rate_, audio_config.samples_per_second);

  CloseStream(stream);
}

TEST_F(CastAudioOutputStreamTest, BitsPerSample) {
  bits_per_sample_ = 16;
  ::media::AudioOutputStream* stream = CreateStream();
  ASSERT_TRUE(stream);
  EXPECT_TRUE(OpenStream(stream));

  FakeAudioDecoder* audio_decoder = GetAudio();
  ASSERT_TRUE(audio_decoder);
  const AudioConfig& audio_config = audio_decoder->config();
  EXPECT_EQ(bits_per_sample_ / 8, audio_config.bytes_per_channel);

  CloseStream(stream);
}

TEST_F(CastAudioOutputStreamTest, DeviceState) {
  ::media::AudioOutputStream* stream = CreateStream();
  ASSERT_TRUE(stream);
  EXPECT_FALSE(GetAudio());

  EXPECT_TRUE(OpenStream(stream));
  FakeAudioDecoder* audio_decoder = GetAudio();
  ASSERT_TRUE(audio_decoder);
  FakeMediaPipelineBackend* backend = GetBackend();
  ASSERT_TRUE(backend);
  EXPECT_EQ(FakeMediaPipelineBackend::kStateStopped, backend->state());

  scoped_ptr<FakeAudioSourceCallback> source_callback(
      new FakeAudioSourceCallback);
  StartStream(stream, source_callback.get());
  EXPECT_EQ(FakeMediaPipelineBackend::kStateRunning, backend->state());

  StopStream(stream);
  EXPECT_EQ(FakeMediaPipelineBackend::kStatePaused, backend->state());

  CloseStream(stream);
  EXPECT_FALSE(GetAudio());
}

TEST_F(CastAudioOutputStreamTest, PushFrame) {
  ::media::AudioOutputStream* stream = CreateStream();
  ASSERT_TRUE(stream);
  EXPECT_TRUE(OpenStream(stream));

  FakeAudioDecoder* audio_decoder = GetAudio();
  ASSERT_TRUE(audio_decoder);
  // Verify initial state.
  EXPECT_EQ(0u, audio_decoder->pushed_buffer_count());
  EXPECT_FALSE(audio_decoder->last_buffer());

  scoped_ptr<FakeAudioSourceCallback> source_callback(
      new FakeAudioSourceCallback);
  StartStream(stream, source_callback.get());
  StopStream(stream);

  // Verify that the stream pushed frames to the backend.
  EXPECT_LT(0u, audio_decoder->pushed_buffer_count());
  EXPECT_TRUE(audio_decoder->last_buffer());

  // Verify decoder buffer.
  ::media::AudioParameters audio_params = GetAudioParams();
  const size_t expected_frame_size =
      static_cast<size_t>(audio_params.GetBytesPerBuffer());
  const CastDecoderBuffer* buffer = audio_decoder->last_buffer();
  EXPECT_TRUE(buffer->data());
  EXPECT_EQ(expected_frame_size, buffer->data_size());
  EXPECT_FALSE(buffer->decrypt_config());  // Null because of raw audio.
  EXPECT_FALSE(buffer->end_of_stream());

  // No error must be reported to source callback.
  EXPECT_FALSE(source_callback->error());

  CloseStream(stream);
}

TEST_F(CastAudioOutputStreamTest, DeviceBusy) {
  ::media::AudioOutputStream* stream = CreateStream();
  ASSERT_TRUE(stream);
  EXPECT_TRUE(OpenStream(stream));

  FakeAudioDecoder* audio_decoder = GetAudio();
  ASSERT_TRUE(audio_decoder);
  audio_decoder->set_pipeline_status(FakeAudioDecoder::PIPELINE_STATUS_BUSY);

  scoped_ptr<FakeAudioSourceCallback> source_callback(
      new FakeAudioSourceCallback);
  StartStream(stream, source_callback.get());

  // Make sure that one frame was pushed.
  EXPECT_EQ(1u, audio_decoder->pushed_buffer_count());
  // No error must be reported to source callback.
  EXPECT_FALSE(source_callback->error());

  // Sleep for a few frames and verify that more frames were not pushed
  // because the backend device was busy.
  ::media::AudioParameters audio_params = GetAudioParams();
  base::TimeDelta pause = audio_params.GetBufferDuration() * 5;
  base::PlatformThread::Sleep(pause);
  RunUntilIdle(audio_task_runner_.get());
  RunUntilIdle(backend_task_runner_.get());
  EXPECT_EQ(1u, audio_decoder->pushed_buffer_count());

  // Unblock the pipeline and verify that PushFrame resumes.
  // (have to post because this directly calls buffer complete)
  backend_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&FakeAudioDecoder::set_pipeline_status,
                 base::Unretained(audio_decoder),
                 FakeAudioDecoder::PIPELINE_STATUS_OK));

  base::PlatformThread::Sleep(pause);
  RunUntilIdle(audio_task_runner_.get());
  RunUntilIdle(backend_task_runner_.get());
  EXPECT_LT(1u, audio_decoder->pushed_buffer_count());
  EXPECT_FALSE(source_callback->error());

  StopStream(stream);
  CloseStream(stream);
}

TEST_F(CastAudioOutputStreamTest, DeviceError) {
  ::media::AudioOutputStream* stream = CreateStream();
  ASSERT_TRUE(stream);
  EXPECT_TRUE(OpenStream(stream));

  FakeAudioDecoder* audio_decoder = GetAudio();
  ASSERT_TRUE(audio_decoder);
  audio_decoder->set_pipeline_status(FakeAudioDecoder::PIPELINE_STATUS_ERROR);

  scoped_ptr<FakeAudioSourceCallback> source_callback(
      new FakeAudioSourceCallback);
  StartStream(stream, source_callback.get());

  // Make sure that AudioOutputStream attempted to push the initial frame.
  EXPECT_LT(0u, audio_decoder->pushed_buffer_count());
  // AudioOutputStream must report error to source callback.
  EXPECT_TRUE(source_callback->error());

  StopStream(stream);
  CloseStream(stream);
}

TEST_F(CastAudioOutputStreamTest, DeviceAsyncError) {
  ::media::AudioOutputStream* stream = CreateStream();
  ASSERT_TRUE(stream);
  EXPECT_TRUE(OpenStream(stream));

  FakeAudioDecoder* audio_decoder = GetAudio();
  ASSERT_TRUE(audio_decoder);
  audio_decoder->set_pipeline_status(
      FakeAudioDecoder::PIPELINE_STATUS_ASYNC_ERROR);

  scoped_ptr<FakeAudioSourceCallback> source_callback(
      new FakeAudioSourceCallback);
  StartStream(stream, source_callback.get());

  // Make sure that one frame was pushed.
  EXPECT_EQ(1u, audio_decoder->pushed_buffer_count());

  // Unblock the pipeline and verify that PushFrame resumes.
  // (have to post because this directly calls buffer complete)
  backend_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&FakeAudioDecoder::set_pipeline_status,
                 base::Unretained(audio_decoder),
                 FakeAudioDecoder::PIPELINE_STATUS_OK));

  RunUntilIdle(audio_task_runner_.get());
  RunUntilIdle(backend_task_runner_.get());
  // AudioOutputStream must report error to source callback.
  EXPECT_TRUE(source_callback->error());

  StopStream(stream);
  CloseStream(stream);
}

TEST_F(CastAudioOutputStreamTest, Volume) {
  ::media::AudioOutputStream* stream = CreateStream();
  ASSERT_TRUE(stream);
  ASSERT_TRUE(OpenStream(stream));
  FakeAudioDecoder* audio_decoder = GetAudio();
  ASSERT_TRUE(audio_decoder);

  double volume = GetStreamVolume(stream);
  EXPECT_EQ(1.0, volume);
  EXPECT_EQ(1.0f, audio_decoder->volume());

  SetStreamVolume(stream, 0.5);
  volume = GetStreamVolume(stream);
  EXPECT_EQ(0.5, volume);
  EXPECT_EQ(0.5f, audio_decoder->volume());

  CloseStream(stream);
}

TEST_F(CastAudioOutputStreamTest, StartStopStart) {
  ::media::AudioOutputStream* stream = CreateStream();
  ASSERT_TRUE(stream);
  ASSERT_TRUE(OpenStream(stream));

  scoped_ptr<FakeAudioSourceCallback> source_callback(
      new FakeAudioSourceCallback);
  audio_task_runner_->PostTask(
      FROM_HERE, base::Bind(&::media::AudioOutputStream::Start,
                            base::Unretained(stream), source_callback.get()));
  audio_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&::media::AudioOutputStream::Stop, base::Unretained(stream)));
  audio_task_runner_->PostTask(
      FROM_HERE, base::Bind(&::media::AudioOutputStream::Start,
                            base::Unretained(stream), source_callback.get()));
  RunUntilIdle(audio_task_runner_.get());
  RunUntilIdle(backend_task_runner_.get());

  FakeAudioDecoder* audio_device = GetAudio();
  EXPECT_TRUE(audio_device);
  EXPECT_EQ(FakeMediaPipelineBackend::kStateRunning, GetBackend()->state());

  CloseStream(stream);
}

TEST_F(CastAudioOutputStreamTest, CloseWithoutStart) {
  ::media::AudioOutputStream* stream = CreateStream();
  ASSERT_TRUE(stream);
  ASSERT_TRUE(OpenStream(stream));
  CloseStream(stream);
}

}  // namespace
}  // namespace media
}  // namespace chromecast
