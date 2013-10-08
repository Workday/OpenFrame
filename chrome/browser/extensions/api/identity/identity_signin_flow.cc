// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/identity/identity_signin_flow.h"

#include "chrome/browser/app_mode/app_mode_utils.h"
#include "chrome/browser/signin/profile_oauth2_token_service.h"
#include "chrome/browser/signin/profile_oauth2_token_service_factory.h"
#include "chrome/browser/ui/webui/signin/login_ui_service.h"
#include "chrome/browser/ui/webui/signin/login_ui_service_factory.h"

namespace extensions {

IdentitySigninFlow::IdentitySigninFlow(Delegate* delegate, Profile* profile)
    : delegate_(delegate),
      profile_(profile) {
}

IdentitySigninFlow::~IdentitySigninFlow() {
  ProfileOAuth2TokenServiceFactory::GetForProfile(profile_)->
      RemoveObserver(this);
}

void IdentitySigninFlow::Start() {
  DCHECK(delegate_);

#if defined(OS_CHROMEOS)
  // In normal mode (i.e. non-forced app mode), the user has to log out to
  // re-establish credentials. Let the global error popup handle everything.
  if (!chrome::IsRunningInForcedAppMode()) {
    delegate_->SigninFailed();
    return;
  }
#endif

  ProfileOAuth2TokenServiceFactory::GetForProfile(profile_)->AddObserver(this);

  LoginUIService* login_ui_service =
      LoginUIServiceFactory::GetForProfile(profile_);
  login_ui_service->ShowLoginPopup();
}

void IdentitySigninFlow::OnRefreshTokenAvailable(
    const std::string& account_id) {
  delegate_->SigninSuccess();
}

}  // namespace extensions
