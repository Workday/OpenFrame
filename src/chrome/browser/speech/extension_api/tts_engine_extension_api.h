// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SPEECH_EXTENSION_API_TTS_ENGINE_EXTENSION_API_H_
#define CHROME_BROWSER_SPEECH_EXTENSION_API_TTS_ENGINE_EXTENSION_API_H_

#include <vector>

#include "base/memory/singleton.h"
#include "chrome/browser/extensions/extension_function.h"
#include "chrome/browser/speech/tts_controller.h"

class Utterance;

namespace base {
class ListValue;
}

namespace extensions {
class Extension;
}

namespace tts_engine_events {
extern const char kOnSpeak[];
extern const char kOnStop[];
extern const char kOnPause[];
extern const char kOnResume[];
}

// Return a list of all available voices registered by extensions.
void GetExtensionVoices(Profile* profile, std::vector<VoiceData>* out_voices);

// Find the first extension with a tts_voices in its
// manifest that matches the speech parameters of this utterance.
// If found, store a pointer to the extension in |matching_extension| and
// the index of the voice within the extension in |voice_index| and
// return true.
bool GetMatchingExtensionVoice(Utterance* utterance,
                               const extensions::Extension** matching_extension,
                               size_t* voice_index);

// Speak the given utterance by sending an event to the given TTS engine
// extension voice.
void ExtensionTtsEngineSpeak(Utterance* utterance,
                             const VoiceData& voice);

// Stop speaking the given utterance by sending an event to the extension
// associated with this utterance.
void ExtensionTtsEngineStop(Utterance* utterance);

// Pause in the middle of speaking this utterance.
void ExtensionTtsEnginePause(Utterance* utterance);

// Resume speaking this utterance.
void ExtensionTtsEngineResume(Utterance* utterance);

// Hidden/internal extension function used to allow TTS engine extensions
// to send events back to the client that's calling tts.speak().
class ExtensionTtsEngineSendTtsEventFunction : public SyncExtensionFunction {
 private:
  virtual ~ExtensionTtsEngineSendTtsEventFunction() {}
  virtual bool RunImpl() OVERRIDE;
  DECLARE_EXTENSION_FUNCTION("ttsEngine.sendTtsEvent", TTSENGINE_SENDTTSEVENT)
};

#endif  // CHROME_BROWSER_SPEECH_EXTENSION_API_TTS_ENGINE_EXTENSION_API_H_
