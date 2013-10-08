// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SPEECH_TTS_CONTROLLER_H_
#define CHROME_BROWSER_SPEECH_TTS_CONTROLLER_H_

#include <queue>
#include <set>
#include <string>
#include <vector>

#include "base/memory/scoped_ptr.h"
#include "base/memory/singleton.h"
#include "url/gurl.h"

class Utterance;
class TtsPlatformImpl;
class Profile;

namespace base {
class Value;
}

// Events sent back from the TTS engine indicating the progress.
enum TtsEventType {
  TTS_EVENT_START,
  TTS_EVENT_END,
  TTS_EVENT_WORD,
  TTS_EVENT_SENTENCE,
  TTS_EVENT_MARKER,
  TTS_EVENT_INTERRUPTED,
  TTS_EVENT_CANCELLED,
  TTS_EVENT_ERROR,
  TTS_EVENT_PAUSE,
  TTS_EVENT_RESUME
};

enum TtsGenderType {
  TTS_GENDER_NONE,
  TTS_GENDER_MALE,
  TTS_GENDER_FEMALE
};

// Returns true if this event type is one that indicates an utterance
// is finished and can be destroyed.
bool IsFinalTtsEventType(TtsEventType event_type);

// The continuous parameters that apply to a given utterance.
struct UtteranceContinuousParameters {
  UtteranceContinuousParameters();

  double rate;
  double pitch;
  double volume;
};

// Information about one voice.
struct VoiceData {
  VoiceData();
  ~VoiceData();

  std::string name;
  std::string lang;
  TtsGenderType gender;
  std::string extension_id;
  std::set<TtsEventType> events;

  // If true, this is implemented by this platform's subclass of
  // TtsPlatformImpl. If false, this is implemented by an extension.
  bool native;
  std::string native_voice_identifier;
};

// Class that wants to receive events on utterances.
class UtteranceEventDelegate {
 public:
  virtual ~UtteranceEventDelegate() {}
  virtual void OnTtsEvent(Utterance* utterance,
                          TtsEventType event_type,
                          int char_index,
                          const std::string& error_message) = 0;
};

// Class that wants to be notified when the set of
// voices has changed.
class VoicesChangedDelegate {
 public:
  virtual ~VoicesChangedDelegate() {}
  virtual void OnVoicesChanged() = 0;
};

// One speech utterance.
class Utterance {
 public:
  // Construct an utterance given a profile and a completion task to call
  // when the utterance is done speaking. Before speaking this utterance,
  // its other parameters like text, rate, pitch, etc. should all be set.
  explicit Utterance(Profile* profile);
  ~Utterance();

  // Sends an event to the delegate. If the event type is TTS_EVENT_END
  // or TTS_EVENT_ERROR, deletes the utterance. If |char_index| is -1,
  // uses the last good value.
  void OnTtsEvent(TtsEventType event_type,
                  int char_index,
                  const std::string& error_message);

  // Finish an utterance without sending an event to the delegate.
  void Finish();

  // Getters and setters for the text to speak and other speech options.
  void set_text(const std::string& text) { text_ = text; }
  const std::string& text() const { return text_; }

  void set_options(const base::Value* options);
  const base::Value* options() const { return options_.get(); }

  void set_src_extension_id(const std::string& src_extension_id) {
    src_extension_id_ = src_extension_id;
  }
  const std::string& src_extension_id() { return src_extension_id_; }

  void set_src_id(int src_id) { src_id_ = src_id; }
  int src_id() { return src_id_; }

  void set_src_url(const GURL& src_url) { src_url_ = src_url; }
  const GURL& src_url() { return src_url_; }

  void set_voice_name(const std::string& voice_name) {
    voice_name_ = voice_name;
  }
  const std::string& voice_name() const { return voice_name_; }

  void set_lang(const std::string& lang) {
    lang_ = lang;
  }
  const std::string& lang() const { return lang_; }

  void set_gender(TtsGenderType gender) {
    gender_ = gender;
  }
  TtsGenderType gender() const { return gender_; }

  void set_continuous_parameters(const UtteranceContinuousParameters& params) {
    continuous_parameters_ = params;
  }
  const UtteranceContinuousParameters& continuous_parameters() {
    return continuous_parameters_;
  }

  void set_can_enqueue(bool can_enqueue) { can_enqueue_ = can_enqueue; }
  bool can_enqueue() const { return can_enqueue_; }

  void set_required_event_types(const std::set<TtsEventType>& types) {
    required_event_types_ = types;
  }
  const std::set<TtsEventType>& required_event_types() const {
    return required_event_types_;
  }

  void set_desired_event_types(const std::set<TtsEventType>& types) {
    desired_event_types_ = types;
  }
  const std::set<TtsEventType>& desired_event_types() const {
    return desired_event_types_;
  }

  const std::string& extension_id() const { return extension_id_; }
  void set_extension_id(const std::string& extension_id) {
    extension_id_ = extension_id;
  }

  UtteranceEventDelegate* event_delegate() const { return event_delegate_; }
  void set_event_delegate(UtteranceEventDelegate* event_delegate) {
    event_delegate_ = event_delegate;
  }

