// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_AUTOCOMPLETE_AUTOCOMPLETE_CLASSIFIER_H_
#define CHROME_BROWSER_AUTOCOMPLETE_AUTOCOMPLETE_CLASSIFIER_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "base/strings/string16.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service.h"

class AutocompleteController;
struct AutocompleteMatch;
class GURL;
class Profile;

class AutocompleteClassifier : public BrowserContextKeyedService {
 public:
  // Bitmap of AutocompleteProvider::Type values describing the default set of
  // providers queried for the omnibox.  Intended to be passed to
  // AutocompleteController().
  static const int kDefaultOmniboxProviders;

  // Bitmap of AutocompleteProvider::Type values describing the set of providers
  // that have been whitelisted as working properly with the Instant Extended
  // API.  Intended to be passed to AutocompleteController().
  static const int kInstantExtendedOmniboxProviders;

  explicit AutocompleteClassifier(Profile* profile);
  virtual ~AutocompleteClassifier();

  // Given some string |text| that the user wants to use for navigation,
  // determines how it should be interpreted.
  // |prefer_keyword| should be true the when keyword UI is onscreen; see
  // comments on AutocompleteController::Start().
  // |allow_exact_keyword_match| should be true when treating the string as a
  // potential keyword search is valid; see
  // AutocompleteInput::allow_exact_keyword_match(). |match| should be a
  // non-NULL outparam that will be set to the default match for this input, if
  // any (for invalid input, there will be no default match, and |match| will be
  // left unchanged).  |alternate_nav_url| is a possibly-NULL outparam that, if
  // non-NULL, will be set to the navigational URL (if any) in case of an
  // accidental search; see comments on
  // AutocompleteResult::alternate_nav_url_ in autocomplete.h.
  void Classify(const string16& text,
                bool prefer_keyword,
                bool allow_exact_keyword_match,
                AutocompleteMatch* match,
                GURL* alternate_nav_url);

 private:
  // BrowserContextKeyedService:
  virtual void Shutdown() OVERRIDE;

  scoped_ptr<AutocompleteController> controller_;

  // Are we currently in Classify? Used to verify Classify isn't invoked
  // recursively, since this can corrupt state and cause crashes.
  bool inside_classify_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(AutocompleteClassifier);
};

#endif  // CHROME_BROWSER_AUTOCOMPLETE_AUTOCOMPLETE_CLASSIFIER_H_
