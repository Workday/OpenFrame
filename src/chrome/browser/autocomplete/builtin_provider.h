// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_AUTOCOMPLETE_BUILTIN_PROVIDER_H_
#define CHROME_BROWSER_AUTOCOMPLETE_BUILTIN_PROVIDER_H_

#include <vector>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/strings/string16.h"
#include "chrome/browser/autocomplete/autocomplete_match.h"
#include "chrome/browser/autocomplete/autocomplete_provider.h"

// This is the provider for built-in URLs, such as about:settings and
// chrome://version.
class BuiltinProvider : public AutocompleteProvider {
 public:
  BuiltinProvider(AutocompleteProviderListener* listener, Profile* profile);

  // AutocompleteProvider:
  virtual void Start(const AutocompleteInput& input,
                     bool minimal_changes) OVERRIDE;

 private:
  virtual ~BuiltinProvider();

  typedef std::vector<string16> Builtins;

  static const int kRelevance;

  void AddMatch(const string16& match_string,
                const ACMatchClassifications& styles);

  Builtins builtins_;

  DISALLOW_COPY_AND_ASSIGN(BuiltinProvider);
};

#endif  // CHROME_BROWSER_AUTOCOMPLETE_BUILTIN_PROVIDER_H_
