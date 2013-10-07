// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/startup/bad_flags_prompt.h"

#include "base/command_line.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/infobars/infobar_service.h"
#include "chrome/browser/infobars/simple_alert_infobar_delegate.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_switches.h"
#include "extensions/common/switches.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

namespace chrome {

void ShowBadFlagsPrompt(Browser* browser) {
  content::WebContents* web_contents =
      browser->tab_strip_model()->GetActiveWebContents();
  if (!web_contents)
    return;

  // Unsupported flags for which to display a warning that "stability and
  // security will suffer".
  static const char* kBadFlags[] = {
    // These imply disabling the sandbox.
    switches::kSingleProcess,
    switches::kNoSandbox,
    switches::kDisableWebSecurity,
    // Browser plugin is dangerous on regular pages because it breaks the Same
    // Origin Policy.
    switches::kEnableBrowserPluginForAllViewTypes,
    extensions::switches::kExtensionsOnChromeURLs,
    // This parameter should be used only for server side developments.
    switches::kTranslateScriptURL,
    switches::kTranslateSecurityOrigin,
    NULL
  };

  for (const char** flag = kBadFlags; *flag; ++flag) {
    if (CommandLine::ForCurrentProcess()->HasSwitch(*flag)) {
      SimpleAlertInfoBarDelegate::Create(
          InfoBarService::FromWebContents(web_contents),
          InfoBarDelegate::kNoIconID,
          l10n_util::GetStringFUTF16(IDS_BAD_FLAGS_WARNING_MESSAGE,
                                     UTF8ToUTF16(std::string("--") + *flag)),
          false);
      return;
    }
  }
}

}  // namespace chrome
