// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/login_performer.h"

#include <string>

#include "base/bind.h"
#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "base/metrics/histogram.h"
#include "base/prefs/pref_service.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/chromeos/boot_times_loader.h"
#include "chrome/browser/chromeos/login/login_utils.h"
#include "chrome/browser/chromeos/login/managed/locally_managed_user_login_flow.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/policy/device_local_account_policy_service.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/browser/chromeos/settings/cros_settings_names.h"
#include "chrome/browser/policy/browser_policy_connector.h"
#include "chrome/common/pref_names.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/session_manager_client.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/user_metrics.h"
#include "google_apis/gaia/gaia_auth_util.h"
#include "grit/generated_resources.h"
#include "net/cookies/cookie_monster.h"
#include "net/cookies/cookie_store.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_getter.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"

using content::BrowserThread;
using content::UserMetricsAction;

namespace chromeos {

LoginPerformer::LoginPerformer(Delegate* delegate)
    : online_attempt_host_(this),
      last_login_failure_(LoginFailure::LoginFailureNone()),
      delegate_(delegate),
      password_changed_(false),
      password_changed_callback_count_(0),
      auth_mode_(AUTH_MODE_INTERNAL),
      weak_factory_(this) {
}

LoginPerformer::~LoginPerformer() {
  DVLOG(1) << "Deleting LoginPerformer";
  if (authenticator_.get())
    authenticator_->SetConsumer(NULL);
}

////////////////////////////////////////////////////////////////////////////////
// LoginPerformer, LoginStatusConsumer implementation:

void LoginPerformer::OnLoginFailure(const LoginFailure& failure) {
  content::RecordAction(UserMetricsAction("Login_Failure"));
  UMA_HISTOGRAM_ENUMERATION("Login.FailureReason", failure.reason(),
                            LoginFailure::NUM_FAILURE_REASONS);

  DVLOG(1) << "failure.reason " << failure.reason();
  DVLOG(1) << "failure.error.state " << failure.error().state();

  last_login_failure_ = failure;
  if (delegate_) {
    delegate_->OnLoginFailure(failure);
    return;
  } else {
    // COULD_NOT_MOUNT_CRYPTOHOME, COULD_NOT_MOUNT_TMPFS:
    // happens during offline auth only.
    NOTREACHED();
  }
}

void LoginPerformer::OnRetailModeLoginSuccess(
    const UserContext& user_context) {
  content::RecordAction(
      UserMetricsAction("Login_DemoUserLoginSuccess"));
  LoginStatusConsumer::OnRetailModeLoginSuccess(user_context);
}

void LoginPerformer::OnLoginSuccess(
    const UserContext& user_context,
    bool pending_requests,
    bool using_oauth) {
  content::RecordAction(UserMetricsAction("Login_Success"));
  // The value of |pending_requests| indicates:
  // 0 - New regular user, login success offline and online.
  //     - or -
  //     Existing regular user, login success offline and online, offline
  //     authentication took longer than online authentication.
  //     - or -
  //     Public account user, login successful.
  // 1 - Existing regular user, login success offline only.
  UMA_HISTOGRAM_ENUMERATION("Login.SuccessReason", pending_requests, 2);

  VLOG(1) << "LoginSuccess hash: " << user_context.username_hash
          << ", pending_requests " << pending_requests;
  DCHECK(delegate_);
  // After delegate_->OnLoginSuccess(...) is called, delegate_ releases
  // LoginPerformer ownership. LP now manages it's lifetime on its own.
  DCHECK(!pending_requests);
  base::MessageLoop::current()->DeleteSoon(FROM_HERE, this);

  delegate_->OnLoginSuccess(user_context,
                            pending_requests,
                            using_oauth);
}

void LoginPerformer::OnOffTheRecordLoginSuccess() {
  content::RecordAction(
      UserMetricsAction("Login_GuestLoginSuccess"));

  if (delegate_)
    delegate_->OnOffTheRecordLoginSuccess();
  else
    NOTREACHED();
}

void LoginPerformer::OnPasswordChangeDetected() {
  password_changed_ = true;
  password_changed_callback_count_++;
  if (delegate_) {
    delegate_->OnPasswordChangeDetected();
  } else {
    NOTREACHED();
  }
}

void LoginPerformer::OnChecked(const std::string& username, bool success) {
  if (!delegate_) {
    // Delegate is reset in case of successful offline login.
    // See ExistingUserConstoller::OnLoginSuccess().
    // Case when user has changed password and enters old password
    // does not block user from sign in yet.
    return;
  }
  delegate_->OnOnlineChecked(username, success);
}

////////////////////////////////////////////////////////////////////////////////
// LoginPerformer, public:

void LoginPerformer::PerformLogin(const UserContext& user_context,
                                  AuthorizationMode auth_mode) {
  auth_mode_ = auth_mode;
  user_context_ = user_context;

  CrosSettings* cros_settings = CrosSettings::Get();

  // Whitelist check is always performed during initial login.
  CrosSettingsProvider::TrustedStatus status =
      cros_settings->PrepareTrustedValues(
          base::Bind(&LoginPerformer::PerformLogin,
                     weak_factory_.GetWeakPtr(),
                     user_context_, auth_mode));
  // Must not proceed without signature verification.
  if (status == CrosSettingsProvider::PERMANENTLY_UNTRUSTED) {
    if (delegate_)
      delegate_->PolicyLoadFailed();
    else
      NOTREACHED();
    return;
  } else if (status != CrosSettingsProvider::TRUSTED) {
    // Value of AllowNewUser setting is still not verified.
    // Another attempt will be invoked after verification completion.
    return;
  }

  bool is_whitelisted = LoginUtils::IsWhitelisted(
      gaia::CanonicalizeEmail(user_context.username));
  if (is_whitelisted) {
    switch (auth_mode_) {
      case AUTH_MODE_EXTENSION:
        StartLoginCompletion();
        break;
      case AUTH_MODE_INTERNAL:
        StartAuthentication();
        break;
    }
  } else {
    if (delegate_)
      delegate_->WhiteListCheckFailed(user_context.username);
    else
      NOTREACHED();
  }
}

void LoginPerformer::LoginAsLocallyManagedUser(
    const UserContext& user_context) {
  DCHECK_EQ(UserManager::kLocallyManagedUserDomain,
            gaia::ExtractDomainName(user_context.username));
  // TODO(nkostylev): Check that policy allows locally managed user login.
  UserFlow* new_flow = new LocallyManagedUserLoginFlow(user_context.username);
  new_flow->set_host(
      UserManager::Get()->GetUserFlow(user_context.username)->host());
  UserManager::Get()->SetUserFlow(user_context.username, new_flow);

  authenticator_ = LoginUtils::Get()->CreateAuthenticator(this);
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&Authenticator::LoginAsLocallyManagedUser,
                 authenticator_.get(),
                 user_context));
}

