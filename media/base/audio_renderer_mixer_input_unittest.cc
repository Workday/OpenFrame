// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/run_loop.h"
#include "media/base/audio_renderer_mixer.h"
#include "media/base/audio_renderer_mixer_input.h"
#include "media/base/fake_audio_render_callback.h"
#include "media/base/mock_audio_renderer_sink.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace media {

static const int kBitsPerChannel = 16;
static const int kSampleRate = 48000;
static const int kBufferSize = 8192;
static const ChannelLayout kChannelLayout = CHANNEL_LAYOUT_STEREO;
static const std::string kDefaultDeviceId;
static const std::string kUnauthorizedDeviceId("unauthorized");
static const std::string kNonexistentDeviceId("nonexistent");
static const url::Origin kDefaultSecurityOrigin;

class AudioRendererMixerInputTest : public testing::Test {
 public:
  AudioRendererMixerInputTest() {
    audio_parameters_ = AudioParameters(
        AudioParameters::AUDIO_PCM_LINEAR, kChannelLayout, kSampleRate,
        kBitsPerChannel, kBufferSize);

    CreateMixerInput(kDefaultDeviceId);
    fake_callback_.reset(new FakeAudioRenderCallback(0));
    mixer_input_->Initialize(audio_parameters_, fake_callback_.get());
    audio_bus_ = AudioBus::Create(audio_parameters_);
  }

  void CreateMixerInput(const std::string& device_id) {
    mixer_input_ = new AudioRendererMixerInput(
        base::Bind(&AudioRendererMixerInputTest::GetMixer,
                   base::Unretained(this)),
        base::Bind(&AudioRendererMixerInputTest::RemoveMixer,
                   base::Unretained(this)),
        device_id, kDefaultSecurityOrigin);
  }

  AudioRendererMixer* GetMixer(const AudioParameters& params,
                               const std::string& device_id,
                               const url::Origin& security_origin,
                               OutputDeviceStatus* device_status) {
    if (device_id == kNonexistentDeviceId) {
      if (device_status)
        *device_status = OUTPUT_DEVICE_STATUS_ERROR_NOT_FOUND;
      return nullptr;
    }

    if (device_id == kUnauthorizedDeviceId) {
      if (device_status)
        *device_status = OUTPUT_DEVICE_STATUS_ERROR_NOT_AUTHORIZED;
      return nullptr;
    }

    size_t idx = device_id.empty() ? 0 : 1;
    if (!mixers_[idx]) {
      scoped_refptr<MockAudioRendererSink> sink = new MockAudioRendererSink();
      EXPECT_CALL(*sink.get(), Start());
      EXPECT_CALL(*sink.get(), Stop());

      mixers_[idx].reset(
          new AudioRendererMixer(audio_parameters_, audio_parameters_, sink));
    }
    EXPECT_CALL(*this, RemoveMixer(testing::_, device_id, testing::_));

    if (device_status)
      *device_status = OUTPUT_DEVICE_STATUS_OK;
    return mixers_[idx].get();
  }

  double ProvideInput() {
    return mixer_input_->ProvideInput(audio_bus_.get(), base::TimeDelta());
  }

  MOCK_METHOD3(RemoveMixer,
               void(const AudioParameters&,
                    const std::string&,
                    const url::Origin&));

  MOCK_METHOD1(SwitchCallbackCalled, void(OutputDeviceStatus));
  void SwitchCallback(base::RunLoop* loop, OutputDeviceStatus result) {
    SwitchCallbackCalled(result);
    loop->Quit();
  }

  AudioRendererMixer* GetInputMixer() { return mixer_input_->mixer_; }

 protected:
  virtual ~AudioRendererMixerInputTest() {}

  AudioParameters audio_parameters_;
  scoped_ptr<AudioRendererMixer> mixers_[2];
  scoped_refptr<AudioRendererMixerInput> mixer_input_;
  scoped_ptr<FakeAudioRenderCallback> fake_callback_;
  scoped_ptr<AudioBus> audio_bus_;

