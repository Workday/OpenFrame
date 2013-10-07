// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/audio/cras_audio_handler.h"

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/values.h"
#include "chromeos/audio/audio_devices_pref_handler_stub.h"
#include "chromeos/dbus/audio_node.h"
#include "chromeos/dbus/cras_audio_client_stub_impl.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace chromeos {

const uint64 kInternalSpeakerId = 10001;
const uint64 kHeadphoneId = 10002;
const uint64 kInternalMicId = 10003;
const uint64 kUSBMicId = 10004;
const uint64 kBluetoothHeadsetId = 10005;
const uint64 kHDMIOutputId = 10006;
const uint64 kUSBHeadphoneId1 = 10007;
const uint64 kUSBHeadphoneId2 = 10008;
const uint64 kOtherTypeOutputId = 90001;
const uint64 kOtherTypeInputId = 90002;

const AudioNode kInternalSpeaker(
    false,
    kInternalSpeakerId,
    "Fake Speaker",
    "INTERNAL_SPEAKER",
    "Speaker",
    false,
    0
);

const AudioNode kHeadphone(
    false,
    kHeadphoneId,
    "Fake Headphone",
    "HEADPHONE",
    "Headphone",
    false,
    0
);

const AudioNode kInternalMic(
    true,
    kInternalMicId,
    "Fake Mic",
    "INTERNAL_MIC",
    "Internal Mic",
    false,
    0
);

const AudioNode kUSBMic(
    true,
    kUSBMicId,
    "Fake USB Mic",
    "USB",
    "USB Microphone",
    false,
    0
);

const AudioNode kOtherTypeOutput(
    false,
    kOtherTypeOutputId,
    "Output Device",
    "SOME_OTHER_TYPE",
    "Other Type Output Device",
    false,
    0
);

const AudioNode kOtherTypeInput(
    true,
    kOtherTypeInputId,
    "Input Device",
    "SOME_OTHER_TYPE",
    "Other Type Input Device",
    false,
    0
);

const AudioNode kBluetoothHeadset (
    false,
    kBluetoothHeadsetId,
    "Bluetooth Headset",
    "BLUETOOTH",
    "Bluetooth Headset 1",
    false,
    0
);

const AudioNode kHDMIOutput (
    false,
    kHDMIOutputId,
    "HDMI output",
    "HDMI",
    "HDMI output",
    false,
    0
);

const AudioNode kUSBHeadphone1 (
    false,
    kUSBHeadphoneId1,
    "USB Headphone",
    "USB",
    "USB Headphone 1",
    false,
    0
);

const AudioNode kUSBHeadphone2 (
    false,
    kUSBHeadphoneId2,
    "USB Headphone",
    "USB",
    "USB Headphone 1",
    false,
    0
);


class TestObserver : public chromeos::CrasAudioHandler::AudioObserver {
 public:
  TestObserver() : active_output_node_changed_count_(0),
                   active_input_node_changed_count_(0),
                   audio_nodes_changed_count_(0),
                   output_mute_changed_count_(0),
                   input_mute_changed_count_(0),
                   output_volume_changed_count_(0),
                   input_gain_changed_count_(0) {
  }

  int active_output_node_changed_count() const {
    return active_output_node_changed_count_;
  }

  int active_input_node_changed_count() const {
    return active_input_node_changed_count_;
  }

  int audio_nodes_changed_count() const {
    return audio_nodes_changed_count_;
  }

  int output_mute_changed_count() const {
    return output_mute_changed_count_;
  }

  int input_mute_changed_count() const {
    return input_mute_changed_count_;
  }

  int output_volume_changed_count() const {
    return output_volume_changed_count_;
  }

  int input_gain_changed_count() const {
    return input_gain_changed_count_;
  }

  virtual ~TestObserver() {}

 protected:
  // chromeos::CrasAudioHandler::AudioObserver overrides.
  virtual void OnActiveOutputNodeChanged() OVERRIDE {
    ++active_output_node_changed_count_;
  }

  virtual void OnActiveInputNodeChanged() OVERRIDE {
    ++active_input_node_changed_count_;
  }

  virtual void OnAudioNodesChanged() OVERRIDE {
    ++audio_nodes_changed_count_;
  }

  virtual void OnOutputMuteChanged() OVERRIDE {
    ++output_mute_changed_count_;
  }

  virtual void OnInputMuteChanged() OVERRIDE {
    ++input_mute_changed_count_;
  }

  virtual void OnOutputVolumeChanged() OVERRIDE {
    ++output_volume_changed_count_;
  }

  virtual void OnInputGainChanged() OVERRIDE {
    ++input_gain_changed_count_;
  }

 private:
  int active_output_node_changed_count_;
  int active_input_node_changed_count_;
  int audio_nodes_changed_count_;
  int output_mute_changed_count_;
  int input_mute_changed_count_;
  int output_volume_changed_count_;
  int input_gain_changed_count_;

  DISALLOW_COPY_AND_ASSIGN(TestObserver);
};

class CrasAudioHandlerTest : public testing::Test {
 public:
  CrasAudioHandlerTest() : cras_audio_handler_(NULL),
                           cras_audio_client_stub_(NULL) {
  }
  virtual ~CrasAudioHandlerTest() {}

  virtual void SetUp() OVERRIDE {
  }

  virtual void TearDown() OVERRIDE {
    cras_audio_handler_->RemoveAudioObserver(test_observer_.get());
    test_observer_.reset();
    CrasAudioHandler::Shutdown();
    audio_pref_handler_ = NULL;
    DBusThreadManager::Shutdown();
  }

  void SetUpCrasAudioHandler(const AudioNodeList& audio_nodes) {
    DBusThreadManager::InitializeWithStub();
    cras_audio_client_stub_ = static_cast<CrasAudioClientStubImpl*>(
        DBusThreadManager::Get()->GetCrasAudioClient());
    cras_audio_client_stub_->SetAudioDevices(audio_nodes);
    audio_pref_handler_ = new AudioDevicesPrefHandlerStub();
    CrasAudioHandler::Initialize(audio_pref_handler_);
    cras_audio_handler_ = CrasAudioHandler::Get();
    test_observer_.reset(new TestObserver);
    cras_audio_handler_->AddAudioObserver(test_observer_.get());
    message_loop_.RunUntilIdle();
  }

  void ChangeAudioNodes(const AudioNodeList& audio_nodes) {
    cras_audio_client_stub_->ChangeAudioNodes(audio_nodes);
    message_loop_.RunUntilIdle();
  }

