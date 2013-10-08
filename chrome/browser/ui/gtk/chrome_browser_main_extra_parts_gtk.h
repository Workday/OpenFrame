// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_GTK_CHROME_BROWSER_MAIN_EXTRA_PARTS_GTK_H_
#define CHROME_BROWSER_UI_GTK_CHROME_BROWSER_MAIN_EXTRA_PARTS_GTK_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "chrome/browser/chrome_browser_main_extra_parts.h"

class ChromeBrowserMainExtraPartsGtk : public ChromeBrowserMainExtraParts {
 public:
  ChromeBrowserMainExtraPartsGtk();

  static void ShowMessageBox(const char* message);

  // Overridden from ChromeBrowserMainExtraParts:
  virtual void PreEarlyInitialization() OVERRIDE;

 private:
  void DetectRunningAsRoot();

  DISALLOW_COPY_AND_ASSIGN(ChromeBrowserMainExtraPartsGtk);
};

#endif  // CHROME_BROWSER_UI_GTK_CHROME_BROWSER_MAIN_EXTRA_PARTS_GTK_H_
