// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_SYSTEM_INDICATOR_SYSTEM_INDICATOR_API_H_
#define CHROME_BROWSER_EXTENSIONS_API_SYSTEM_INDICATOR_SYSTEM_INDICATOR_API_H_

#include "chrome/browser/extensions/api/extension_action/extension_action_api.h"
#include "chrome/browser/extensions/extension_function.h"

class Profile;

namespace extensions {

class SystemIndicatorSetIconFunction : public ExtensionActionSetIconFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("systemIndicator.setIcon", SYSTEMINDICATOR_SETICON)

 protected:
  virtual ~SystemIndicatorSetIconFunction() {}
};

class SystemIndicatorEnableFunction : public ExtensionActionShowFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("systemIndicator.enable", SYSTEMINDICATOR_ENABLE)

 protected:
  virtual ~SystemIndicatorEnableFunction() {}
};

class SystemIndicatorDisableFunction : public ExtensionActionHideFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("systemIndicator.disable", SYSTEMINDICATOR_DISABLE)

 protected:
  virtual ~SystemIndicatorDisableFunction() {}
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_SYSTEM_INDICATOR_SYSTEM_INDICATOR_API_H_