 protected:
  base::MessageLoopForUI message_loop_;
  CrasAudioHandler* cras_audio_handler_;  // Not owned.
  CrasAudioClientStubImpl* cras_audio_client_stub_;  // Not owned.
  scoped_ptr<TestObserver> test_observer_;
  scoped_refptr<AudioDevicesPrefHandlerStub> audio_pref_handler_;

 private:
  DISALLOW_COPY_AND_ASSIGN(CrasAudioHandlerTest);
};

TEST_F(CrasAudioHandlerTest, InitializeWithOnlyDefaultAudioDevices) {
  AudioNodeList audio_nodes;
  audio_nodes.push_back(kInternalSpeaker);
  audio_nodes.push_back(kInternalMic);
  SetUpCrasAudioHandler(audio_nodes);

  // Verify the audio devices size.
  AudioDeviceList audio_devices;
  cras_audio_handler_->GetAudioDevices(&audio_devices);
  EXPECT_EQ(audio_nodes.size(), audio_devices.size());

  // Verify the internal speaker has been selected as the active output.
  AudioDevice active_output;
  EXPECT_TRUE(cras_audio_handler_->GetActiveOutputDevice(&active_output));
  EXPECT_EQ(kInternalSpeaker.id, active_output.id);
  EXPECT_EQ(kInternalSpeaker.id, cras_audio_handler_->GetActiveOutputNode());
  EXPECT_FALSE(cras_audio_handler_->has_alternative_output());

  // Ensure the internal microphone has been selected as the active input.
  AudioDevice active_input;
  EXPECT_EQ(kInternalMic.id, cras_audio_handler_->GetActiveInputNode());
  EXPECT_FALSE(cras_audio_handler_->has_alternative_input());
}

TEST_F(CrasAudioHandlerTest, InitializeWithAlternativeAudioDevices) {
  AudioNodeList audio_nodes;
  audio_nodes.push_back(kInternalSpeaker);
  audio_nodes.push_back(kHeadphone);
  audio_nodes.push_back(kInternalMic);
  audio_nodes.push_back(kUSBMic);
  SetUpCrasAudioHandler(audio_nodes);

  // Verify the audio devices size.
  AudioDeviceList audio_devices;
  cras_audio_handler_->GetAudioDevices(&audio_devices);
  EXPECT_EQ(audio_nodes.size(), audio_devices.size());

  // Verify the headphone has been selected as the active output.
  AudioDevice active_output;
  EXPECT_TRUE(cras_audio_handler_->GetActiveOutputDevice(&active_output));
  EXPECT_EQ(kHeadphone.id, active_output.id);
  EXPECT_EQ(kHeadphone.id, cras_audio_handler_->GetActiveOutputNode());
  EXPECT_TRUE(cras_audio_handler_->has_alternative_output());

  // Ensure the USB microphone has been selected as the active input.
  AudioDevice active_input;
  EXPECT_EQ(kUSBMicId, cras_audio_handler_->GetActiveInputNode());
  EXPECT_TRUE(cras_audio_handler_->has_alternative_input());
}

TEST_F(CrasAudioHandlerTest, SwitchActiveOutputDevice) {
  AudioNodeList audio_nodes;
  audio_nodes.push_back(kInternalSpeaker);
  audio_nodes.push_back(kHeadphone);
  SetUpCrasAudioHandler(audio_nodes);
  AudioDeviceList audio_devices;
  cras_audio_handler_->GetAudioDevices(&audio_devices);
  EXPECT_EQ(audio_nodes.size(), audio_devices.size());

  // Verify the initial active output device is headphone.
  EXPECT_EQ(0, test_observer_->active_output_node_changed_count());
  AudioDevice active_output;
  EXPECT_TRUE(cras_audio_handler_->GetActiveOutputDevice(&active_output));
  EXPECT_EQ(kHeadphone.id, active_output.id);
  EXPECT_EQ(kHeadphone.id, cras_audio_handler_->GetActiveOutputNode());

  // Switch the active output to internal speaker.
  AudioDevice internal_speaker(kInternalSpeaker);
  cras_audio_handler_->SwitchToDevice(internal_speaker);

  // Verify the active output is switched to internal speaker, and the
  // ActiveOutputNodeChanged event is fired.
  EXPECT_EQ(1, test_observer_->active_output_node_changed_count());
  EXPECT_TRUE(cras_audio_handler_->GetActiveOutputDevice(&active_output));
  EXPECT_EQ(kInternalSpeaker.id, active_output.id);
  EXPECT_EQ(kInternalSpeaker.id, cras_audio_handler_->GetActiveOutputNode());
}

TEST_F(CrasAudioHandlerTest, SwitchActiveInputDevice) {
  AudioNodeList audio_nodes;
  audio_nodes.push_back(kInternalMic);
  audio_nodes.push_back(kUSBMic);
  SetUpCrasAudioHandler(audio_nodes);
  AudioDeviceList audio_devices;
  cras_audio_handler_->GetAudioDevices(&audio_devices);
  EXPECT_EQ(audio_nodes.size(), audio_devices.size());

  // Verify the initial active input device is USB mic.
  EXPECT_EQ(0, test_observer_->active_input_node_changed_count());
  EXPECT_EQ(kUSBMicId, cras_audio_handler_->GetActiveInputNode());

  // Switch the active input to internal mic.
  AudioDevice internal_mic(kInternalMic);
  cras_audio_handler_->SwitchToDevice(internal_mic);

  // Verify the active output is switched to internal speaker, and the active
  // ActiveInputNodeChanged event is fired.
  EXPECT_EQ(1, test_observer_->active_input_node_changed_count());
  EXPECT_EQ(kInternalMic.id, cras_audio_handler_->GetActiveInputNode());
}

