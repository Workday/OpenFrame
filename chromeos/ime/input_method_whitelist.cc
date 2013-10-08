// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/ime/input_method_whitelist.h"

#include <vector>

#include "chromeos/ime/input_method_descriptor.h"
#include "chromeos/ime/input_methods.h"

namespace chromeos {
namespace input_method {

InputMethodWhitelist::InputMethodWhitelist() {
  for (size_t i = 0; i < arraysize(kInputMethods); ++i) {
    supported_input_methods_.insert(kInputMethods[i].input_method_id);
  }
}

InputMethodWhitelist::~InputMethodWhitelist() {
}

bool InputMethodWhitelist::InputMethodIdIsWhitelisted(
    const std::string& input_method_id) const {
  return supported_input_methods_.count(input_method_id) > 0;
}

scoped_ptr<InputMethodDescriptors>
InputMethodWhitelist::GetSupportedInputMethods() const {
  scoped_ptr<InputMethodDescriptors> input_methods(new InputMethodDescriptors);
  input_methods->reserve(arraysize(kInputMethods));
  for (size_t i = 0; i < arraysize(kInputMethods); ++i) {
    std::vector<std::string> layouts;
    layouts.push_back(kInputMethods[i].xkb_layout_id);

    std::vector<std::string> languages;
    languages.push_back(kInputMethods[i].language_code);

    input_methods->push_back(InputMethodDescriptor(
        kInputMethods[i].input_method_id,
        "",
        layouts,
        languages,
        GURL()));  // options page url, not available for non-extension input
                   // method.
  }
  return input_methods.Pass();
}

}  // namespace input_method
}  // namespace chromeos
