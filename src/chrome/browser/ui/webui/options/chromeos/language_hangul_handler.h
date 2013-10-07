// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_OPTIONS_CHROMEOS_LANGUAGE_HANGUL_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_OPTIONS_CHROMEOS_LANGUAGE_HANGUL_HANDLER_H_

#include "base/compiler_specific.h"
#include "chrome/browser/ui/webui/options/options_ui.h"

namespace base {
class DictionaryValue;
class ListValue;
}

namespace chromeos {
namespace options {

// Hangul options page UI handler.
class LanguageHangulHandler : public ::options::OptionsPageUIHandler {
 public:
  LanguageHangulHandler();
  virtual ~LanguageHangulHandler();

  // OptionsPageUIHandler implementation.
  virtual void GetLocalizedValues(
      base::DictionaryValue* localized_strings) OVERRIDE;

 private:
  // Returns the list of hangul keyboards.
  static base::ListValue* GetKeyboardLayoutList();

  DISALLOW_COPY_AND_ASSIGN(LanguageHangulHandler);
};

}  // namespace options
}  // namespace chromeos

#endif  // CHROME_BROWSER_UI_WEBUI_OPTIONS_CHROMEOS_LANGUAGE_HANGUL_HANDLER_H_
