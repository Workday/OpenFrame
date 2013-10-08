// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_SCREENS_CORE_OOBE_ACTOR_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_SCREENS_CORE_OOBE_ACTOR_H_

#include <string>

#include "chrome/browser/chromeos/login/help_app_launcher.h"

namespace base {
class DictionaryValue;
}

namespace chromeos {

class CoreOobeActor {
 public:
  virtual ~CoreOobeActor() {}

  virtual void ShowSignInError(int login_attempts,
                               const std::string& error_text,
                               const std::string& help_link_text,
                               HelpAppLauncher::HelpTopic help_topic_id) = 0;
  virtual void ShowTpmError() = 0;
  virtual void ShowSignInUI(const std::string& email) = 0;
  virtual void ResetSignInUI(bool force_online) = 0;
  virtual void ClearUserPodPassword() = 0;
  virtual void RefocusCurrentPod() = 0;
  virtual void OnLoginSuccess(const std::string& username) = 0;
  virtual void ShowPasswordChangedScreen(bool show_password_error) = 0;
  virtual void SetUsageStats(bool checked) = 0;
  virtual void SetOemEulaUrl(const std::string& oem_eula_url) = 0;
  virtual void SetTpmPassword(const std::string& tmp_password) = 0;
  virtual void ClearErrors() = 0;
  virtual void ReloadContent(const base::DictionaryValue& dictionary) = 0;
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_SCREENS_CORE_OOBE_ACTOR_H_
