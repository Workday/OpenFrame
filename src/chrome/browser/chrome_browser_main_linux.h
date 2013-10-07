// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Contains functions used by BrowserMain() that are linux-specific.

#ifndef CHROME_BROWSER_CHROME_BROWSER_MAIN_LINUX_H_
#define CHROME_BROWSER_CHROME_BROWSER_MAIN_LINUX_H_

#include "base/compiler_specific.h"
#include "chrome/browser/chrome_browser_main_posix.h"

class ChromeBrowserMainPartsLinux : public ChromeBrowserMainPartsPosix {
 public:
  explicit ChromeBrowserMainPartsLinux(
      const content::MainFunctionParams& parameters);
  virtual ~ChromeBrowserMainPartsLinux();

  // ChromeBrowserMainParts overrides.
  virtual void PreProfileInit() OVERRIDE;
  virtual void PostProfileInit() OVERRIDE;

 private:
  DISALLOW_COPY_AND_ASSIGN(ChromeBrowserMainPartsLinux);
};

#endif  // CHROME_BROWSER_CHROME_BROWSER_MAIN_LINUX_H_
