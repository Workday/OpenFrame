// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/speech/tts_controller.h"

#include <string>
#include <vector>

#include "base/float_util.h"
#include "base/values.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/speech/extension_api/tts_engine_extension_api.h"
#include "chrome/browser/speech/extension_api/tts_extension_api.h"
#include "chrome/browser/speech/tts_platform.h"
#include "chrome/common/extensions/api/speech/tts_engine_manifest_handler.h"
#include "chrome/common/extensions/extension.h"

namespace {
// A value to be used to indicate that there is no char index available.
const int kInvalidCharIndex = -1;

// Given a language/region code of the form 'fr-FR', returns just the basic
// language portion, e.g. 'fr'.
std::string TrimLanguageCode(std::string lang) {
  if (lang.size() >= 5 && lang[2] == '-')
    return lang.substr(0, 2);
  else
    return lang;
}

}  // namespace

bool IsFinalTtsEventType(TtsEventType event_type) {
  return (event_type == TTS_EVENT_END ||
          event_type == TTS_EVENT_INTERRUPTED ||
          event_type == TTS_EVENT_CANCELLED ||
          event_type == TTS_EVENT_ERROR);
}

//
// UtteranceContinuousParameters
//


UtteranceContinuousParameters::UtteranceContinuousParameters()
    : rate(-1),
      pitch(-1),
      volume(-1) {}


//
// VoiceData
//


VoiceData::VoiceData()
    : gender(TTS_GENDER_NONE),
      native(false) {}

VoiceData::~VoiceData() {}


//
// Utterance
//

// static
int Utterance::next_utterance_id_ = 0;

Utterance::Utterance(Profile* profile)
    : profile_(profile),
      id_(next_utterance_id_++),
      src_id_(-1),
      event_delegate_(NULL),
      can_enqueue_(false),
      char_index_(0),
      finished_(false) {
  options_.reset(new DictionaryValue());
}

Utterance::~Utterance() {
  DCHECK(finished_);
}

void Utterance::OnTtsEvent(TtsEventType event_type,
                           int char_index,
                           const std::string& error_message) {
  if (char_index >= 0)
    char_index_ = char_index;
  if (IsFinalTtsEventType(event_type))
    finished_ = true;

  if (event_delegate_)
    event_delegate_->OnTtsEvent(this, event_type, char_index, error_message);
  if (finished_)
    event_delegate_ = NULL;
}

void Utterance::Finish() {
  finished_ = true;
}

void Utterance::set_options(const Value* options) {
  options_.reset(options->DeepCopy());
}

//
// TtsController
//

// static
TtsController* TtsController::GetInstance() {
  return Singleton<TtsController>::get();
}

TtsController::TtsController()
    : current_utterance_(NULL),
      paused_(false),
      platform_impl_(NULL) {
}

TtsController::~TtsController() {
  if (current_utterance_) {
    current_utterance_->Finish();
    delete current_utterance_;
  }

  // Clear any queued utterances too.
  ClearUtteranceQueue(false);  // Don't sent events.
}

void TtsController::SpeakOrEnqueue(Utterance* utterance) {
  // If we're paused and we get an utterance that can't be queued,
  // flush the queue but stay in the paused state.
  if (paused_ && !utterance->can_enqueue()) {
    Stop();
    paused_ = true;
    return;
  }

  if (paused_ || (IsSpeaking() && utterance->can_enqueue())) {
    utterance_queue_.push(utterance);
  } else {
    Stop();
    SpeakNow(utterance);
  }
}

