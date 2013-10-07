// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_AUDIO_AUDIO_DEVICES_PREF_HANDLER_H_
#define CHROMEOS_AUDIO_AUDIO_DEVICES_PREF_HANDLER_H_

#include "base/basictypes.h"
#include "base/memory/ref_counted.h"
#include "chromeos/audio/audio_pref_observer.h"
#include "chromeos/chromeos_export.h"

class PrefService;

namespace chromeos {

struct AudioDevice;

// Interface that handles audio preference related work, reads and writes
// audio preferences, and notifies AudioPrefObserver for audio preference
// changes.
class CHROMEOS_EXPORT AudioDevicesPrefHandler
    : public base::RefCountedThreadSafe<AudioDevicesPrefHandler> {
 public:
  // Gets the audio output volume value from prefs for a device. Since we can
  // only have either a gain or a volume for a device (depending on whether it
  // is input or output), we don't really care which value it is.
  virtual double GetVolumeGainValue(const AudioDevice& device) = 0;
  // Sets the audio volume or gain value to prefs for a device.
  virtual void SetVolumeGainValue(const AudioDevice& device, double value) = 0;

  // Reads the audio mute value from prefs for a device.
  virtual bool GetMuteValue(const AudioDevice& device) = 0;
  // Sets the audio mute value to prefs for a device.
  virtual void SetMuteValue(const AudioDevice& device, bool mute_on) = 0;

  // Reads the audio capture allowed value from prefs.
  virtual bool GetAudioCaptureAllowedValue() = 0;
  // Reads the audio output allowed value from prefs.
  virtual bool GetAudioOutputAllowedValue() = 0;

  // Adds an audio preference observer.
  virtual void AddAudioPrefObserver(AudioPrefObserver* observer) = 0;
  // Removes an audio preference observer.
  virtual void RemoveAudioPrefObserver(AudioPrefObserver* observer) = 0;

  // Creates the instance.
  static AudioDevicesPrefHandler* Create(PrefService* local_state);

 protected:
  virtual ~AudioDevicesPrefHandler() {}

 private:
  friend class base::RefCountedThreadSafe<AudioDevicesPrefHandler>;
};

}  // namespace chromeos

#endif  // CHROMEOS_AUDIO_AUDIO_DEVICES_PREF_HANDLER_H_