TEST_F(CrasAudioHandlerTest, PlugHeadphone) {
  // Set up initial audio devices, only with internal speaker.
  AudioNodeList audio_nodes;
  audio_nodes.push_back(kInternalSpeaker);
  SetUpCrasAudioHandler(audio_nodes);
  const size_t init_nodes_size = audio_nodes.size();

  // Verify the audio devices size.
  AudioDeviceList audio_devices;
  cras_audio_handler_->GetAudioDevices(&audio_devices);
  EXPECT_EQ(init_nodes_size, audio_devices.size());
  EXPECT_EQ(0, test_observer_->audio_nodes_changed_count());

  // Verify the internal speaker has been selected as the active output.
  EXPECT_EQ(0, test_observer_->active_output_node_changed_count());
  AudioDevice active_output;
  EXPECT_TRUE(cras_audio_handler_->GetActiveOutputDevice(&active_output));
  EXPECT_EQ(kInternalSpeaker.id, active_output.id);
  EXPECT_EQ(kInternalSpeaker.id, cras_audio_handler_->GetActiveOutputNode());
  EXPECT_FALSE(cras_audio_handler_->has_alternative_output());

  // Plug the headphone.
  audio_nodes.push_back(kHeadphone);
  ChangeAudioNodes(audio_nodes);

  // Verify the AudioNodesChanged event is fired and new audio device is added.
  EXPECT_EQ(1, test_observer_->audio_nodes_changed_count());
  cras_audio_handler_->GetAudioDevices(&audio_devices);
  EXPECT_EQ(init_nodes_size + 1, audio_devices.size());

  // Verify the active output device is switched to headphone and
  // ActiveOutputChanged event is fired.
  EXPECT_EQ(1, test_observer_->active_output_node_changed_count());
  EXPECT_TRUE(cras_audio_handler_->GetActiveOutputDevice(&active_output));
  EXPECT_EQ(kHeadphone.id, active_output.id);
  EXPECT_EQ(kHeadphone.id, cras_audio_handler_->GetActiveOutputNode());
  EXPECT_TRUE(cras_audio_handler_->has_alternative_output());
}

TEST_F(CrasAudioHandlerTest, UnplugHeadphone) {
  // Set up initial audio devices, with internal speaker and headphone.
  AudioNodeList audio_nodes;
  audio_nodes.push_back(kInternalSpeaker);
  audio_nodes.push_back(kHeadphone);
  SetUpCrasAudioHandler(audio_nodes);
  const size_t init_nodes_size = audio_nodes.size();

  // Verify the audio devices size.
  AudioDeviceList audio_devices;
  cras_audio_handler_->GetAudioDevices(&audio_devices);
  EXPECT_EQ(init_nodes_size, audio_devices.size());
  EXPECT_EQ(0, test_observer_->audio_nodes_changed_count());

  // Verify the headphone has been selected as the active output.
  EXPECT_EQ(0, test_observer_->active_output_node_changed_count());
  AudioDevice active_output;
  EXPECT_TRUE(cras_audio_handler_->GetActiveOutputDevice(&active_output));
  EXPECT_EQ(kHeadphone.id, active_output.id);
  EXPECT_EQ(kHeadphone.id, cras_audio_handler_->GetActiveOutputNode());
  EXPECT_TRUE(cras_audio_handler_->has_alternative_output());

  // Unplug the headphone.
  audio_nodes.clear();
  audio_nodes.push_back(kInternalSpeaker);
  ChangeAudioNodes(audio_nodes);

  // Verify the AudioNodesChanged event is fired and one audio device is
  // removed.
  EXPECT_EQ(1, test_observer_->audio_nodes_changed_count());
  cras_audio_handler_->GetAudioDevices(&audio_devices);
  EXPECT_EQ(init_nodes_size - 1, audio_devices.size());

  // Verify the active output device is switched to internal speaker and
  // ActiveOutputChanged event is fired.
  EXPECT_EQ(1, test_observer_->active_output_node_changed_count());
  EXPECT_TRUE(cras_audio_handler_->GetActiveOutputDevice(&active_output));
  EXPECT_EQ(kInternalSpeaker.id, active_output.id);
  EXPECT_EQ(kInternalSpeaker.id, cras_audio_handler_->GetActiveOutputNode());
  EXPECT_FALSE(cras_audio_handler_->has_alternative_output());
}

TEST_F(CrasAudioHandlerTest, InitializeWithBluetoothHeadset) {
  AudioNodeList audio_nodes;
  audio_nodes.push_back(kInternalSpeaker);
  audio_nodes.push_back(kBluetoothHeadset);
  SetUpCrasAudioHandler(audio_nodes);

  // Verify the audio devices size.
  AudioDeviceList audio_devices;
  cras_audio_handler_->GetAudioDevices(&audio_devices);
  EXPECT_EQ(audio_nodes.size(), audio_devices.size());
  EXPECT_EQ(0, test_observer_->audio_nodes_changed_count());

  // Verify the bluetooth headset has been selected as the active output.
  EXPECT_EQ(0, test_observer_->active_output_node_changed_count());
  AudioDevice active_output;
  EXPECT_TRUE(cras_audio_handler_->GetActiveOutputDevice(&active_output));
  EXPECT_EQ(kBluetoothHeadset.id, active_output.id);
  EXPECT_EQ(kBluetoothHeadset.id, cras_audio_handler_->GetActiveOutputNode());
  EXPECT_TRUE(cras_audio_handler_->has_alternative_output());
}

