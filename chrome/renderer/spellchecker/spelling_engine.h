// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_SPELLCHECKER_SPELLING_ENGINE_H_
#define CHROME_RENDERER_SPELLCHECKER_SPELLING_ENGINE_H_

#include <string>
#include <vector>

#include "base/platform_file.h"
#include "base/strings/string16.h"

// Creates the platform's "native" spelling engine.
class SpellingEngine* CreateNativeSpellingEngine();

// Interface to different spelling engines.
class SpellingEngine {
 public:
  virtual ~SpellingEngine() {}

  // Initialize spelling engine with browser-side info. Must be called before
  // any other functions are called.
  virtual void Init(base::PlatformFile bdict_file) = 0;
  virtual bool InitializeIfNeeded() = 0;
  virtual bool IsEnabled() = 0;
  virtual bool CheckSpelling(const string16& word_to_check, int tag) = 0;
  virtual void FillSuggestionList(
      const string16& wrong_word,
      std::vector<string16>* optional_suggestions) = 0;
};

#endif  // CHROME_RENDERER_SPELLCHECKER_SPELLING_ENGINE_H_

