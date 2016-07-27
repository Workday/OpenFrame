// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMECAST_PUBLIC_AVSETTINGS_H_
#define CHROMECAST_PUBLIC_AVSETTINGS_H_

#include "task_runner.h"

namespace chromecast {

// Pure abstract interface to get and set media-related information. Each
// platform must provide its own implementation.
// All functions except constructor and destructor are called in one thread.
// All delegate functions can be called by platform implementation on any
// threads, for example, created by platform implementation internally.
class AvSettings {
 public:
  // Defines whether or not the cast receiver is the current active source of
  // the screen. If the device is connected to HDMI sinks, it may be unknown.
  enum ActiveState {
    UNKNOWN,
    STANDBY,   // Screen is off
    INACTIVE,  // Screen is on, but cast receiver is not active
    ACTIVE,    // Screen is on and cast receiver is active
  };

  // Audio codec supported by the device (or HDMI sink).
  enum AudioCodec {
    AC3 = 1 << 0,
    DTS = 1 << 1,
    DTS_HD = 1 << 2,
    EAC3 = 1 << 3,
    LPCM = 1 << 4,
  };

  enum Event {
    // This event shall be fired whenever the active state is changed including
    // when the screen turned on, when the cast receiver (or the device where
    // cast receiver is running on) became the active input source, or after a
    // call to TurnActive() or TurnStandby().
    // WakeSystem() may change the active state depending on implementation.
    // On this event, GetActiveState() will be called on the thread where
    // Initialize() was called.
    ACTIVE_STATE_CHANGED = 0,

    // This event shall be fired whenever the system volume level or muted state
    // are changed including when user changed volume via a remote controller,
    // or after a call to SetAudioVolume() or SetAudioMuted().
    // On this event, GetAudioVolume() and IsAudioMuted() will be called on
    // the thread where Initialize() was called.
    AUDIO_VOLUME_CHANGED = 1,

    // This event shall be fired whenever the audio codecs supported by the
    // device (or HDMI sinks connected to the device) are changed.
    // On this event, GetAudioCodecsSupported() and GetMaxAudioChannels() will
    // be called on the thread where Initialize() was called.
    AUDIO_CODECS_SUPPORTED_CHANGED = 2,

    // This event shall be fired whenever the screen information of the device
    // (or HDMI sinks connected to the device) are changed including screen
    // resolution.
    // On this event, GetScreenResolution() will be called on the thread where
    // Initialize() was called.
    SCREEN_INFO_CHANGED = 3,

    // This event should be fired when the device is connected to HDMI sinks.
    HDMI_CONNECTED = 100,

    // This event should be fired when the device is disconnected to HDMI sinks.
    HDMI_DISCONNECTED = 101,
  };

  // Delegate to inform the caller events. As a subclass of TaskRunner,
  // AvSettings implementation can post tasks to the thread where Initialize()
  // was called.
  class Delegate : public TaskRunner {
   public:
    // This may be invoked to posts a task to the thread where Initialize() was
    // called.
    bool PostTask(Task* task, uint64_t delay_ms) override = 0;

    // This must be invoked to fire an event when one of the conditions
    // described above (Event) happens.
    virtual void OnMediaEvent(Event event) = 0;

    // This should be invoked when a key is pressed.
    // |key_code| is a CEC code defined in User Control Codes table of the CEC
    // specification (CEC Table 30 in the HDMI 1.4a specification).
    virtual void OnKeyPressed(int key_code) = 0;

   protected:
    ~Delegate() override {}
  };

  virtual ~AvSettings() {}

  // Initializes avsettings and starts delivering events to |delegate|.
  // |delegate| must not be null.
  virtual void Initialize(Delegate* delegate) = 0;

  // Finalizes avsettings. It must assume |delegate| passed to Initialize() is
  // invalid after this call and stop delivering events.
  virtual void Finalize() = 0;

  // Returns current active state.
  virtual ActiveState GetActiveState() = 0;

  // Turns the screen on and sets the active input to the cast receiver.
  // If successful, it must return true and fire ACTIVE_STATE_CHANGED.
  virtual bool TurnActive() = 0;

  // Turns the screen off (or stand-by). If the device is connecting to HDMI
  // sinks, broadcasts a CEC standby message on the HDMI control bus to put all
  // sink devices (TV, AVR) into a standby state.
  // If successful, it must return true and fire ACTIVE_STATE_CHANGED.
  virtual bool TurnStandby() = 0;

  // Requests the system where cast receiver is running on to be kept awake for
  // |time_ms|. If the system is already being kept awake, the period should be
  // extended from |time_ms| in the future.
  // It will be called when cast senders discover the cast receiver while the
  // system is in a stand-by mode (or a deeper sleeping/dormant mode depending
  // on the system). To respond to cast senders' requests, cast receiver needs
  // the system awake for given amount of time. The system should not turn
  // screen on.
  // Returns true if successful.
  virtual bool KeepSystemAwake(int time_ms) = 0;

  // Whether or not the device is a master volume system, i.e the system volume
  // is changed, but not attenuated. For example, normal TVs, devices of CEC
  // audio controls, and audio devices are master volume systems.
  // The counter examples are Chromecast (which doesn't do CEC audio controls)
  // and Nexus Player (which has volume fixed).
  virtual bool IsMasterVolumeDevice() = 0;

  // Returns the current volume level, which must be from 0.0 (inclusive) to
  // 1.0 (inclusive).
  virtual float GetAudioVolume() = 0;

  // Sets new volume level of the device (or HDMI sinks). |level| is from 0.0
  // (inclusive) to 1.0 (inclusive).
  // If successful and the level has changed, it must return true and fire
  // AUDIO_VOLUME_CHANGED.
  virtual bool SetAudioVolume(float level) = 0;

  // Whether or not the device (or HDMI sinks) is muted.
  virtual bool IsAudioMuted() = 0;

  // Sets the device (or HDMI sinks) muted.
  // If successful and the muted state has changed, it must return true and fire
  // AUDIO_VOLUME_CHANGED.
  virtual bool SetAudioMuted(bool muted) = 0;

  // Gets audio codecs supported by the device (or HDMI sinks).
  // The result is an integer of OR'ed AudioCodec values.
  virtual int GetAudioCodecsSupported() = 0;

  // Gets maximum number of channels for given audio codec, |codec|.
  virtual int GetMaxAudioChannels(AudioCodec codec) = 0;

  // Retrieves the resolution of screen of the device (or HDMI sinks).
  // Returns true if it gets resolution successfully.
  virtual bool GetScreenResolution(int* width, int* height) = 0;
};

}  // namespace chromecast

#endif  // CHROMECAST_PUBLIC_AVSETTINGS_H_