TEST_F(CrasAudioHandlerTest, ConnectAndDisconnectBluetoothHeadset) {
  // Initialize with internal speaker and headphone.
  AudioNodeList audio_nodes;
  audio_nodes.push_back(kInternalSpeaker);
  audio_nodes.push_back(kHeadphone);
  SetUpCrasAudioHandler(audio_nodes);
  const size_t init_nodes_size = audio_nodes.size();

  // Verify the audio devices size.
  AudioDeviceList audio_devices;
  cras_audio_handler_->GetAudioDevices(&audio_devices);
  EXPECT_EQ(init_nodes_size, audio_devices.size());
  EXPECT_EQ(0, test_observer_->audio_nodes_changed_count());

  // Verify the headphone is selected as the active output initially.
  EXPECT_EQ(0, test_observer_->active_output_node_changed_count());
  AudioDevice active_output;
  EXPECT_TRUE(cras_audio_handler_->GetActiveOutputDevice(&active_output));
  EXPECT_EQ(kHeadphone.id, active_output.id);
  EXPECT_EQ(kHeadphone.id, cras_audio_handler_->GetActiveOutputNode());
  EXPECT_TRUE(cras_audio_handler_->has_alternative_output());

  // Connect to bluetooth headset. Since it is plugged in later than
  // headphone, active output should be switched to it.
  audio_nodes.clear();
  audio_nodes.push_back(kInternalSpeaker);
  AudioNode headphone(kHeadphone);
  headphone.plugged_time = 80000000;
  headphone.active = true;
  audio_nodes.push_back(headphone);
  AudioNode bluetooth_headset(kBluetoothHeadset);
  bluetooth_headset.plugged_time = 90000000;
  audio_nodes.push_back(bluetooth_headset);
  ChangeAudioNodes(audio_nodes);

  // Verify the AudioNodesChanged event is fired and new audio device is added.
  EXPECT_EQ(1, test_observer_->audio_nodes_changed_count());
  cras_audio_handler_->GetAudioDevices(&audio_devices);
  EXPECT_EQ(init_nodes_size + 1, audio_devices.size());

  // Verify the active output device is switched to bluetooth headset, and
  // ActiveOutputChanged event is fired.
  EXPECT_EQ(1, test_observer_->active_output_node_changed_count());
  EXPECT_TRUE(cras_audio_handler_->GetActiveOutputDevice(&active_output));
  EXPECT_EQ(kBluetoothHeadset.id, active_output.id);
  EXPECT_EQ(kBluetoothHeadset.id, cras_audio_handler_->GetActiveOutputNode());
  EXPECT_TRUE(cras_audio_handler_->has_alternative_output());

  // Disconnect bluetooth headset.
  audio_nodes.clear();
  audio_nodes.push_back(kInternalSpeaker);
  audio_nodes.push_back(headphone);
  ChangeAudioNodes(audio_nodes);

  // Verify the AudioNodesChanged event is fired and one audio device is
  // removed.
  EXPECT_EQ(2, test_observer_->audio_nodes_changed_count());
  cras_audio_handler_->GetAudioDevices(&audio_devices);
  EXPECT_EQ(init_nodes_size, audio_devices.size());

  // Verify the active output device is switched to headphone, and
  // ActiveOutputChanged event is fired.
  EXPECT_EQ(2, test_observer_->active_output_node_changed_count());
  EXPECT_TRUE(cras_audio_handler_->GetActiveOutputDevice(&active_output));
  EXPECT_EQ(kHeadphone.id, active_output.id);
  EXPECT_EQ(kHeadphone.id, cras_audio_handler_->GetActiveOutputNode());
  EXPECT_TRUE(cras_audio_handler_->has_alternative_output());
}

TEST_F(CrasAudioHandlerTest, InitializeWithHDMIOutput) {
  AudioNodeList audio_nodes;
  audio_nodes.push_back(kInternalSpeaker);
  audio_nodes.push_back(kHDMIOutput);
  SetUpCrasAudioHandler(audio_nodes);

  // Verify the audio devices size.
  AudioDeviceList audio_devices;
  cras_audio_handler_->GetAudioDevices(&audio_devices);
  EXPECT_EQ(audio_nodes.size(), audio_devices.size());
  EXPECT_EQ(0, test_observer_->audio_nodes_changed_count());

  // Verify the HDMI device has been selected as the active output.
  EXPECT_EQ(0, test_observer_->active_output_node_changed_count());
  AudioDevice active_output;
  EXPECT_TRUE(cras_audio_handler_->GetActiveOutputDevice(&active_output));
  EXPECT_EQ(kHDMIOutput.id, active_output.id);
  EXPECT_EQ(kHDMIOutput.id, cras_audio_handler_->GetActiveOutputNode());
  EXPECT_TRUE(cras_audio_handler_->has_alternative_output());
}

TEST_F(CrasAudioHandlerTest, ConnectAndDisconnectHDMIOutput) {
  // Initialize with internal speaker.
  AudioNodeList audio_nodes;
  audio_nodes.push_back(kInternalSpeaker);
  SetUpCrasAudioHandler(audio_nodes);
  const size_t init_nodes_size = audio_nodes.size();

  // Verify the audio devices size.
  AudioDeviceList audio_devices;
  cras_audio_handler_->GetAudioDevices(&audio_devices);
  EXPECT_EQ(init_nodes_size, audio_devices.size());
  EXPECT_EQ(0, test_observer_->audio_nodes_changed_count());

  // Verify the internal speaker is selected as the active output initially.
  EXPECT_EQ(0, test_observer_->active_output_node_changed_count());
  AudioDevice active_output;
  EXPECT_TRUE(cras_audio_handler_->GetActiveOutputDevice(&active_output));
  EXPECT_EQ(kInternalSpeaker.id, active_output.id);
  EXPECT_EQ(kInternalSpeaker.id, cras_audio_handler_->GetActiveOutputNode());
  EXPECT_FALSE(cras_audio_handler_->has_alternative_output());

  // Connect to HDMI output.
  audio_nodes.clear();
  AudioNode internal_speaker(kInternalSpeaker);
  internal_speaker.active = true;
  internal_speaker.plugged_time = 80000000;
  audio_nodes.push_back(internal_speaker);
  AudioNode hdmi(kHDMIOutput);
  hdmi.plugged_time = 90000000;
  audio_nodes.push_back(hdmi);
  ChangeAudioNodes(audio_nodes);

  // Verify the AudioNodesChanged event is fired and new audio device is added.
  EXPECT_EQ(1, test_observer_->audio_nodes_changed_count());
  cras_audio_handler_->GetAudioDevices(&audio_devices);
  EXPECT_EQ(init_nodes_size + 1, audio_devices.size());

  // Verify the active output device is switched to hdmi output, and
  // ActiveOutputChanged event is fired.
  EXPECT_EQ(1, test_observer_->active_output_node_changed_count());
  EXPECT_TRUE(cras_audio_handler_->GetActiveOutputDevice(&active_output));
  EXPECT_EQ(kHDMIOutput.id, active_output.id);
  EXPECT_EQ(kHDMIOutput.id, cras_audio_handler_->GetActiveOutputNode());
  EXPECT_TRUE(cras_audio_handler_->has_alternative_output());

  // Disconnect hdmi headset.
  audio_nodes.clear();
  audio_nodes.push_back(kInternalSpeaker);
  ChangeAudioNodes(audio_nodes);

  // Verify the AudioNodesChanged event is fired and one audio device is
  // removed.
  EXPECT_EQ(2, test_observer_->audio_nodes_changed_count());
  cras_audio_handler_->GetAudioDevices(&audio_devices);
  EXPECT_EQ(init_nodes_size, audio_devices.size());

  // Verify the active output device is switched to internal speaker, and
  // ActiveOutputChanged event is fired.
  EXPECT_EQ(2, test_observer_->active_output_node_changed_count());
  EXPECT_TRUE(cras_audio_handler_->GetActiveOutputDevice(&active_output));
  EXPECT_EQ(kInternalSpeaker.id, active_output.id);
  EXPECT_EQ(kInternalSpeaker.id, cras_audio_handler_->GetActiveOutputNode());
  EXPECT_FALSE(cras_audio_handler_->has_alternative_output());
}

