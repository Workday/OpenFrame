// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/base/test_switches.h"

namespace switches {

// Also emit full event trace logs for successful tests.
const char kAlsoEmitSuccessLogs[] = "also-emit-success-logs";

// Extra flags that the test should pass to launched browser process.
const char kExtraChromeFlags[] = "extra-chrome-flags";

// Enable Chromium branding of the executable.
const char kEnableChromiumBranding[] = "enable-chromium-branding";

// Enable displaying error dialogs (for debugging).
const char kEnableErrorDialogs[] = "enable-errdialogs";

#if defined(OS_WIN) && defined(USE_AURA)
// Force browser tests to run in Ash/Metro on Windows 8.
const char kAshBrowserTests[] = "ash-browsertests";
#endif

}  // namespace switches