void TtsController::SpeakNow(Utterance* utterance) {
  // Get all available voices and try to find a matching voice.
  std::vector<VoiceData> voices;
  GetVoices(utterance->profile(), &voices);
  int index = GetMatchingVoice(utterance, voices);

  // Select the matching voice, but if none was found, initialize an
  // empty VoiceData with native = true, which will give the native
  // speech synthesizer a chance to try to synthesize the utterance
  // anyway.
  VoiceData voice;
  if (index >= 0 && index < static_cast<int>(voices.size()))
    voice = voices[index];
  else
    voice.native = true;

  if (!voice.native) {
#if !defined(OS_ANDROID)
    DCHECK(!voice.extension_id.empty());
    current_utterance_ = utterance;
    utterance->set_extension_id(voice.extension_id);
    ExtensionTtsEngineSpeak(utterance, voice);
    bool sends_end_event =
        voice.events.find(TTS_EVENT_END) != voice.events.end();
    if (!sends_end_event) {
      utterance->Finish();
      delete utterance;
      current_utterance_ = NULL;
      SpeakNextUtterance();
    }
#endif
  } else {
    GetPlatformImpl()->clear_error();
    bool success = GetPlatformImpl()->Speak(
        utterance->id(),
        utterance->text(),
        utterance->lang(),
        voice,
        utterance->continuous_parameters());

    // If the native voice wasn't able to process this speech, see if
    // the browser has built-in TTS that isn't loaded yet.
    if (!success &&
        GetPlatformImpl()->LoadBuiltInTtsExtension(utterance->profile())) {
      utterance_queue_.push(utterance);
      return;
    }

    if (!success) {
      utterance->OnTtsEvent(TTS_EVENT_ERROR, kInvalidCharIndex,
                            GetPlatformImpl()->error());
      delete utterance;
      return;
    }
    current_utterance_ = utterance;
  }
}

void TtsController::Stop() {
  paused_ = false;
  if (current_utterance_ && !current_utterance_->extension_id().empty()) {
#if !defined(OS_ANDROID)
    ExtensionTtsEngineStop(current_utterance_);
#endif
  } else {
    GetPlatformImpl()->clear_error();
    GetPlatformImpl()->StopSpeaking();
  }

  if (current_utterance_)
    current_utterance_->OnTtsEvent(TTS_EVENT_INTERRUPTED, kInvalidCharIndex,
                                   std::string());
  FinishCurrentUtterance();
  ClearUtteranceQueue(true);  // Send events.
}

void TtsController::Pause() {
  paused_ = true;
  if (current_utterance_ && !current_utterance_->extension_id().empty()) {
#if !defined(OS_ANDROID)
    ExtensionTtsEnginePause(current_utterance_);
#endif
  } else if (current_utterance_) {
    GetPlatformImpl()->clear_error();
    GetPlatformImpl()->Pause();
  }
}

void TtsController::Resume() {
  paused_ = false;
  if (current_utterance_ && !current_utterance_->extension_id().empty()) {
#if !defined(OS_ANDROID)
    ExtensionTtsEngineResume(current_utterance_);
#endif
  } else if (current_utterance_) {
    GetPlatformImpl()->clear_error();
    GetPlatformImpl()->Resume();
  } else {
    SpeakNextUtterance();
  }
}

void TtsController::OnTtsEvent(int utterance_id,
                                        TtsEventType event_type,
                                        int char_index,
                                        const std::string& error_message) {
  // We may sometimes receive completion callbacks "late", after we've
  // already finished the utterance (for example because another utterance
  // interrupted or we got a call to Stop). This is normal and we can
  // safely just ignore these events.
  if (!current_utterance_ || utterance_id != current_utterance_->id())
    return;

  current_utterance_->OnTtsEvent(event_type, char_index, error_message);
  if (current_utterance_->finished()) {
    FinishCurrentUtterance();
    SpeakNextUtterance();
  }
}

void TtsController::GetVoices(Profile* profile,
                              std::vector<VoiceData>* out_voices) {
#if !defined(OS_ANDROID)
  if (profile)
    GetExtensionVoices(profile, out_voices);
#endif

  TtsPlatformImpl* platform_impl = GetPlatformImpl();
  if (platform_impl && platform_impl->PlatformImplAvailable())
    platform_impl->GetVoices(out_voices);
}

bool TtsController::IsSpeaking() {
  return current_utterance_ != NULL || GetPlatformImpl()->IsSpeaking();
}