TEST_F(CrasAudioHandlerTest, HandleHeadphoneAndHDMIOutput) {
  // Initialize with internal speaker, headphone and HDMI output.
  AudioNodeList audio_nodes;
  audio_nodes.push_back(kInternalSpeaker);
  audio_nodes.push_back(kHeadphone);
  audio_nodes.push_back(kHDMIOutput);
  SetUpCrasAudioHandler(audio_nodes);
  const size_t init_nodes_size = audio_nodes.size();

  // Verify the audio devices size.
  AudioDeviceList audio_devices;
  cras_audio_handler_->GetAudioDevices(&audio_devices);
  EXPECT_EQ(init_nodes_size, audio_devices.size());
  EXPECT_EQ(0, test_observer_->audio_nodes_changed_count());

  // Verify the headphone is selected as the active output initially.
  EXPECT_EQ(0, test_observer_->active_output_node_changed_count());
  AudioDevice active_output;
  EXPECT_TRUE(cras_audio_handler_->GetActiveOutputDevice(&active_output));
  EXPECT_EQ(kHeadphone.id, active_output.id);
  EXPECT_EQ(kHeadphone.id, cras_audio_handler_->GetActiveOutputNode());
  EXPECT_TRUE(cras_audio_handler_->has_alternative_output());

  // Disconnect HDMI output.
  audio_nodes.clear();
  audio_nodes.push_back(kInternalSpeaker);
  audio_nodes.push_back(kHDMIOutput);
  ChangeAudioNodes(audio_nodes);

  // Verify the AudioNodesChanged event is fired and one audio device is
  // removed.
  EXPECT_EQ(1, test_observer_->audio_nodes_changed_count());
  cras_audio_handler_->GetAudioDevices(&audio_devices);
  EXPECT_EQ(init_nodes_size - 1, audio_devices.size());

  // Verify the active output device is switched to HDMI output, and
  // ActiveOutputChanged event is fired.
  EXPECT_EQ(1, test_observer_->active_output_node_changed_count());
  EXPECT_TRUE(cras_audio_handler_->GetActiveOutputDevice(&active_output));
  EXPECT_EQ(kHDMIOutput.id, active_output.id);
  EXPECT_EQ(kHDMIOutput.id, cras_audio_handler_->GetActiveOutputNode());
  EXPECT_TRUE(cras_audio_handler_->has_alternative_output());
}

TEST_F(CrasAudioHandlerTest, InitializeWithUSBHeadphone) {
  AudioNodeList audio_nodes;
  audio_nodes.push_back(kInternalSpeaker);
  audio_nodes.push_back(kUSBHeadphone1);
  SetUpCrasAudioHandler(audio_nodes);

  // Verify the audio devices size.
  AudioDeviceList audio_devices;
  cras_audio_handler_->GetAudioDevices(&audio_devices);
  EXPECT_EQ(audio_nodes.size(), audio_devices.size());
  EXPECT_EQ(0, test_observer_->audio_nodes_changed_count());

  // Verify the usb headphone has been selected as the active output.
  EXPECT_EQ(0, test_observer_->active_output_node_changed_count());
  AudioDevice active_output;
  EXPECT_TRUE(cras_audio_handler_->GetActiveOutputDevice(&active_output));
  EXPECT_EQ(kUSBHeadphone1.id, active_output.id);
  EXPECT_EQ(kUSBHeadphone1.id, cras_audio_handler_->GetActiveOutputNode());
  EXPECT_TRUE(cras_audio_handler_->has_alternative_output());
}

TEST_F(CrasAudioHandlerTest, PlugAndUnplugUSBHeadphone) {
  // Initialize with internal speaker.
  AudioNodeList audio_nodes;
  audio_nodes.push_back(kInternalSpeaker);
  SetUpCrasAudioHandler(audio_nodes);
  const size_t init_nodes_size = audio_nodes.size();

  // Verify the audio devices size.
  AudioDeviceList audio_devices;
  cras_audio_handler_->GetAudioDevices(&audio_devices);
  EXPECT_EQ(init_nodes_size, audio_devices.size());
  EXPECT_EQ(0, test_observer_->audio_nodes_changed_count());

  // Verify the internal speaker is selected as the active output initially.
  EXPECT_EQ(0, test_observer_->active_output_node_changed_count());
  AudioDevice active_output;
  EXPECT_TRUE(cras_audio_handler_->GetActiveOutputDevice(&active_output));
  EXPECT_EQ(kInternalSpeaker.id, active_output.id);
  EXPECT_EQ(kInternalSpeaker.id, cras_audio_handler_->GetActiveOutputNode());
  EXPECT_FALSE(cras_audio_handler_->has_alternative_output());

  // Plug in usb headphone
  audio_nodes.clear();
  AudioNode internal_speaker(kInternalSpeaker);
  internal_speaker.active = true;
  internal_speaker.plugged_time = 80000000;
  audio_nodes.push_back(internal_speaker);
  AudioNode usb_headphone(kUSBHeadphone1);
  usb_headphone.plugged_time = 90000000;
  audio_nodes.push_back(usb_headphone);
  ChangeAudioNodes(audio_nodes);

  // Verify the AudioNodesChanged event is fired and new audio device is added.
  EXPECT_EQ(1, test_observer_->audio_nodes_changed_count());
  cras_audio_handler_->GetAudioDevices(&audio_devices);
  EXPECT_EQ(init_nodes_size + 1, audio_devices.size());

  // Verify the active output device is switched to usb headphone, and
  // ActiveOutputChanged event is fired.
  EXPECT_EQ(1, test_observer_->active_output_node_changed_count());
  EXPECT_TRUE(cras_audio_handler_->GetActiveOutputDevice(&active_output));
  EXPECT_EQ(kUSBHeadphone1.id, active_output.id);
  EXPECT_EQ(kUSBHeadphone1.id, cras_audio_handler_->GetActiveOutputNode());
  EXPECT_TRUE(cras_audio_handler_->has_alternative_output());

  // Unplug usb headphone.
  audio_nodes.clear();
  audio_nodes.push_back(kInternalSpeaker);
  ChangeAudioNodes(audio_nodes);

  // Verify the AudioNodesChanged event is fired and one audio device is
  // removed.
  EXPECT_EQ(2, test_observer_->audio_nodes_changed_count());
  cras_audio_handler_->GetAudioDevices(&audio_devices);
  EXPECT_EQ(init_nodes_size, audio_devices.size());

  // Verify the active output device is switched to internal speaker, and
  // ActiveOutputChanged event is fired.
  EXPECT_EQ(2, test_observer_->active_output_node_changed_count());
  EXPECT_TRUE(cras_audio_handler_->GetActiveOutputDevice(&active_output));
  EXPECT_EQ(kInternalSpeaker.id, active_output.id);
  EXPECT_EQ(kInternalSpeaker.id, cras_audio_handler_->GetActiveOutputNode());
  EXPECT_FALSE(cras_audio_handler_->has_alternative_output());
}