void LoginPerformer::LoginRetailMode() {
  authenticator_ = LoginUtils::Get()->CreateAuthenticator(this);
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&Authenticator::LoginRetailMode, authenticator_.get()));
}

void LoginPerformer::LoginOffTheRecord() {
  authenticator_ = LoginUtils::Get()->CreateAuthenticator(this);
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&Authenticator::LoginOffTheRecord, authenticator_.get()));
}

void LoginPerformer::LoginAsPublicAccount(const std::string& username) {
  // Login is not allowed if policy could not be loaded for the account.
  policy::DeviceLocalAccountPolicyService* policy_service =
      g_browser_process->browser_policy_connector()->
          GetDeviceLocalAccountPolicyService();
  if (!policy_service || !policy_service->IsPolicyAvailableForUser(username)) {
    DCHECK(delegate_);
    if (delegate_)
      delegate_->PolicyLoadFailed();
    return;
  }

  authenticator_ = LoginUtils::Get()->CreateAuthenticator(this);
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&Authenticator::LoginAsPublicAccount, authenticator_.get(),
                 username));
}

void LoginPerformer::RecoverEncryptedData(const std::string& old_password) {
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&Authenticator::RecoverEncryptedData, authenticator_.get(),
                 old_password));
}

void LoginPerformer::ResyncEncryptedData() {
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&Authenticator::ResyncEncryptedData, authenticator_.get()));
}

////////////////////////////////////////////////////////////////////////////////
// LoginPerformer, private:

void LoginPerformer::StartLoginCompletion() {
  DVLOG(1) << "Login completion started";
  BootTimesLoader::Get()->AddLoginTimeMarker("AuthStarted", false);
  Profile* profile = ProfileHelper::GetSigninProfile();

  authenticator_ = LoginUtils::Get()->CreateAuthenticator(this);
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&Authenticator::CompleteLogin, authenticator_.get(),
                 profile,
                 user_context_));

  user_context_.password.clear();
  user_context_.auth_code.clear();
}

void LoginPerformer::StartAuthentication() {
  DVLOG(1) << "Auth started";
  BootTimesLoader::Get()->AddLoginTimeMarker("AuthStarted", false);
  Profile* profile = ProfileHelper::GetSigninProfile();
  if (delegate_) {
    authenticator_ = LoginUtils::Get()->CreateAuthenticator(this);
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(&Authenticator::AuthenticateToLogin, authenticator_.get(),
                   profile,
                   user_context_));
    // Make unobtrusive online check. It helps to determine password change
    // state in the case when offline login fails.
    online_attempt_host_.Check(profile, user_context_);
  } else {
    NOTREACHED();
  }
  user_context_.password.clear();
  user_context_.auth_code.clear();
}

}  // namespace chromeos
