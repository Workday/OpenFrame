// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_COMMON_EXTENSIONS_API_SPELLCHECK_SPELLCHECK_HANDLER_H_
#define CHROME_COMMON_EXTENSIONS_API_SPELLCHECK_SPELLCHECK_HANDLER_H_

#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/manifest_handler.h"

namespace extensions {

// This structure holds the information parsed by the SpellcheckHandler to be
// used in the SpellcheckAPI functions. It is stored on the extension.
struct SpellcheckDictionaryInfo : public extensions::Extension::ManifestData {
  SpellcheckDictionaryInfo();
  virtual ~SpellcheckDictionaryInfo();

  std::string language;
  std::string locale;
  std::string path;
  std::string format;
};

// Parses the "spellcheck" manifest key.
class SpellcheckHandler : public ManifestHandler {
 public:
  SpellcheckHandler();
  virtual ~SpellcheckHandler();

  virtual bool Parse(Extension* extension, string16* error) OVERRIDE;

 private:
  virtual const std::vector<std::string> Keys() const OVERRIDE;

  DISALLOW_COPY_AND_ASSIGN(SpellcheckHandler);
};

}  // namespace extensions

#endif  // CHROME_COMMON_EXTENSIONS_API_SPELLCHECK_SPELLCHECK_HANDLER_H_