TEST_F(CrasAudioHandlerTest, HandleMultipleUSBHeadphones) {
  // Initialize with internal speaker and one usb headphone.
  AudioNodeList audio_nodes;
  audio_nodes.push_back(kInternalSpeaker);
  audio_nodes.push_back(kUSBHeadphone1);
  SetUpCrasAudioHandler(audio_nodes);
  const size_t init_nodes_size = audio_nodes.size();

  // Verify the audio devices size.
  AudioDeviceList audio_devices;
  cras_audio_handler_->GetAudioDevices(&audio_devices);
  EXPECT_EQ(init_nodes_size, audio_devices.size());
  EXPECT_EQ(0, test_observer_->audio_nodes_changed_count());

  // Verify the usb headphone is selected as the active output initially.
  EXPECT_EQ(0, test_observer_->active_output_node_changed_count());
  AudioDevice active_output;
  EXPECT_TRUE(cras_audio_handler_->GetActiveOutputDevice(&active_output));
  EXPECT_EQ(kUSBHeadphone1.id, active_output.id);
  EXPECT_EQ(kUSBHeadphone1.id, cras_audio_handler_->GetActiveOutputNode());
  EXPECT_TRUE(cras_audio_handler_->has_alternative_output());

  // Plug in another usb headphone.
  audio_nodes.clear();
  audio_nodes.push_back(kInternalSpeaker);
  AudioNode usb_headphone_1(kUSBHeadphone1);
  usb_headphone_1.active = true;
  usb_headphone_1.plugged_time = 80000000;
  audio_nodes.push_back(usb_headphone_1);
  AudioNode usb_headphone_2(kUSBHeadphone2);
  usb_headphone_2.plugged_time = 90000000;
  audio_nodes.push_back(usb_headphone_2);
  ChangeAudioNodes(audio_nodes);

  // Verify the AudioNodesChanged event is fired and new audio device is added.
  EXPECT_EQ(1, test_observer_->audio_nodes_changed_count());
  cras_audio_handler_->GetAudioDevices(&audio_devices);
  EXPECT_EQ(init_nodes_size + 1, audio_devices.size());

  // Verify the active output device is switched to the 2nd usb headphone, which
  // is plugged later, and ActiveOutputChanged event is fired.
  EXPECT_EQ(1, test_observer_->active_output_node_changed_count());
  EXPECT_TRUE(cras_audio_handler_->GetActiveOutputDevice(&active_output));
  EXPECT_EQ(kUSBHeadphone2.id, active_output.id);
  EXPECT_EQ(kUSBHeadphone2.id, cras_audio_handler_->GetActiveOutputNode());
  EXPECT_TRUE(cras_audio_handler_->has_alternative_output());

  // Unplug the 2nd usb headphone.
  audio_nodes.clear();
  audio_nodes.push_back(kInternalSpeaker);
  audio_nodes.push_back(kUSBHeadphone1);
  ChangeAudioNodes(audio_nodes);

  // Verify the AudioNodesChanged event is fired and one audio device is
  // removed.
  EXPECT_EQ(2, test_observer_->audio_nodes_changed_count());
  cras_audio_handler_->GetAudioDevices(&audio_devices);
  EXPECT_EQ(init_nodes_size, audio_devices.size());

  // Verify the active output device is switched to the first usb headphone, and
  // ActiveOutputChanged event is fired.
  EXPECT_EQ(2, test_observer_->active_output_node_changed_count());
  EXPECT_TRUE(cras_audio_handler_->GetActiveOutputDevice(&active_output));
  EXPECT_EQ(kUSBHeadphone1.id, active_output.id);
  EXPECT_EQ(kUSBHeadphone1.id, cras_audio_handler_->GetActiveOutputNode());
  EXPECT_TRUE(cras_audio_handler_->has_alternative_output());
}

TEST_F(CrasAudioHandlerTest, PlugUSBMic) {
  // Set up initial audio devices, only with internal mic.
  AudioNodeList audio_nodes;
  audio_nodes.push_back(kInternalMic);
  SetUpCrasAudioHandler(audio_nodes);
  const size_t init_nodes_size = audio_nodes.size();

  // Verify the audio devices size.
  AudioDeviceList audio_devices;
  cras_audio_handler_->GetAudioDevices(&audio_devices);
  EXPECT_EQ(init_nodes_size, audio_devices.size());
  EXPECT_EQ(0, test_observer_->audio_nodes_changed_count());

  // Verify the internal mic is selected as the active output.
  EXPECT_EQ(0, test_observer_->active_input_node_changed_count());
  EXPECT_EQ(kInternalMic.id, cras_audio_handler_->GetActiveInputNode());
  EXPECT_FALSE(cras_audio_handler_->has_alternative_input());

  // Plug the USB Mic.
  audio_nodes.push_back(kUSBMic);
  ChangeAudioNodes(audio_nodes);

  // Verify the AudioNodesChanged event is fired and new audio device is added.
  EXPECT_EQ(1, test_observer_->audio_nodes_changed_count());
  cras_audio_handler_->GetAudioDevices(&audio_devices);
  EXPECT_EQ(init_nodes_size + 1, audio_devices.size());

  // Verify the active input device is switched to USB mic and
  // and ActiveInputChanged event is fired.
  EXPECT_EQ(1, test_observer_->active_input_node_changed_count());
  EXPECT_EQ(kUSBMicId, cras_audio_handler_->GetActiveInputNode());
  EXPECT_TRUE(cras_audio_handler_->has_alternative_input());
}