  // Getters and setters for internal state.
  Profile* profile() const { return profile_; }
  int id() const { return id_; }
  bool finished() const { return finished_; }

 private:
  // The profile that initiated this utterance.
  Profile* profile_;

  // The extension ID of the extension providing TTS for this utterance, or
  // empty if native TTS is being used.
  std::string extension_id_;

  // The unique ID of this utterance, used to associate callback functions
  // with utterances.
  int id_;

  // The id of the next utterance, so we can associate requests with
  // responses.
  static int next_utterance_id_;

  // The text to speak.
  std::string text_;

  // The full options arg passed to tts.speak, which may include fields
  // other than the ones we explicitly parse, below.
  scoped_ptr<base::Value> options_;

  // The extension ID of the extension that called speak() and should
  // receive events.
  std::string src_extension_id_;

  // The source extension's ID of this utterance, so that it can associate
  // events with the appropriate callback.
  int src_id_;

  // The URL of the page where the source extension called speak.
  GURL src_url_;

  // The delegate to be called when an utterance event is fired.
  // Weak reference; it will be cleared after we fire a "final" event
  // (as determined by IsFinalTtsEventType).
  UtteranceEventDelegate* event_delegate_;

  // The parsed options.
  std::string voice_name_;
  std::string lang_;
  TtsGenderType gender_;
  UtteranceContinuousParameters continuous_parameters_;
  bool can_enqueue_;
  std::set<TtsEventType> required_event_types_;
  std::set<TtsEventType> desired_event_types_;

  // The index of the current char being spoken.
  int char_index_;

  // True if this utterance received an event indicating it's done.
  bool finished_;
};

// Singleton class that manages text-to-speech for the TTS and TTS engine
// extension APIs, maintaining a queue of pending utterances and keeping
// track of all state.
class TtsController {
 public:
  // Get the single instance of this class.
  static TtsController* GetInstance();

  // Returns true if we're currently speaking an utterance.
  bool IsSpeaking();

  // Speak the given utterance. If the utterance's can_enqueue flag is true
  // and another utterance is in progress, adds it to the end of the queue.
  // Otherwise, interrupts any current utterance and speaks this one
  // immediately.
  void SpeakOrEnqueue(Utterance* utterance);

  // Stop all utterances and flush the queue. Implies leaving pause mode
  // as well.
  void Stop();

  // Pause the speech queue. Some engines may support pausing in the middle
  // of an utterance.
  void Pause();

  // Resume speaking.
  void Resume();

  // Handle events received from the speech engine. Events are forwarded to
  // the callback function, and in addition, completion and error events
  // trigger finishing the current utterance and starting the next one, if
  // any.
  void OnTtsEvent(int utterance_id,
                  TtsEventType event_type,
                  int char_index,
                  const std::string& error_message);

  // Return a list of all available voices, including the native voice,
  // if supported, and all voices registered by extensions.
  void GetVoices(Profile* profile, std::vector<VoiceData>* out_voices);

  // Called by TtsExtensionLoaderChromeOs::LoadTtsExtension when it
  // finishes loading the built-in TTS component extension.
  void RetrySpeakingQueuedUtterances();

  // Called by the extension system or platform implementation when the
  // list of voices may have changed and should be re-queried.
  void VoicesChanged();

  // Add a delegate that wants to be notified when the set of voices changes.
  void AddVoicesChangedDelegate(VoicesChangedDelegate* delegate);

  // Remove delegate that wants to be notified when the set of voices changes.
  void RemoveVoicesChangedDelegate(VoicesChangedDelegate* delegate);

  // For unit testing.
  void SetPlatformImpl(TtsPlatformImpl* platform_impl);
  int QueueSize();

 protected:
  TtsController();
  virtual ~TtsController();

 private:
  // Get the platform TTS implementation (or injected mock).
  TtsPlatformImpl* GetPlatformImpl();

  // Start speaking the given utterance. Will either take ownership of
  // |utterance| or delete it if there's an error. Returns true on success.
  void SpeakNow(Utterance* utterance);

  // Clear the utterance queue. If send_events is true, will send
  // TTS_EVENT_CANCELLED events on each one.
  void ClearUtteranceQueue(bool send_events);

  // Finalize and delete the current utterance.
  void FinishCurrentUtterance();

  // Start speaking the next utterance in the queue.
  void SpeakNextUtterance();

  // Given an utterance and a vector of voices, return the
  // index of the voice that best matches the utterance.
  int GetMatchingVoice(const Utterance* utterance,
                       std::vector<VoiceData>& voices);

  friend struct DefaultSingletonTraits<TtsController>;

  // The current utterance being spoken.
  Utterance* current_utterance_;

  // Whether the queue is paused or not.
  bool paused_;

  // A queue of utterances to speak after the current one finishes.
  std::queue<Utterance*> utterance_queue_;

  // A set of delegates that want to be notified when the voices change.
  std::set<VoicesChangedDelegate*> voices_changed_delegates_;

  // A pointer to the platform implementation of text-to-speech, for
  // dependency injection.
  TtsPlatformImpl* platform_impl_;

  DISALLOW_COPY_AND_ASSIGN(TtsController);
};

#endif  // CHROME_BROWSER_SPEECH_TTS_CONTROLLER_H_
