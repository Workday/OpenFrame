// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_COMMON_EXTENSIONS_API_INPUT_IME_INPUT_COMPONENTS_HANDLER_H_
#define CHROME_COMMON_EXTENSIONS_API_INPUT_IME_INPUT_COMPONENTS_HANDLER_H_

#include <set>
#include <string>
#include <vector>

#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/manifest_handler.h"
#include "url/gurl.h"

namespace extensions {

class Extension;

enum InputComponentType {
  INPUT_COMPONENT_TYPE_NONE = -1,
  INPUT_COMPONENT_TYPE_IME,
  INPUT_COMPONENT_TYPE_COUNT
};

struct InputComponentInfo {
  // Define out of line constructor/destructor to please Clang.
  InputComponentInfo();
  ~InputComponentInfo();

  std::string name;
  InputComponentType type;
  std::string id;
  std::string description;
  std::set<std::string> languages;
  std::set<std::string> layouts;
  std::string shortcut_keycode;
  bool shortcut_alt;
  bool shortcut_ctrl;
  bool shortcut_shift;
  GURL options_page_url;
};

struct InputComponents : public Extension::ManifestData {
  // Define out of line constructor/destructor to please Clang.
  InputComponents();
  virtual ~InputComponents();

  std::vector<InputComponentInfo> input_components;

  // Returns list of input components and associated properties.
  static const std::vector<InputComponentInfo>* GetInputComponents(
      const Extension* extension);
};

// Parses the "incognito" manifest key.
class InputComponentsHandler : public ManifestHandler {
 public:
  InputComponentsHandler();
  virtual ~InputComponentsHandler();

  virtual bool Parse(Extension* extension, string16* error) OVERRIDE;

 private:
  virtual const std::vector<std::string> Keys() const OVERRIDE;

  DISALLOW_COPY_AND_ASSIGN(InputComponentsHandler);
};

}  // namespace extensions

#endif  // CHROME_COMMON_EXTENSIONS_API_INPUT_IME_INPUT_COMPONENTS_HANDLER_H_