TEST_F(CrasAudioHandlerTest, UnplugUSBMic) {
  // Set up initial audio devices, with internal mic and USB Mic.
  AudioNodeList audio_nodes;
  audio_nodes.push_back(kInternalMic);
  audio_nodes.push_back(kUSBMic);
  SetUpCrasAudioHandler(audio_nodes);
  const size_t init_nodes_size = audio_nodes.size();

  // Verify the audio devices size.
  EXPECT_EQ(0, test_observer_->audio_nodes_changed_count());
  AudioDeviceList audio_devices;
  cras_audio_handler_->GetAudioDevices(&audio_devices);
  EXPECT_EQ(init_nodes_size, audio_devices.size());

  // Verify the USB mic is selected as the active output.
  EXPECT_EQ(0, test_observer_->active_input_node_changed_count());
  EXPECT_EQ(kUSBMicId, cras_audio_handler_->GetActiveInputNode());
  EXPECT_TRUE(cras_audio_handler_->has_alternative_input());

  // Unplug the USB Mic.
  audio_nodes.clear();
  audio_nodes.push_back(kInternalMic);
  ChangeAudioNodes(audio_nodes);

  // Verify the AudioNodesChanged event is fired, and one audio device is
  // removed.
  EXPECT_EQ(1, test_observer_->audio_nodes_changed_count());
  cras_audio_handler_->GetAudioDevices(&audio_devices);
  EXPECT_EQ(init_nodes_size - 1, audio_devices.size());

  // Verify the active input device is switched to internal mic, and
  // and ActiveInputChanged event is fired.
  EXPECT_EQ(1, test_observer_->active_input_node_changed_count());
  EXPECT_EQ(kInternalMic.id, cras_audio_handler_->GetActiveInputNode());
  EXPECT_FALSE(cras_audio_handler_->has_alternative_input());
}

TEST_F(CrasAudioHandlerTest, SetOutputMute) {
  AudioNodeList audio_nodes;
  audio_nodes.push_back(kInternalSpeaker);
  SetUpCrasAudioHandler(audio_nodes);
  EXPECT_EQ(0, test_observer_->output_mute_changed_count());

  // Mute the device.
  cras_audio_handler_->SetOutputMute(true);

  // Verify the output is muted, OnOutputMuteChanged event is fired,
  // and mute value is saved in the preferences.
  EXPECT_TRUE(cras_audio_handler_->IsOutputMuted());
  EXPECT_EQ(1, test_observer_->output_mute_changed_count());
  AudioDevice speaker(kInternalSpeaker);
  EXPECT_TRUE(audio_pref_handler_->GetMuteValue(speaker));

  // Unmute the device.
  cras_audio_handler_->SetOutputMute(false);

  // Verify the output is unmuted, OnOutputMuteChanged event is fired,
  // and mute value is saved in the preferences.
  EXPECT_FALSE(cras_audio_handler_->IsOutputMuted());
  EXPECT_EQ(2, test_observer_->output_mute_changed_count());
  EXPECT_FALSE(audio_pref_handler_->GetMuteValue(speaker));
}

TEST_F(CrasAudioHandlerTest, SetInputMute) {
  AudioNodeList audio_nodes;
  audio_nodes.push_back(kInternalMic);
  SetUpCrasAudioHandler(audio_nodes);
  EXPECT_EQ(0, test_observer_->input_mute_changed_count());

  // Mute the device.
  cras_audio_handler_->SetInputMute(true);

  // Verify the input is muted, OnInputMuteChanged event is fired,
  // and mute value is saved in the preferences.
  EXPECT_TRUE(cras_audio_handler_->IsInputMuted());
  EXPECT_EQ(1, test_observer_->input_mute_changed_count());
  AudioDevice internal_mic(kInternalMic);
  EXPECT_TRUE(audio_pref_handler_->GetMuteValue(internal_mic));

  // Unmute the device.
  cras_audio_handler_->SetInputMute(false);

  // Verify the input is unmuted, OnInputMuteChanged event is fired,
  // and mute value is saved in the preferences.
  EXPECT_FALSE(cras_audio_handler_->IsInputMuted());
  EXPECT_EQ(2, test_observer_->input_mute_changed_count());
  EXPECT_FALSE(audio_pref_handler_->GetMuteValue(internal_mic));
}

TEST_F(CrasAudioHandlerTest, SetOutputVolumePercent) {
  AudioNodeList audio_nodes;
  audio_nodes.push_back(kInternalSpeaker);
  SetUpCrasAudioHandler(audio_nodes);
  EXPECT_EQ(0, test_observer_->output_volume_changed_count());

  cras_audio_handler_->SetOutputVolumePercent(60);

  // Verify the output volume is changed to the designated value,
  // OnOutputVolumeChanged event is fired, and the device volume value
  // is saved the preferences.
  const int kVolume = 60;
  EXPECT_EQ(kVolume, cras_audio_handler_->GetOutputVolumePercent());
  EXPECT_EQ(1, test_observer_->output_volume_changed_count());
  AudioDevice device;
  EXPECT_TRUE(cras_audio_handler_->GetActiveOutputDevice(&device));
  EXPECT_EQ(device.id, kInternalSpeaker.id);
  EXPECT_EQ(kVolume, audio_pref_handler_->GetVolumeGainValue(device));
}

TEST_F(CrasAudioHandlerTest, SetInputGainPercent) {
  AudioNodeList audio_nodes;
  audio_nodes.push_back(kInternalMic);
  SetUpCrasAudioHandler(audio_nodes);
  EXPECT_EQ(0, test_observer_->input_gain_changed_count());

  cras_audio_handler_->SetInputGainPercent(60);

  // Verify the input gain changed to the designated value,
  // OnInputGainChanged event is fired, and the device gain value
  // is saved in the preferences.
  const int kGain = 60;
  EXPECT_EQ(kGain, cras_audio_handler_->GetInputGainPercent());
  EXPECT_EQ(1, test_observer_->input_gain_changed_count());
  AudioDevice internal_mic(kInternalMic);
  EXPECT_EQ(kGain, audio_pref_handler_->GetVolumeGainValue(internal_mic));
}

