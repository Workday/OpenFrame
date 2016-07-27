// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/settings/settings_default_browser_handler.h"

#include "base/bind.h"
#include "base/prefs/pref_service.h"
#include "chrome/browser/browser_process.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/web_ui.h"

namespace settings {

DefaultBrowserHandler::DefaultBrowserHandler(content::WebUI* webui)
    : default_browser_worker_(
          new ShellIntegration::DefaultBrowserWorker(this)) {
  default_browser_policy_.Init(
      prefs::kDefaultBrowserSettingEnabled, g_browser_process->local_state(),
      base::Bind(&DefaultBrowserHandler::RequestDefaultBrowserState,
                 base::Unretained(this), nullptr));
}

DefaultBrowserHandler::~DefaultBrowserHandler() {
  default_browser_worker_->ObserverDestroyed();
}

void DefaultBrowserHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "SettingsDefaultBrowser.requestDefaultBrowserState",
      base::Bind(&DefaultBrowserHandler::RequestDefaultBrowserState,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "SettingsDefaultBrowser.setAsDefaultBrowser",
      base::Bind(&DefaultBrowserHandler::SetAsDefaultBrowser,
                 base::Unretained(this)));
}

void DefaultBrowserHandler::SetDefaultWebClientUIState(
    ShellIntegration::DefaultWebClientUIState state) {
  if (state == ShellIntegration::STATE_PROCESSING)
    return;

  if (state == ShellIntegration::STATE_IS_DEFAULT) {
    // Notify the user in the future if Chrome ceases to be the user's chosen
    // default browser.
    Profile::FromWebUI(web_ui())->GetPrefs()->SetBoolean(
        prefs::kCheckDefaultBrowser, true);
  }

  base::FundamentalValue is_default(
      state == ShellIntegration::STATE_IS_DEFAULT);
  base::FundamentalValue can_be_default(
      state != ShellIntegration::STATE_UNKNOWN &&
      !default_browser_policy_.IsManaged() &&
      ShellIntegration::CanSetAsDefaultBrowser() !=
          ShellIntegration::SET_DEFAULT_NOT_ALLOWED);

  web_ui()->CallJavascriptFunction("Settings.updateDefaultBrowserState",
                                   is_default, can_be_default);
}

bool DefaultBrowserHandler::IsInteractiveSetDefaultPermitted() {
  return true;
}

void DefaultBrowserHandler::OnSetAsDefaultConcluded(bool succeeded) {
  base::FundamentalValue success(succeeded);
  web_ui()->CallJavascriptFunction("Settings.setAsDefaultConcluded", success);
}

void DefaultBrowserHandler::RequestDefaultBrowserState(
    const base::ListValue* /*args*/) {
  default_browser_worker_->StartCheckIsDefault();
}

void DefaultBrowserHandler::SetAsDefaultBrowser(const base::ListValue* args) {
  CHECK(!default_browser_policy_.IsManaged());

  default_browser_worker_->StartSetAsDefault();

  // If the user attempted to make Chrome the default browser, notify
  // them when this changes.
  Profile::FromWebUI(web_ui())->GetPrefs()->SetBoolean(
      prefs::kCheckDefaultBrowser, true);
}

}  // namespace settings
