// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/app_mode/startup_app_launcher.h"

#include "ash/shell.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/json/json_file_value_serializer.h"
#include "base/path_service.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/chromeos/app_mode/app_session_lifetime.h"
#include "chrome/browser/chromeos/app_mode/kiosk_app_manager.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/ui/app_launch_view.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/extensions/webstore_startup_installer.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/signin/profile_oauth2_token_service.h"
#include "chrome/browser/signin/profile_oauth2_token_service_factory.h"
#include "chrome/browser/signin/token_service.h"
#include "chrome/browser/signin/token_service_factory.h"
#include "chrome/browser/ui/extensions/application_launch.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/manifest_handlers/kiosk_enabled_info.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_service.h"
#include "google_apis/gaia/gaia_auth_consumer.h"
#include "google_apis/gaia/gaia_constants.h"

using content::BrowserThread;
using extensions::Extension;
using extensions::WebstoreStartupInstaller;

namespace chromeos {

namespace {

const char kOAuthRefreshToken[] = "refresh_token";
const char kOAuthClientId[] = "client_id";
const char kOAuthClientSecret[] = "client_secret";

const base::FilePath::CharType kOAuthFileName[] =
    FILE_PATH_LITERAL("kiosk_auth");

// Application install splash screen minimum show time in milliseconds.
const int kAppInstallSplashScreenMinTimeMS = 3000;

bool IsAppInstalled(Profile* profile, const std::string& app_id) {
  return extensions::ExtensionSystem::Get(profile)->extension_service()->
      GetInstalledExtension(app_id);
}

}  // namespace

StartupAppLauncher::StartupAppLauncher(Profile* profile,
                                       const std::string& app_id)
    : profile_(profile),
      app_id_(app_id),
      launch_splash_start_time_(0) {
  DCHECK(profile_);
  DCHECK(Extension::IdIsValid(app_id_));
  DCHECK(ash::Shell::HasInstance());
  ash::Shell::GetInstance()->AddPreTargetHandler(this);
}

StartupAppLauncher::~StartupAppLauncher() {
  DCHECK(ash::Shell::HasInstance());
  ash::Shell::GetInstance()->RemovePreTargetHandler(this);
}

void StartupAppLauncher::Start() {
  launch_splash_start_time_ = base::TimeTicks::Now().ToInternalValue();
  DVLOG(1) << "Starting... connection = "
           <<  net::NetworkChangeNotifier::GetConnectionType();
  chromeos::ShowAppLaunchSplashScreen(app_id_);
  StartLoadingOAuthFile();
}

void StartupAppLauncher::StartLoadingOAuthFile() {
  KioskOAuthParams* auth_params = new KioskOAuthParams();
  BrowserThread::PostBlockingPoolTaskAndReply(
      FROM_HERE,
      base::Bind(&StartupAppLauncher::LoadOAuthFileOnBlockingPool,
                 auth_params),
      base::Bind(&StartupAppLauncher::OnOAuthFileLoaded,
                 AsWeakPtr(),
                 base::Owned(auth_params)));
}

// static.
void StartupAppLauncher::LoadOAuthFileOnBlockingPool(
    KioskOAuthParams* auth_params) {
  int error_code = JSONFileValueSerializer::JSON_NO_ERROR;
  std::string error_msg;
  base::FilePath user_data_dir;
  CHECK(PathService::Get(chrome::DIR_USER_DATA, &user_data_dir));
  base::FilePath auth_file = user_data_dir.Append(kOAuthFileName);
  scoped_ptr<JSONFileValueSerializer> serializer(
      new JSONFileValueSerializer(user_data_dir.Append(kOAuthFileName)));
  scoped_ptr<base::Value> value(
      serializer->Deserialize(&error_code, &error_msg));
  base::DictionaryValue* dict = NULL;
  if (error_code != JSONFileValueSerializer::JSON_NO_ERROR ||
      !value.get() || !value->GetAsDictionary(&dict)) {
    LOG(WARNING) << "Can't find auth file at " << auth_file.value();
    return;
  }

  dict->GetString(kOAuthRefreshToken, &auth_params->refresh_token);
  dict->GetString(kOAuthClientId, &auth_params->client_id);
  dict->GetString(kOAuthClientSecret, &auth_params->client_secret);
}

void StartupAppLauncher::OnOAuthFileLoaded(KioskOAuthParams* auth_params) {
  auth_params_ = *auth_params;
  // Override chrome client_id and secret that will be used for identity
  // API token minting.
  if (!auth_params_.client_id.empty() && !auth_params_.client_secret.empty()) {
    UserManager::Get()->SetAppModeChromeClientOAuthInfo(
            auth_params_.client_id,
            auth_params_.client_secret);
  }

  // If we are restarting chrome (i.e. on crash), we need to initialize
  // TokenService as well.
  InitializeTokenService();
}

void StartupAppLauncher::InitializeNetwork() {
  chromeos::UpdateAppLaunchSplashScreenState(
      chromeos::APP_LAUNCH_STATE_PREPARING_NETWORK);
  // Set a maximum allowed wait time for network.
  const int kMaxNetworkWaitSeconds = 5 * 60;
  network_wait_timer_.Start(
      FROM_HERE,
      base::TimeDelta::FromSeconds(kMaxNetworkWaitSeconds),
      this, &StartupAppLauncher::OnNetworkWaitTimedout);

  net::NetworkChangeNotifier::AddNetworkChangeObserver(this);
  OnNetworkChanged(net::NetworkChangeNotifier::GetConnectionType());
}

void StartupAppLauncher::InitializeTokenService() {
  chromeos::UpdateAppLaunchSplashScreenState(
      chromeos::APP_LAUNCH_STATE_LOADING_TOKEN_SERVICE);
  ProfileOAuth2TokenService* profile_token_service =
      ProfileOAuth2TokenServiceFactory::GetForProfile(profile_);
  if (profile_token_service->RefreshTokenIsAvailable()) {
    InitializeNetwork();
    return;
  }

  // At the end of this method, the execution will be put on hold until
  // ProfileOAuth2TokenService triggers either OnRefreshTokenAvailable or
  // OnRefreshTokensLoaded. Given that we want to handle exactly one event,
  // whichever comes first, both handlers call RemoveObserver on PO2TS. Handling
  // any of the two events is the only way to resume the execution and enable
  // Cleanup method to be called, self-invoking a destructor. In destructor
  // StartupAppLauncher is no longer an observer of PO2TS and there is no need
  // to call RemoveObserver again.
  profile_token_service->AddObserver(this);

  TokenService* token_service = TokenServiceFactory::GetForProfile(profile_);
  token_service->Initialize(GaiaConstants::kChromeSource, profile_);

  // Pass oauth2 refresh token from the auth file.
  // TODO(zelidrag): We should probably remove this option after M27.
  // TODO(fgorski): This can go when we have persistence implemented on PO2TS.
  // Unless the code is no longer needed.
  if (!auth_params_.refresh_token.empty()) {
    token_service->UpdateCredentialsWithOAuth2(
        GaiaAuthConsumer::ClientOAuthResult(
            auth_params_.refresh_token,
            std::string(),  // access_token
            0));            // new_expires_in_secs
  } else {
    // Load whatever tokens we have stored there last time around.
    token_service->LoadTokensFromDB();
  }
}

void StartupAppLauncher::OnRefreshTokenAvailable(
    const std::string& account_id) {
  ProfileOAuth2TokenServiceFactory::GetForProfile(profile_)
      ->RemoveObserver(this);
  InitializeNetwork();
}

void StartupAppLauncher::OnRefreshTokensLoaded() {
  ProfileOAuth2TokenServiceFactory::GetForProfile(profile_)
      ->RemoveObserver(this);
  InitializeNetwork();
}

void StartupAppLauncher::Cleanup() {
  chromeos::CloseAppLaunchSplashScreen();

  delete this;
}

void StartupAppLauncher::OnLaunchSuccess() {
  const int64 time_taken_ms = (base::TimeTicks::Now() -
      base::TimeTicks::FromInternalValue(launch_splash_start_time_)).
      InMilliseconds();

  // Enforce that we show app install splash screen for some minimum amount
  // of time.
  if (time_taken_ms < kAppInstallSplashScreenMinTimeMS) {
    BrowserThread::PostDelayedTask(
        BrowserThread::UI,
        FROM_HERE,
        base::Bind(&StartupAppLauncher::OnLaunchSuccess, AsWeakPtr()),
        base::TimeDelta::FromMilliseconds(
            kAppInstallSplashScreenMinTimeMS - time_taken_ms));
    return;
  }

  Cleanup();
}

void StartupAppLauncher::OnLaunchFailure(KioskAppLaunchError::Error error) {
  DCHECK_NE(KioskAppLaunchError::NONE, error);

  // Saves the error and ends the session to go back to login screen.
  KioskAppLaunchError::Save(error);
  chrome::AttemptUserExit();

  Cleanup();
}

void StartupAppLauncher::Launch() {
  const Extension* extension = extensions::ExtensionSystem::Get(profile_)->
      extension_service()->GetInstalledExtension(app_id_);
  CHECK(extension);

  if (!extensions::KioskEnabledInfo::IsKioskEnabled(extension)) {
    OnLaunchFailure(KioskAppLaunchError::NOT_KIOSK_ENABLED);
    return;
  }

  // Always open the app in a window.
  chrome::OpenApplication(chrome::AppLaunchParams(profile_,
                                                  extension,
                                                  extension_misc::LAUNCH_WINDOW,
                                                  NEW_WINDOW));
  InitAppSession(profile_, app_id_);

  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_KIOSK_APP_LAUNCHED,
      content::NotificationService::AllSources(),
      content::NotificationService::NoDetails());