 private:
  DISALLOW_COPY_AND_ASSIGN(AudioRendererMixerInputTest);
};

// Test that getting and setting the volume work as expected.  The volume is
// returned from ProvideInput() only when playing.
TEST_F(AudioRendererMixerInputTest, GetSetVolume) {
  mixer_input_->Start();
  mixer_input_->Play();

  // Starting volume should be 1.0.
  EXPECT_DOUBLE_EQ(ProvideInput(), 1);

  const double kVolume = 0.5;
  EXPECT_TRUE(mixer_input_->SetVolume(kVolume));
  EXPECT_DOUBLE_EQ(ProvideInput(), kVolume);

  mixer_input_->Stop();
}

// Test Start()/Play()/Pause()/Stop()/playing() all work as expected.  Also
// implicitly tests that AddMixerInput() and RemoveMixerInput() work without
// crashing; functional tests for these methods are in AudioRendererMixerTest.
TEST_F(AudioRendererMixerInputTest, StartPlayPauseStopPlaying) {
  mixer_input_->Start();
  mixer_input_->Play();
  EXPECT_DOUBLE_EQ(ProvideInput(), 1);
  mixer_input_->Pause();
  mixer_input_->Play();
  EXPECT_DOUBLE_EQ(ProvideInput(), 1);
  mixer_input_->Stop();
}

// Test that Stop() can be called before Initialize() and Start().
TEST_F(AudioRendererMixerInputTest, StopBeforeInitializeOrStart) {
  // |mixer_input_| was initialized during construction.
  mixer_input_->Stop();

  // Verify Stop() works without Initialize() or Start().
  CreateMixerInput(kDefaultDeviceId);
  mixer_input_->Stop();
}

// Test that Start() can be called after Stop().
TEST_F(AudioRendererMixerInputTest, StartAfterStop) {
  mixer_input_->Stop();
  mixer_input_->Start();
  mixer_input_->Stop();
}

// Test that Initialize() can be called again after Stop().
TEST_F(AudioRendererMixerInputTest, InitializeAfterStop) {
  mixer_input_->Initialize(audio_parameters_, fake_callback_.get());
  mixer_input_->Start();
  mixer_input_->Stop();
  mixer_input_->Initialize(audio_parameters_, fake_callback_.get());
  mixer_input_->Stop();
}

// Test SwitchOutputDevice().
TEST_F(AudioRendererMixerInputTest, SwitchOutputDevice) {
  mixer_input_->Start();
  const std::string kDeviceId("mock-device-id");
  EXPECT_CALL(*this, SwitchCallbackCalled(OUTPUT_DEVICE_STATUS_OK));
  AudioRendererMixer* old_mixer = GetInputMixer();
  EXPECT_EQ(old_mixer, mixers_[0].get());
  base::RunLoop run_loop;
  mixer_input_->SwitchOutputDevice(
      kDeviceId, kDefaultSecurityOrigin,
      base::Bind(&AudioRendererMixerInputTest::SwitchCallback,
                 base::Unretained(this), &run_loop));
  run_loop.Run();
  AudioRendererMixer* new_mixer = GetInputMixer();
  EXPECT_EQ(new_mixer, mixers_[1].get());
  EXPECT_NE(old_mixer, new_mixer);
  mixer_input_->Stop();
}

// Test SwitchOutputDevice() to the same device as the current (default) device
TEST_F(AudioRendererMixerInputTest, SwitchOutputDeviceToSameDevice) {
  mixer_input_->Start();
  EXPECT_CALL(*this, SwitchCallbackCalled(OUTPUT_DEVICE_STATUS_OK));
  AudioRendererMixer* old_mixer = GetInputMixer();
  base::RunLoop run_loop;
  mixer_input_->SwitchOutputDevice(
      kDefaultDeviceId, kDefaultSecurityOrigin,
      base::Bind(&AudioRendererMixerInputTest::SwitchCallback,
                 base::Unretained(this), &run_loop));
  run_loop.Run();
  AudioRendererMixer* new_mixer = GetInputMixer();
  EXPECT_EQ(old_mixer, new_mixer);
  mixer_input_->Stop();
}

