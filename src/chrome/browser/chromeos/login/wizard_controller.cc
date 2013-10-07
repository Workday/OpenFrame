// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/wizard_controller.h"

#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>

#include <string>
#include <vector>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "base/prefs/pref_registry_simple.h"
#include "base/prefs/pref_service.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "chrome/app/breakpad_linux.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chromeos/app_mode/kiosk_app_launcher.h"
#include "chrome/browser/chromeos/app_mode/kiosk_app_manager.h"
#include "chrome/browser/chromeos/customization_document.h"
#include "chrome/browser/chromeos/login/enrollment/enrollment_screen.h"
#include "chrome/browser/chromeos/login/existing_user_controller.h"
#include "chrome/browser/chromeos/login/helper.h"
#include "chrome/browser/chromeos/login/hwid_checker.h"
#include "chrome/browser/chromeos/login/login_display_host.h"
#include "chrome/browser/chromeos/login/login_utils.h"
#include "chrome/browser/chromeos/login/managed/locally_managed_user_creation_screen.h"
#include "chrome/browser/chromeos/login/oobe_display.h"
#include "chrome/browser/chromeos/login/screens/error_screen.h"
#include "chrome/browser/chromeos/login/screens/eula_screen.h"
#include "chrome/browser/chromeos/login/screens/kiosk_autolaunch_screen.h"
#include "chrome/browser/chromeos/login/screens/kiosk_enable_screen.h"
#include "chrome/browser/chromeos/login/screens/network_screen.h"
#include "chrome/browser/chromeos/login/screens/reset_screen.h"
#include "chrome/browser/chromeos/login/screens/terms_of_service_screen.h"
#include "chrome/browser/chromeos/login/screens/update_screen.h"
#include "chrome/browser/chromeos/login/screens/user_image_screen.h"
#include "chrome/browser/chromeos/login/screens/wrong_hwid_screen.h"
#include "chrome/browser/chromeos/login/startup_utils.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/net/network_portal_detector.h"
#include "chrome/browser/chromeos/policy/device_cloud_policy_manager_chromeos.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/browser/chromeos/settings/cros_settings_names.h"
#include "chrome/browser/policy/browser_policy_connector.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/options/options_util.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/pref_names.h"
#include "chromeos/chromeos_constants.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/session_manager_client.h"
#include "chromeos/network/network_state_handler.h"
#include "content/public/browser/browser_thread.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/l10n/l10n_util.h"

using content::BrowserThread;

namespace {

// A string pref with initial locale set in VPD or manifest.
const char kInitialLocale[] = "intl.initial_locale";

// A boolean pref of the OOBE complete flag (first OOBE part before login).
const char kOobeComplete[] = "OobeComplete";

// A boolean pref of the device registered flag (second part after first login).
const char kDeviceRegistered[] = "DeviceRegistered";

// Time in seconds that we wait for the device to reboot.
// If reboot didn't happen, ask user to reboot device manually.
const int kWaitForRebootTimeSec = 3;

// Interval in ms which is used for smooth screen showing.
static int kShowDelayMs = 400;

// Saves boolean "Local State" preference and forces its persistence to disk.
void SaveBoolPreferenceForced(const char* pref_name, bool value) {
  PrefService* prefs = g_browser_process->local_state();
  prefs->SetBoolean(pref_name, value);
  prefs->CommitPendingWrite();
}

// Saves integer "Local State" preference and forces its persistence to disk.
void SaveIntegerPreferenceForced(const char* pref_name, int value) {
  PrefService* prefs = g_browser_process->local_state();
  prefs->SetInteger(pref_name, value);
  prefs->CommitPendingWrite();
}

// Saves string "Local State" preference and forces its persistence to disk.
void SaveStringPreferenceForced(const char* pref_name,
                                const std::string& value) {
  PrefService* prefs = g_browser_process->local_state();
  prefs->SetString(pref_name, value);
  prefs->CommitPendingWrite();
}

}  // namespace

