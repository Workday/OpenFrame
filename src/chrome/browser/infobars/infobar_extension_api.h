// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_INFOBARS_INFOBAR_EXTENSION_API_H_
#define CHROME_BROWSER_INFOBARS_INFOBAR_EXTENSION_API_H_

#include "chrome/browser/extensions/extension_function.h"

class InfobarsShowFunction : public SyncExtensionFunction {
  virtual ~InfobarsShowFunction() {}
  virtual bool RunImpl() OVERRIDE;
  DECLARE_EXTENSION_FUNCTION("infobars.show", INFOBARS_SHOW)
};

#endif  // CHROME_BROWSER_INFOBARS_INFOBAR_EXTENSION_API_H_