// Test that SwitchOutputDevice() to a nonexistent device fails.
TEST_F(AudioRendererMixerInputTest, SwitchOutputDeviceToNonexistentDevice) {
  mixer_input_->Start();
  EXPECT_CALL(*this,
              SwitchCallbackCalled(OUTPUT_DEVICE_STATUS_ERROR_NOT_FOUND));
  base::RunLoop run_loop;
  mixer_input_->SwitchOutputDevice(
      kNonexistentDeviceId, kDefaultSecurityOrigin,
      base::Bind(&AudioRendererMixerInputTest::SwitchCallback,
                 base::Unretained(this), &run_loop));
  run_loop.Run();
  mixer_input_->Stop();
}

// Test that SwitchOutputDevice() to an unauthorized device fails.
TEST_F(AudioRendererMixerInputTest, SwitchOutputDeviceToUnauthorizedDevice) {
  mixer_input_->Start();
  EXPECT_CALL(*this,
              SwitchCallbackCalled(OUTPUT_DEVICE_STATUS_ERROR_NOT_AUTHORIZED));
  base::RunLoop run_loop;
  mixer_input_->SwitchOutputDevice(
      kUnauthorizedDeviceId, kDefaultSecurityOrigin,
      base::Bind(&AudioRendererMixerInputTest::SwitchCallback,
                 base::Unretained(this), &run_loop));
  run_loop.Run();
  mixer_input_->Stop();
}

// Test that calling SwitchOutputDevice() before Start() is resolved after
// a call to Start().
TEST_F(AudioRendererMixerInputTest, SwitchOutputDeviceBeforeStart) {
  base::RunLoop run_loop;
  mixer_input_->SwitchOutputDevice(
      kDefaultDeviceId, kDefaultSecurityOrigin,
      base::Bind(&AudioRendererMixerInputTest::SwitchCallback,
                 base::Unretained(this), &run_loop));
  EXPECT_CALL(*this, SwitchCallbackCalled(OUTPUT_DEVICE_STATUS_OK));
  mixer_input_->Start();
  run_loop.Run();
  mixer_input_->Stop();
}

// Test that calling SwitchOutputDevice() without ever calling Start() fails
// cleanly after calling Stop().
TEST_F(AudioRendererMixerInputTest, SwitchOutputDeviceWithoutStart) {
  base::RunLoop run_loop;
  mixer_input_->SwitchOutputDevice(
      kDefaultDeviceId, kDefaultSecurityOrigin,
      base::Bind(&AudioRendererMixerInputTest::SwitchCallback,
                 base::Unretained(this), &run_loop));
  EXPECT_CALL(*this, SwitchCallbackCalled(OUTPUT_DEVICE_STATUS_ERROR_INTERNAL));
  mixer_input_->Stop();
  run_loop.Run();
}

// Test creation with an invalid device. OnRenderError() should be called.
// Play() and Pause() should not cause crashes, even if they have no effect.
// SwitchOutputDevice() should fail.
TEST_F(AudioRendererMixerInputTest, CreateWithInvalidDevice) {
  // |mixer_input_| was initialized during construction.
  mixer_input_->Stop();

  CreateMixerInput(kNonexistentDeviceId);
  EXPECT_CALL(*fake_callback_, OnRenderError());
  mixer_input_->Initialize(audio_parameters_, fake_callback_.get());
  mixer_input_->Start();
  mixer_input_->Play();
  mixer_input_->Pause();
  base::RunLoop run_loop;
  EXPECT_CALL(*this, SwitchCallbackCalled(OUTPUT_DEVICE_STATUS_ERROR_INTERNAL));
  mixer_input_->SwitchOutputDevice(
      kDefaultDeviceId, kDefaultSecurityOrigin,
      base::Bind(&AudioRendererMixerInputTest::SwitchCallback,
                 base::Unretained(this), &run_loop));
  mixer_input_->Stop();
  run_loop.Run();
}

}  // namespace media