void TtsController::FinishCurrentUtterance() {
  if (current_utterance_) {
    if (!current_utterance_->finished())
      current_utterance_->OnTtsEvent(TTS_EVENT_INTERRUPTED, kInvalidCharIndex,
                                     std::string());
    delete current_utterance_;
    current_utterance_ = NULL;
  }
}

void TtsController::SpeakNextUtterance() {
  if (paused_)
    return;

  // Start speaking the next utterance in the queue.  Keep trying in case
  // one fails but there are still more in the queue to try.
  while (!utterance_queue_.empty() && !current_utterance_) {
    Utterance* utterance = utterance_queue_.front();
    utterance_queue_.pop();
    SpeakNow(utterance);
  }
}

void TtsController::RetrySpeakingQueuedUtterances() {
  if (current_utterance_ == NULL && !utterance_queue_.empty())
    SpeakNextUtterance();
}

void TtsController::ClearUtteranceQueue(bool send_events) {
  while (!utterance_queue_.empty()) {
    Utterance* utterance = utterance_queue_.front();
    utterance_queue_.pop();
    if (send_events)
      utterance->OnTtsEvent(TTS_EVENT_CANCELLED, kInvalidCharIndex,
                            std::string());
    else
      utterance->Finish();
    delete utterance;
  }
}

void TtsController::SetPlatformImpl(
    TtsPlatformImpl* platform_impl) {
  platform_impl_ = platform_impl;
}

int TtsController::QueueSize() {
  return static_cast<int>(utterance_queue_.size());
}

TtsPlatformImpl* TtsController::GetPlatformImpl() {
  if (!platform_impl_)
    platform_impl_ = TtsPlatformImpl::GetInstance();
  return platform_impl_;
}

int TtsController::GetMatchingVoice(
    const Utterance* utterance, std::vector<VoiceData>& voices) {
  // Make two passes: the first time, do strict language matching
  // ('fr-FR' does not match 'fr-CA'). The second time, do prefix
  // language matching ('fr-FR' matches 'fr' and 'fr-CA')
  for (int pass = 0; pass < 2; ++pass) {
    for (size_t i = 0; i < voices.size(); ++i) {
      const VoiceData& voice = voices[i];

      if (!utterance->extension_id().empty() &&
          utterance->extension_id() != voice.extension_id) {
        continue;
      }

      if (!voice.name.empty() &&
          !utterance->voice_name().empty() &&
          voice.name != utterance->voice_name()) {
        continue;
      }
      if (!voice.lang.empty() && !utterance->lang().empty()) {
        std::string voice_lang = voice.lang;
        std::string utterance_lang = utterance->lang();
        if (pass == 1) {
          voice_lang = TrimLanguageCode(voice_lang);
          utterance_lang = TrimLanguageCode(utterance_lang);
        }
        if (voice_lang != utterance_lang) {
          continue;
        }
      }
      if (voice.gender != TTS_GENDER_NONE &&
          utterance->gender() != TTS_GENDER_NONE &&
          voice.gender != utterance->gender()) {
        continue;
      }

      if (utterance->required_event_types().size() > 0) {
        bool has_all_required_event_types = true;
        for (std::set<TtsEventType>::const_iterator iter =
                 utterance->required_event_types().begin();
             iter != utterance->required_event_types().end();
             ++iter) {
          if (voice.events.find(*iter) == voice.events.end()) {
            has_all_required_event_types = false;
            break;
          }
        }
        if (!has_all_required_event_types)
          continue;
      }

      return static_cast<int>(i);
    }
  }

  return -1;
}

void TtsController::VoicesChanged() {
  for (std::set<VoicesChangedDelegate*>::iterator iter =
           voices_changed_delegates_.begin();
       iter != voices_changed_delegates_.end(); ++iter) {
    (*iter)->OnVoicesChanged();
  }
}

void TtsController::AddVoicesChangedDelegate(VoicesChangedDelegate* delegate) {
  voices_changed_delegates_.insert(delegate);
}

void TtsController::RemoveVoicesChangedDelegate(
    VoicesChangedDelegate* delegate) {
  voices_changed_delegates_.erase(delegate);
}