TEST_F(CrasAudioHandlerTest, SetMuteForDevice) {
  AudioNodeList audio_nodes;
  audio_nodes.push_back(kInternalSpeaker);
  audio_nodes.push_back(kHeadphone);
  audio_nodes.push_back(kInternalMic);
  audio_nodes.push_back(kUSBMic);
  SetUpCrasAudioHandler(audio_nodes);

  // Mute the active output device.
  EXPECT_EQ(kHeadphone.id, cras_audio_handler_->GetActiveOutputNode());
  cras_audio_handler_->SetMuteForDevice(kHeadphone.id, true);

  // Verify the headphone is muted and mute value is saved in the preferences.
  EXPECT_TRUE(cras_audio_handler_->IsOutputMutedForDevice(kHeadphone.id));
  AudioDevice headphone(kHeadphone);
  EXPECT_TRUE(audio_pref_handler_->GetMuteValue(headphone));

  // Mute the non-active output device.
  cras_audio_handler_->SetMuteForDevice(kInternalSpeaker.id, true);

  // Verify the internal speaker is muted and mute value is saved in the
  // preferences.
  EXPECT_TRUE(cras_audio_handler_->IsOutputMutedForDevice(kInternalSpeaker.id));
  AudioDevice internal_speaker(kInternalSpeaker);
  EXPECT_TRUE(audio_pref_handler_->GetMuteValue(internal_speaker));

  // Mute the active input device.
  EXPECT_EQ(kUSBMic.id, cras_audio_handler_->GetActiveInputNode());
  cras_audio_handler_->SetMuteForDevice(kUSBMic.id, true);

  // Verify the USB Mic is muted and mute state is saved in the preferences.
  EXPECT_TRUE(cras_audio_handler_->IsOutputMutedForDevice(kUSBMic.id));
  AudioDevice usb_mic(kUSBMic);
  EXPECT_TRUE(audio_pref_handler_->GetMuteValue(usb_mic));

  // Mute the non-active input device.
  cras_audio_handler_->SetMuteForDevice(kInternalMic.id, true);

  // Verify the internal mic is muted and mute value is saved in the
  // preferences.
  EXPECT_TRUE(cras_audio_handler_->IsOutputMutedForDevice(kInternalMic.id));
  AudioDevice internal_mic(kInternalMic);
  EXPECT_TRUE(audio_pref_handler_->GetMuteValue(internal_mic));
}

TEST_F(CrasAudioHandlerTest, SetVolumeGainPercentForDevice) {
  AudioNodeList audio_nodes;
  audio_nodes.push_back(kInternalSpeaker);
  audio_nodes.push_back(kHeadphone);
  audio_nodes.push_back(kInternalMic);
  audio_nodes.push_back(kUSBMic);
  SetUpCrasAudioHandler(audio_nodes);

  // Set volume percent for active output device.
  const int kHeadphoneVolume = 30;
  EXPECT_EQ(kHeadphone.id, cras_audio_handler_->GetActiveOutputNode());
  cras_audio_handler_->SetVolumeGainPercentForDevice(kHeadphone.id,
                                                     kHeadphoneVolume);

  // Verify the volume percent of headphone is set, and saved in preferences.
  EXPECT_EQ(kHeadphoneVolume,
            cras_audio_handler_->GetOutputVolumePercentForDevice(
                kHeadphone.id));
  AudioDevice headphone(kHeadphone);
  EXPECT_EQ(kHeadphoneVolume,
            audio_pref_handler_->GetVolumeGainValue(headphone));

  // Set volume percent for non-active output device.
  const int kSpeakerVolume = 60;
  cras_audio_handler_->SetVolumeGainPercentForDevice(kInternalSpeaker.id,
                                                     kSpeakerVolume);

  // Verify the volume percent of speaker is set, and saved in preferences.
  EXPECT_EQ(kSpeakerVolume,
            cras_audio_handler_->GetOutputVolumePercentForDevice(
                kInternalSpeaker.id));
  AudioDevice speaker(kInternalSpeaker);
  EXPECT_EQ(kSpeakerVolume,
            audio_pref_handler_->GetVolumeGainValue(speaker));

  // Set gain percent for active input device.
  const int kUSBMicGain = 30;
  EXPECT_EQ(kUSBMic.id, cras_audio_handler_->GetActiveInputNode());
  cras_audio_handler_->SetVolumeGainPercentForDevice(kUSBMic.id,
                                                     kUSBMicGain);

  // Verify the gain percent of USB mic is set, and saved in preferences.
  EXPECT_EQ(kUSBMicGain,
            cras_audio_handler_->GetOutputVolumePercentForDevice(kUSBMic.id));
  AudioDevice usb_mic(kHeadphone);
  EXPECT_EQ(kUSBMicGain,
            audio_pref_handler_->GetVolumeGainValue(usb_mic));

  // Set gain percent for non-active input device.
  const int kInternalMicGain = 60;
  cras_audio_handler_->SetVolumeGainPercentForDevice(kInternalMic.id,
                                                     kInternalMicGain);

  // Verify the gain percent of internal mic is set, and saved in preferences.
  EXPECT_EQ(kInternalMicGain,
            cras_audio_handler_->GetOutputVolumePercentForDevice(
                kInternalMic.id));
  AudioDevice internal_mic(kInternalMic);
  EXPECT_EQ(kInternalMicGain,
            audio_pref_handler_->GetVolumeGainValue(internal_mic));
}

TEST_F(CrasAudioHandlerTest, HandleOtherDeviceType) {
  const size_t kNumValidAudioDevices = 4;
  AudioNodeList audio_nodes;
  audio_nodes.push_back(kInternalSpeaker);
  audio_nodes.push_back(kOtherTypeOutput);
  audio_nodes.push_back(kInternalMic);
  audio_nodes.push_back(kOtherTypeInput);
  SetUpCrasAudioHandler(audio_nodes);

  // Verify the audio devices size.
  AudioDeviceList audio_devices;
  cras_audio_handler_->GetAudioDevices(&audio_devices);
  EXPECT_EQ(kNumValidAudioDevices, audio_devices.size());

  // Verify the internal speaker has been selected as the active output,
  // and the output device with some randown unknown type is handled gracefully.
  AudioDevice active_output;
  EXPECT_TRUE(cras_audio_handler_->GetActiveOutputDevice(&active_output));
  EXPECT_EQ(kInternalSpeaker.id, active_output.id);
  EXPECT_EQ(kInternalSpeaker.id, cras_audio_handler_->GetActiveOutputNode());
  EXPECT_TRUE(cras_audio_handler_->has_alternative_output());

  // Ensure the internal microphone has been selected as the active input,
  // and the input device with some random unknown type is handled gracefully.
  AudioDevice active_input;
  EXPECT_EQ(kInternalMic.id, cras_audio_handler_->GetActiveInputNode());
  EXPECT_TRUE(cras_audio_handler_->has_alternative_input());
}

}  // namespace chromeos