namespace chromeos {

const char WizardController::kNetworkScreenName[] = "network";
const char WizardController::kLoginScreenName[] = "login";
const char WizardController::kUpdateScreenName[] = "update";
const char WizardController::kUserImageScreenName[] = "image";
const char WizardController::kEulaScreenName[] = "eula";
const char WizardController::kEnrollmentScreenName[] = "enroll";
const char WizardController::kResetScreenName[] = "reset";
const char WizardController::kKioskEnableScreenName[] = "kiosk-enable";
const char WizardController::kKioskAutolaunchScreenName[] = "autolaunch";
const char WizardController::kErrorScreenName[] = "error-message";
const char WizardController::kTermsOfServiceScreenName[] = "tos";
const char WizardController::kWrongHWIDScreenName[] = "wrong-hwid";
const char WizardController::kLocallyManagedUserCreationScreenName[] =
  "locally-managed-user-creation-flow";

// Passing this parameter as a "first screen" initiates full OOBE flow.
const char WizardController::kOutOfBoxScreenName[] = "oobe";

// Special test value that commands not to create any window yet.
const char WizardController::kTestNoScreenName[] = "test:nowindow";

// Initialize default controller.
// static
WizardController* WizardController::default_controller_ = NULL;

// static
bool WizardController::skip_post_login_screens_ = false;

// static
bool WizardController::zero_delay_enabled_ = false;

///////////////////////////////////////////////////////////////////////////////
// WizardController, public:

WizardController::WizardController(chromeos::LoginDisplayHost* host,
                                   chromeos::OobeDisplay* oobe_display)
    : current_screen_(NULL),
      previous_screen_(NULL),
#if defined(GOOGLE_CHROME_BUILD)
      is_official_build_(true),
#else
      is_official_build_(false),
#endif
      is_out_of_box_(false),
      host_(host),
      oobe_display_(oobe_display),
      usage_statistics_reporting_(true),
      skip_update_enroll_after_eula_(false),
      login_screen_started_(false),
      user_image_screen_return_to_previous_hack_(false) {
  DCHECK(default_controller_ == NULL);
  default_controller_ = this;
}

WizardController::~WizardController() {
  if (default_controller_ == this) {
    default_controller_ = NULL;
  } else {
    NOTREACHED() << "More than one controller are alive.";
  }
}

void WizardController::Init(
    const std::string& first_screen_name,
    scoped_ptr<base::DictionaryValue> screen_parameters) {
  VLOG(1) << "Starting OOBE wizard with screen: " << first_screen_name;
  first_screen_name_ = first_screen_name;
  screen_parameters_ = screen_parameters.Pass();

  bool oobe_complete = StartupUtils::IsOobeCompleted();
  if (!oobe_complete || first_screen_name == kOutOfBoxScreenName)
    is_out_of_box_ = true;

  AdvanceToScreen(first_screen_name);
  if (!IsMachineHWIDCorrect() && !StartupUtils::IsDeviceRegistered() &&
      first_screen_name.empty())
    ShowWrongHWIDScreen();
}

chromeos::NetworkScreen* WizardController::GetNetworkScreen() {
  if (!network_screen_.get())
    network_screen_.reset(new chromeos::NetworkScreen(
        this, oobe_display_->GetNetworkScreenActor()));
  return network_screen_.get();
}

chromeos::UpdateScreen* WizardController::GetUpdateScreen() {
  if (!update_screen_.get()) {
    update_screen_.reset(new chromeos::UpdateScreen(
        this, oobe_display_->GetUpdateScreenActor()));
    update_screen_->SetRebootCheckDelay(kWaitForRebootTimeSec);
  }
  return update_screen_.get();
}

chromeos::UserImageScreen* WizardController::GetUserImageScreen() {
  if (!user_image_screen_.get())
    user_image_screen_.reset(
        new chromeos::UserImageScreen(
            this, oobe_display_->GetUserImageScreenActor()));
  return user_image_screen_.get();
}

chromeos::EulaScreen* WizardController::GetEulaScreen() {
  if (!eula_screen_.get())
    eula_screen_.reset(new chromeos::EulaScreen(
        this, oobe_display_->GetEulaScreenActor()));
  return eula_screen_.get();
}

chromeos::EnrollmentScreen*
    WizardController::GetEnrollmentScreen() {
  if (!enrollment_screen_.get()) {
    enrollment_screen_.reset(
        new chromeos::EnrollmentScreen(
            this, oobe_display_->GetEnrollmentScreenActor()));
  }
  return enrollment_screen_.get();
}

chromeos::ResetScreen* WizardController::GetResetScreen() {
  if (!reset_screen_.get()) {
    reset_screen_.reset(
        new chromeos::ResetScreen(this, oobe_display_->GetResetScreenActor()));
  }
  return reset_screen_.get();
}

chromeos::KioskEnableScreen* WizardController::GetKioskEnableScreen() {
  if (!kiosk_enable_screen_.get()) {
    kiosk_enable_screen_.reset(
        new chromeos::KioskEnableScreen(
            this,
            oobe_display_->GetKioskEnableScreenActor()));
  }
  return kiosk_enable_screen_.get();
}

chromeos::KioskAutolaunchScreen* WizardController::GetKioskAutolaunchScreen() {
  if (!autolaunch_screen_.get()) {
    autolaunch_screen_.reset(
        new chromeos::KioskAutolaunchScreen(
            this, oobe_display_->GetKioskAutolaunchScreenActor()));
  }
  return autolaunch_screen_.get();
}

chromeos::TermsOfServiceScreen* WizardController::GetTermsOfServiceScreen() {
  if (!terms_of_service_screen_.get()) {
    terms_of_service_screen_.reset(
        new chromeos::TermsOfServiceScreen(
            this, oobe_display_->GetTermsOfServiceScreenActor()));
  }
  return terms_of_service_screen_.get();
}

chromeos::WrongHWIDScreen* WizardController::GetWrongHWIDScreen() {
  if (!wrong_hwid_screen_.get()) {
    wrong_hwid_screen_.reset(
        new chromeos::WrongHWIDScreen(
            this, oobe_display_->GetWrongHWIDScreenActor()));
  }
  return wrong_hwid_screen_.get();
}

chromeos::LocallyManagedUserCreationScreen*
    WizardController::GetLocallyManagedUserCreationScreen() {
  if (!locally_managed_user_creation_screen_.get()) {
    locally_managed_user_creation_screen_.reset(
        new chromeos::LocallyManagedUserCreationScreen(
            this, oobe_display_->GetLocallyManagedUserCreationScreenActor()));
  }
  return locally_managed_user_creation_screen_.get();
}

void WizardController::ShowNetworkScreen() {
  VLOG(1) << "Showing network screen.";
  SetStatusAreaVisible(false);
  SetCurrentScreen(GetNetworkScreen());
}

void WizardController::ShowLoginScreen() {
  if (!time_eula_accepted_.is_null()) {
    base::TimeDelta delta = base::Time::Now() - time_eula_accepted_;
    UMA_HISTOGRAM_MEDIUM_TIMES("OOBE.EULAToSignInTime", delta);
  }
  VLOG(1) << "Showing login screen.";
  SetStatusAreaVisible(true);
  host_->StartSignInScreen();
  smooth_show_timer_.Stop();
  oobe_display_ = NULL;
  login_screen_started_ = true;
}

void WizardController::ResumeLoginScreen() {
  VLOG(1) << "Resuming login screen.";
  SetStatusAreaVisible(true);
  host_->ResumeSignInScreen();
  smooth_show_timer_.Stop();
  oobe_display_ = NULL;
}

void WizardController::ShowUpdateScreen() {
  VLOG(1) << "Showing update screen.";
  SetStatusAreaVisible(true);
  SetCurrentScreen(GetUpdateScreen());
}

void WizardController::ShowUserImageScreen() {
  const chromeos::UserManager* user_manager = chromeos::UserManager::Get();
  // Skip user image selection for public sessions and ephemeral logins.
  if (user_manager->IsLoggedInAsPublicAccount() ||
      user_manager->IsCurrentUserNonCryptohomeDataEphemeral()) {
    OnUserImageSkipped();
    return;
  }
  VLOG(1) << "Showing user image screen.";

  bool profile_picture_enabled = true;
  std::string user_id;
  if (screen_parameters_.get()) {
    screen_parameters_->GetBoolean("profile_picture_enabled",
        &profile_picture_enabled);
    screen_parameters_->GetString("user_id", &user_id);
  }

  // Status area has been already shown at sign in screen so it
  // doesn't make sense to hide it here and then show again at user session as
  // this produces undesired UX transitions.
  SetStatusAreaVisible(true);

  UserImageScreen* screen = GetUserImageScreen();
  if (!user_id.empty())
    screen->SetUserID(user_id);
  screen->SetProfilePictureEnabled(profile_picture_enabled);

  SetCurrentScreen(screen);
  host_->SetShutdownButtonEnabled(false);
}

void WizardController::ShowEulaScreen() {
  VLOG(1) << "Showing EULA screen.";
  SetStatusAreaVisible(false);
  SetCurrentScreen(GetEulaScreen());
}

void WizardController::ShowEnrollmentScreen() {
  SetStatusAreaVisible(true);

  bool is_auto_enrollment = false;
  std::string user;
  if (screen_parameters_.get()) {
    screen_parameters_->GetBoolean("is_auto_enrollment", &is_auto_enrollment);
    screen_parameters_->GetString("user", &user);
  }

  EnrollmentScreen* screen = GetEnrollmentScreen();
  screen->SetParameters(is_auto_enrollment,
                        !ShouldAutoStartEnrollment() || CanExitEnrollment(),
                        user);
  SetCurrentScreen(screen);
}

void WizardController::ShowResetScreen() {
  VLOG(1) << "Showing reset screen.";
  SetStatusAreaVisible(false);
  SetCurrentScreen(GetResetScreen());
}

void WizardController::ShowKioskEnableScreen() {
  VLOG(1) << "Showing kiosk enable screen.";
  SetStatusAreaVisible(false);
  SetCurrentScreen(GetKioskEnableScreen());
}

void WizardController::ShowKioskAutolaunchScreen() {
  VLOG(1) << "Showing kiosk autolaunch screen.";
  SetStatusAreaVisible(false);
  SetCurrentScreen(GetKioskAutolaunchScreen());
}

void WizardController::ShowTermsOfServiceScreen() {
  // Only show the Terms of Service when logging into a public account and Terms
  // of Service have been specified through policy. In all other cases, advance
  // to the user image screen immediately.
  if (!chromeos::UserManager::Get()->IsLoggedInAsPublicAccount() ||
      !ProfileManager::GetDefaultProfile()->GetPrefs()->IsManagedPreference(
          prefs::kTermsOfServiceURL)) {
    ShowUserImageScreen();
    return;
  }

  VLOG(1) << "Showing Terms of Service screen.";
  SetStatusAreaVisible(true);
  SetCurrentScreen(GetTermsOfServiceScreen());
}

void WizardController::ShowWrongHWIDScreen() {
  VLOG(1) << "Showing wrong HWID screen.";
  SetStatusAreaVisible(false);
  SetCurrentScreen(GetWrongHWIDScreen());
}

void WizardController::ShowLocallyManagedUserCreationScreen() {
  VLOG(1) << "Showing Locally managed user creation screen screen.";
  SetStatusAreaVisible(true);
  LocallyManagedUserCreationScreen* screen =
      GetLocallyManagedUserCreationScreen();
  SetCurrentScreen(screen);
}

void WizardController::SkipToLoginForTesting() {
  StartupUtils::MarkEulaAccepted();
  PerformPostEulaActions();
  PerformPostUpdateActions();
  ShowLoginScreen();
}

void WizardController::SkipPostLoginScreensForTesting() {
  skip_post_login_screens_ = true;
}

void WizardController::AddObserver(Observer* observer) {
  observer_list_.AddObserver(observer);
}

void WizardController::RemoveObserver(Observer* observer) {
  observer_list_.RemoveObserver(observer);
}

void WizardController::OnSessionStart() {
  FOR_EACH_OBSERVER(Observer, observer_list_, OnSessionStart());
}

void WizardController::SkipUpdateEnrollAfterEula() {
  skip_update_enroll_after_eula_ = true;
}

///////////////////////////////////////////////////////////////////////////////
// WizardController, ExitHandlers:
void WizardController::OnNetworkConnected() {
  if (is_official_build_) {
    if (!StartupUtils::IsEulaAccepted()) {
      ShowEulaScreen();
    } else {
      // Possible cases:
      // 1. EULA was accepted, forced shutdown/reboot during update.
      // 2. EULA was accepted, planned reboot after update.
      // Make sure that device is up-to-date.
      InitiateOOBEUpdate();
    }
  } else {
    InitiateOOBEUpdate();
  }
}

void WizardController::OnNetworkOffline() {
  // TODO(dpolukhin): if(is_out_of_box_) we cannot work offline and
  // should report some error message here and stay on the same screen.
  ShowLoginScreen();
}

void WizardController::OnConnectionFailed() {
  // TODO(dpolukhin): show error message after login screen is displayed.
  ShowLoginScreen();
}

void WizardController::OnUpdateCompleted() {
  OnOOBECompleted();
}

void WizardController::OnEulaAccepted() {
  time_eula_accepted_ = base::Time::Now();
  StartupUtils::MarkEulaAccepted();
  bool uma_enabled =
      OptionsUtil::ResolveMetricsReportingEnabled(usage_statistics_reporting_);

  CrosSettings::Get()->SetBoolean(kStatsReportingPref, uma_enabled);
  if (uma_enabled) {
#if defined(GOOGLE_CHROME_BUILD)
    // The crash reporter initialization needs IO to complete.
    base::ThreadRestrictions::ScopedAllowIO allow_io;
    InitCrashReporter();
#endif
  }

  if (skip_update_enroll_after_eula_) {
    PerformPostEulaActions();
    PerformPostUpdateActions();
    ShowEnrollmentScreen();
  } else {
    InitiateOOBEUpdate();
  }
}

void WizardController::OnUpdateErrorCheckingForUpdate() {
  // TODO(nkostylev): Update should be required during OOBE.
  // We do not want to block users from being able to proceed to the login
  // screen if there is any error checking for an update.
  // They could use "browse without sign-in" feature to set up the network to be
  // able to perform the update later.
  OnOOBECompleted();
}

void WizardController::OnUpdateErrorUpdating() {
  // If there was an error while getting or applying the update,
  // return to network selection screen.
  // TODO(nkostylev): Show message to the user explaining update error.
  // TODO(nkostylev): Update should be required during OOBE.
  // Temporary fix, need to migrate to new API. http://crosbug.com/4321
  OnOOBECompleted();
}

void WizardController::EnableUserImageScreenReturnToPreviousHack() {
  user_image_screen_return_to_previous_hack_ = true;
}

void WizardController::OnUserImageSelected() {
  if (user_image_screen_return_to_previous_hack_) {
    user_image_screen_return_to_previous_hack_ = false;
    DCHECK(previous_screen_);
    if (previous_screen_) {
      SetCurrentScreen(previous_screen_);
      return;
    }
  }
  // Launch browser and delete login host controller.
  BrowserThread::PostTask(
      BrowserThread::UI,
      FROM_HERE,
      base::Bind(&chromeos::LoginUtils::DoBrowserLaunch,
                 base::Unretained(chromeos::LoginUtils::Get()),
                 ProfileManager::GetDefaultProfile(), host_));
  host_ = NULL;
  // TODO(avayvod): Sync image with Google Sync.
}

void WizardController::OnUserImageSkipped() {
  OnUserImageSelected();
}

void WizardController::OnEnrollmentDone() {
  // Mark OOBE as completed only if enterprise enrollment was part of the
  // forced flow (i.e. app kiosk).
  if (ShouldAutoStartEnrollment())
    PerformPostUpdateActions();

  // TODO(mnissler): Unify the logic for auto-login for Public Sessions and
  // Kiosk Apps and make this code cover both cases: http://crbug.com/234694.
  if (KioskAppManager::Get()->IsAutoLaunchEnabled())
    AutoLaunchKioskApp();
  else
    ShowLoginScreen();
}

void WizardController::OnResetCanceled() {
  if (previous_screen_)
    SetCurrentScreen(previous_screen_);
  else
    ShowLoginScreen();
}

void WizardController::OnKioskAutolaunchCanceled() {
  ShowLoginScreen();
}

void WizardController::OnKioskAutolaunchConfirmed() {
  DCHECK(KioskAppManager::Get()->IsAutoLaunchEnabled());
  AutoLaunchKioskApp();
}

void WizardController::OnKioskEnableCompleted() {
  ShowLoginScreen();
}

void WizardController::OnWrongHWIDWarningSkipped() {
  if (previous_screen_)
    SetCurrentScreen(previous_screen_);
  else
    ShowLoginScreen();
}

void WizardController::OnAutoEnrollmentDone() {
  VLOG(1) << "Automagic enrollment done, resuming previous signin";
  ResumeLoginScreen();
}

void WizardController::OnOOBECompleted() {
  if (ShouldAutoStartEnrollment()) {
    ShowEnrollmentScreen();
  } else {
    PerformPostUpdateActions();
    ShowLoginScreen();
  }
}

void WizardController::OnTermsOfServiceDeclined() {
  // If the user declines the Terms of Service, end the session and return to
  // the login screen.
  DBusThreadManager::Get()->GetSessionManagerClient()->StopSession();
}

void WizardController::OnTermsOfServiceAccepted() {
  // If the user accepts the Terms of Service, advance to the user image screen.
  ShowUserImageScreen();
}

void WizardController::InitiateOOBEUpdate() {
  PerformPostEulaActions();
  SetCurrentScreenSmooth(GetUpdateScreen(), true);
  GetUpdateScreen()->StartNetworkCheck();
}

void WizardController::PerformPostEulaActions() {
  // Now that EULA has been accepted (for official builds), enable portal check.
  // ChromiumOS builds would go though this code path too.
  NetworkHandler::Get()->network_state_handler()->SetCheckPortalList(
      NetworkStateHandler::kDefaultCheckPortalList);
  host_->CheckForAutoEnrollment();
  host_->PrewarmAuthentication();
  NetworkPortalDetector* detector = NetworkPortalDetector::GetInstance();
  if (NetworkPortalDetector::IsEnabledInCommandLine() && detector)
    detector->Enable(true);
}

void WizardController::PerformPostUpdateActions() {
  StartupUtils::MarkOobeCompleted();
}

void WizardController::SetCurrentScreen(WizardScreen* new_current) {
  SetCurrentScreenSmooth(new_current, false);
}

void WizardController::ShowCurrentScreen() {
  // ShowCurrentScreen may get called by smooth_show_timer_ even after
  // flow has been switched to sign in screen (ExistingUserController).
  if (!oobe_display_)
    return;

  smooth_show_timer_.Stop();

  FOR_EACH_OBSERVER(Observer, observer_list_, OnScreenChanged(current_screen_));

  oobe_display_->ShowScreen(current_screen_);
}

void WizardController::SetCurrentScreenSmooth(WizardScreen* new_current,
                                              bool use_smoothing) {
  if (current_screen_ == new_current ||
      new_current == NULL ||
      oobe_display_ == NULL) {
    return;
  }

  smooth_show_timer_.Stop();

  if (current_screen_)
    oobe_display_->HideScreen(current_screen_);

  previous_screen_ = current_screen_;
  current_screen_ = new_current;

  if (use_smoothing) {
    smooth_show_timer_.Start(
        FROM_HERE,
        base::TimeDelta::FromMilliseconds(kShowDelayMs),
        this,
        &WizardController::ShowCurrentScreen);
  } else {
    ShowCurrentScreen();
  }
}

void WizardController::SetStatusAreaVisible(bool visible) {
  host_->SetStatusAreaVisible(visible);
}

void WizardController::AdvanceToScreenWithParams(
    const std::string& screen_name,
    base::DictionaryValue* screen_parameters) {
  screen_parameters_.reset(screen_parameters);
  AdvanceToScreen(screen_name);
}

void WizardController::AdvanceToScreen(const std::string& screen_name) {
  if (screen_name == kNetworkScreenName) {
    ShowNetworkScreen();
  } else if (screen_name == kLoginScreenName) {
    ShowLoginScreen();
  } else if (screen_name == kUpdateScreenName) {
    InitiateOOBEUpdate();
  } else if (screen_name == kUserImageScreenName) {
    ShowUserImageScreen();
  } else if (screen_name == kEulaScreenName) {
    ShowEulaScreen();
  } else if (screen_name == kResetScreenName) {
    ShowResetScreen();
  } else if (screen_name == kKioskEnableScreenName) {
    ShowKioskEnableScreen();
  } else if (screen_name == kKioskAutolaunchScreenName) {
    ShowKioskAutolaunchScreen();
  } else if (screen_name == kEnrollmentScreenName) {
    ShowEnrollmentScreen();
  } else if (screen_name == kTermsOfServiceScreenName) {
    ShowTermsOfServiceScreen();
  } else if (screen_name == kWrongHWIDScreenName) {
    ShowWrongHWIDScreen();
  } else if (screen_name == kLocallyManagedUserCreationScreenName) {
    ShowLocallyManagedUserCreationScreen();
  } else if (screen_name != kTestNoScreenName) {
    if (is_out_of_box_) {
      ShowNetworkScreen();
    } else {
      ShowLoginScreen();
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
// WizardController, chromeos::ScreenObserver overrides:
void WizardController::OnExit(ExitCodes exit_code) {
  LOG(INFO) << "Wizard screen exit code: " << exit_code;
  switch (exit_code) {
    case NETWORK_CONNECTED:
      OnNetworkConnected();
      break;
    case CONNECTION_FAILED:
      OnConnectionFailed();
      break;
    case UPDATE_INSTALLED:
    case UPDATE_NOUPDATE:
      OnUpdateCompleted();
      break;
    case UPDATE_ERROR_CHECKING_FOR_UPDATE:
      OnUpdateErrorCheckingForUpdate();
      break;
    case UPDATE_ERROR_UPDATING:
      OnUpdateErrorUpdating();
      break;
    case USER_IMAGE_SELECTED:
      OnUserImageSelected();
      break;
    case EULA_ACCEPTED:
      OnEulaAccepted();
      break;
    case EULA_BACK:
      ShowNetworkScreen();
      break;
    case ENTERPRISE_ENROLLMENT_COMPLETED:
      OnEnrollmentDone();
      break;
    case RESET_CANCELED:
      OnResetCanceled();
      break;
    case KIOSK_AUTOLAUNCH_CANCELED:
      OnKioskAutolaunchCanceled();
      break;
    case KIOSK_AUTOLAUNCH_CONFIRMED:
      OnKioskAutolaunchConfirmed();
      break;
    case KIOSK_ENABLE_COMPLETED:
      OnKioskEnableCompleted();
      break;
    case ENTERPRISE_AUTO_MAGIC_ENROLLMENT_COMPLETED:
      OnAutoEnrollmentDone();
      break;
    case TERMS_OF_SERVICE_DECLINED:
      OnTermsOfServiceDeclined();
      break;
    case TERMS_OF_SERVICE_ACCEPTED:
      OnTermsOfServiceAccepted();
      break;
    case WRONG_HWID_WARNING_SKIPPED:
      OnWrongHWIDWarningSkipped();
      break;
    default:
      NOTREACHED();
  }
}

void WizardController::OnSetUserNamePassword(const std::string& username,
                                             const std::string& password) {
  username_ = username;
  password_ = password;
}

void WizardController::SetUsageStatisticsReporting(bool val) {
  usage_statistics_reporting_ = val;
}

bool WizardController::GetUsageStatisticsReporting() const {
  return usage_statistics_reporting_;
}

chromeos::ErrorScreen* WizardController::GetErrorScreen() {
  if (!error_screen_.get()) {
    error_screen_.reset(
        new chromeos::ErrorScreen(this, oobe_display_->GetErrorScreenActor()));
  }
  return error_screen_.get();
}

void WizardController::ShowErrorScreen() {
  VLOG(1) << "Showing error screen.";
  SetCurrentScreen(GetErrorScreen());
}

void WizardController::HideErrorScreen(WizardScreen* parent_screen) {
  DCHECK(parent_screen);
  VLOG(1) << "Hiding error screen.";
  SetCurrentScreen(parent_screen);
}

void WizardController::AutoLaunchKioskApp() {
  KioskAppManager::App app_data;
  std::string app_id = KioskAppManager::Get()->GetAutoLaunchApp();
  CHECK(KioskAppManager::Get()->GetApp(app_id, &app_data));
  if (ExistingUserController::current_controller())
    ExistingUserController::current_controller()->PrepareKioskAppLaunch();

  // KioskAppLauncher deletes itself when done.
  (new KioskAppLauncher(KioskAppManager::Get(), app_id))->Start();
}

// static
bool WizardController::IsZeroDelayEnabled() {
  return zero_delay_enabled_;
}

// static
void WizardController::SetZeroDelays() {
  kShowDelayMs = 0;
  zero_delay_enabled_ = true;
}

bool WizardController::ShouldAutoStartEnrollment() const {
  return g_browser_process->browser_policy_connector()->
      GetDeviceCloudPolicyManager()->ShouldAutoStartEnrollment();
}

bool WizardController::CanExitEnrollment() const {
  return g_browser_process->browser_policy_connector()->
      GetDeviceCloudPolicyManager()->CanExitEnrollment();
}

}  // namespace chromeos
