// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_COMMAND_LINE_PRIVATE_COMMAND_LINE_PRIVATE_API_H__
#define CHROME_BROWSER_EXTENSIONS_API_COMMAND_LINE_PRIVATE_COMMAND_LINE_PRIVATE_API_H__

#include "chrome/browser/extensions/extension_function.h"

namespace extensions {

class CommandLinePrivateHasSwitchFunction : public SyncExtensionFunction {
  DECLARE_EXTENSION_FUNCTION("commandLinePrivate.hasSwitch",
                             COMMANDLINEPRIVATE_HASSWITCH)
 protected:
  virtual ~CommandLinePrivateHasSwitchFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_COMMAND_LINE_PRIVATE_COMMAND_LINE_PRIVATE_API_H__