  OnLaunchSuccess();
}

void StartupAppLauncher::BeginInstall() {
  DVLOG(1) << "BeginInstall... connection = "
           <<  net::NetworkChangeNotifier::GetConnectionType();

  chromeos::UpdateAppLaunchSplashScreenState(
      chromeos::APP_LAUNCH_STATE_INSTALLING_APPLICATION);

  if (IsAppInstalled(profile_, app_id_)) {
    Launch();
    return;
  }

  installer_ = new WebstoreStartupInstaller(
      app_id_,
      profile_,
      false,
      base::Bind(&StartupAppLauncher::InstallCallback, AsWeakPtr()));
  installer_->BeginInstall();
}

void StartupAppLauncher::InstallCallback(bool success,
                                         const std::string& error) {
  installer_ = NULL;
  if (success) {
    // Schedules Launch() to be called after the callback returns.
    // So that the app finishes its installation.
    BrowserThread::PostTask(
        BrowserThread::UI,
        FROM_HERE,
        base::Bind(&StartupAppLauncher::Launch, AsWeakPtr()));
    return;
  }

  LOG(ERROR) << "Failed to install app with error: " << error;
  OnLaunchFailure(KioskAppLaunchError::UNABLE_TO_INSTALL);
}

void StartupAppLauncher::OnNetworkWaitTimedout() {
  LOG(WARNING) << "OnNetworkWaitTimedout... connection = "
               <<  net::NetworkChangeNotifier::GetConnectionType();
  // Timeout in waiting for online. Try the install anyway.
  net::NetworkChangeNotifier::RemoveNetworkChangeObserver(this);
  BeginInstall();
}

void StartupAppLauncher::OnNetworkChanged(
    net::NetworkChangeNotifier::ConnectionType type) {
  DVLOG(1) << "OnNetworkChanged... connection = "
           <<  net::NetworkChangeNotifier::GetConnectionType();
  if (!net::NetworkChangeNotifier::IsOffline()) {
    DVLOG(1) << "Network up and running!";
    net::NetworkChangeNotifier::RemoveNetworkChangeObserver(this);
    network_wait_timer_.Stop();

    BeginInstall();
  } else {
    DVLOG(1) << "Network not running yet!";
  }
}

void StartupAppLauncher::OnKeyEvent(ui::KeyEvent* event) {
  if (event->type() != ui::ET_KEY_PRESSED)
    return;

  if (KioskAppManager::Get()->GetDisableBailoutShortcut())
    return;

  if (event->key_code() != ui::VKEY_S ||
      !(event->flags() & ui::EF_CONTROL_DOWN) ||
      !(event->flags() & ui::EF_ALT_DOWN)) {
    return;
  }

  OnLaunchFailure(KioskAppLaunchError::USER_CANCEL);
}

}   // namespace chromeos
