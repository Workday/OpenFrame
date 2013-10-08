// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/chromeos/login/signin_screen_handler.h"

#include "base/callback.h"
#include "base/chromeos/chromeos_version.h"
#include "base/command_line.h"
#include "base/debug/trace_event.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/metrics/histogram.h"
#include "base/prefs/pref_registry_simple.h"
#include "base/prefs/pref_service.h"
#include "base/strings/string16.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browser_process_platform_part_chromeos.h"
#include "chrome/browser/browser_shutdown.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/chromeos/app_mode/kiosk_app_manager.h"
#include "chrome/browser/chromeos/input_method/input_method_util.h"
#include "chrome/browser/chromeos/kiosk_mode/kiosk_mode_settings.h"
#include "chrome/browser/chromeos/login/hwid_checker.h"
#include "chrome/browser/chromeos/login/login_display_host_impl.h"
#include "chrome/browser/chromeos/login/screen_locker.h"
#include "chrome/browser/chromeos/login/screens/core_oobe_actor.h"
#include "chrome/browser/chromeos/login/user.h"
#include "chrome/browser/chromeos/login/webui_login_display.h"
#include "chrome/browser/chromeos/login/wizard_controller.h"
#include "chrome/browser/chromeos/net/network_portal_detector.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/browser/io_thread.h"
#include "chrome/browser/policy/browser_policy_connector.h"
#include "chrome/browser/prefs/scoped_user_pref_update.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/chromeos/login/error_screen_handler.h"
#include "chrome/browser/ui/webui/chromeos/login/native_window_delegate.h"
#include "chrome/browser/ui/webui/chromeos/login/network_state_informer.h"
#include "chrome/browser/ui/webui/chromeos/login/oobe_ui.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "chromeos/chromeos_switches.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/power_manager_client.h"
#include "chromeos/ime/input_method_manager.h"
#include "chromeos/ime/xkeyboard.h"
#include "chromeos/network/network_state.h"
#include "chromeos/network/network_state_handler.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "google_apis/gaia/gaia_auth_util.h"
#include "google_apis/gaia/gaia_switches.h"
#include "google_apis/gaia/gaia_urls.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "third_party/cros_system_api/dbus/service_constants.h"
#include "ui/base/l10n/l10n_util.h"

#if defined(USE_AURA)
#include "ash/shell.h"
#include "ash/wm/lock_state_controller.h"
#endif

using content::BrowserThread;
using content::RenderViewHost;

namespace {

// User dictionary keys.
const char kKeyUsername[] = "username";
const char kKeyDisplayName[] = "displayName";
const char kKeyEmailAddress[] = "emailAddress";
const char kKeyEnterpriseDomain[] = "enterpriseDomain";
const char kKeyPublicAccount[] = "publicAccount";
const char kKeyLocallyManagedUser[] = "locallyManagedUser";
const char kKeySignedIn[] = "signedIn";
const char kKeyCanRemove[] = "canRemove";
const char kKeyIsOwner[] = "isOwner";
const char kKeyOauthTokenStatus[] = "oauthTokenStatus";

// Max number of users to show.
const size_t kMaxUsers = 18;

// Timeout to delay first notification about offline state for a
// current network.
const int kOfflineTimeoutSec = 5;

// Timeout used to prevent infinite connecting to a flaky network.
const int kConnectingTimeoutSec = 60;

// Type of the login screen UI that is currently presented to user.
const char kSourceGaiaSignin[] = "gaia-signin";
const char kSourceAccountPicker[] = "account-picker";

// The Task posted to PostTaskAndReply in StartClearingDnsCache on the IO
// thread.
void ClearDnsCache(IOThread* io_thread) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  if (browser_shutdown::IsTryingToQuit())
    return;

  io_thread->ClearHostCache();
}

static bool Contains(const std::vector<std::string>& container,
                     const std::string& value) {
  return std::find(container.begin(), container.end(), value) !=
         container.end();
}

}  // namespace

namespace chromeos {

namespace {

const char kNetworkStateOffline[] = "offline";
const char kNetworkStateOnline[] = "online";
const char kNetworkStateCaptivePortal[] = "behind captive portal";
const char kNetworkStateConnecting[] = "connecting";
const char kNetworkStateProxyAuthRequired[] = "proxy auth required";

const char kErrorReasonProxyAuthCancelled[] = "proxy auth cancelled";
const char kErrorReasonProxyAuthSupplied[] = "proxy auth supplied";
const char kErrorReasonProxyConnectionFailed[] = "proxy connection failed";
const char kErrorReasonProxyConfigChanged[] = "proxy config changed";
const char kErrorReasonLoadingTimeout[] = "loading timeout";
const char kErrorReasonPortalDetected[] = "portal detected";
const char kErrorReasonNetworkStateChanged[] = "network state changed";
const char kErrorReasonUpdate[] = "update";
const char kErrorReasonFrameError[] = "frame error";

const char* NetworkStateStatusString(NetworkStateInformer::State state) {
  switch (state) {
    case NetworkStateInformer::OFFLINE:
      return kNetworkStateOffline;
    case NetworkStateInformer::ONLINE:
      return kNetworkStateOnline;
    case NetworkStateInformer::CAPTIVE_PORTAL:
      return kNetworkStateCaptivePortal;
    case NetworkStateInformer::CONNECTING:
      return kNetworkStateConnecting;
    case NetworkStateInformer::PROXY_AUTH_REQUIRED:
      return kNetworkStateProxyAuthRequired;
    default:
      NOTREACHED();
      return NULL;
  }
}

const char* ErrorReasonString(ErrorScreenActor::ErrorReason reason) {
  switch (reason) {
    case ErrorScreenActor::ERROR_REASON_PROXY_AUTH_CANCELLED:
      return kErrorReasonProxyAuthCancelled;
    case ErrorScreenActor::ERROR_REASON_PROXY_AUTH_SUPPLIED:
      return kErrorReasonProxyAuthSupplied;
    case ErrorScreenActor::ERROR_REASON_PROXY_CONNECTION_FAILED:
      return kErrorReasonProxyConnectionFailed;
    case ErrorScreenActor::ERROR_REASON_PROXY_CONFIG_CHANGED:
      return kErrorReasonProxyConfigChanged;
    case ErrorScreenActor::ERROR_REASON_LOADING_TIMEOUT:
      return kErrorReasonLoadingTimeout;
    case ErrorScreenActor::ERROR_REASON_PORTAL_DETECTED:
      return kErrorReasonPortalDetected;
    case ErrorScreenActor::ERROR_REASON_NETWORK_STATE_CHANGED:
      return kErrorReasonNetworkStateChanged;
    case ErrorScreenActor::ERROR_REASON_UPDATE:
      return kErrorReasonUpdate;
    case ErrorScreenActor::ERROR_REASON_FRAME_ERROR:
      return kErrorReasonFrameError;
    default:
      NOTREACHED();
      return NULL;
  }
}

// Updates params dictionary passed to the auth extension with related
// preferences from CrosSettings.
void UpdateAuthParamsFromSettings(DictionaryValue* params,
                                  const CrosSettings* cros_settings) {
  bool allow_new_user = true;
  cros_settings->GetBoolean(kAccountsPrefAllowNewUser, &allow_new_user);
  bool allow_guest = true;
  cros_settings->GetBoolean(kAccountsPrefAllowGuest, &allow_guest);
  // Account creation depends on Guest sign-in (http://crosbug.com/24570).
  params->SetBoolean("createAccount", allow_new_user && allow_guest);
  params->SetBoolean("guestSignin", allow_guest);
}

bool IsOnline(NetworkStateInformer::State state,
              ErrorScreenActor::ErrorReason reason) {
  return state == NetworkStateInformer::ONLINE &&
      reason != ErrorScreenActor::ERROR_REASON_PORTAL_DETECTED &&
      reason != ErrorScreenActor::ERROR_REASON_LOADING_TIMEOUT;
}

bool IsUnderCaptivePortal(NetworkStateInformer::State state,
                          ErrorScreenActor::ErrorReason reason) {
  return state == NetworkStateInformer::CAPTIVE_PORTAL ||
      reason == ErrorScreenActor::ERROR_REASON_PORTAL_DETECTED;
}

bool IsProxyError(NetworkStateInformer::State state,
                  ErrorScreenActor::ErrorReason reason,
                  net::Error frame_error) {
  return state == NetworkStateInformer::PROXY_AUTH_REQUIRED ||
      reason == ErrorScreenActor::ERROR_REASON_PROXY_AUTH_CANCELLED ||
      reason == ErrorScreenActor::ERROR_REASON_PROXY_CONNECTION_FAILED ||
      (reason == ErrorScreenActor::ERROR_REASON_FRAME_ERROR &&
       (frame_error == net::ERR_PROXY_CONNECTION_FAILED ||
        frame_error == net::ERR_TUNNEL_CONNECTION_FAILED));
}

bool IsSigninScreen(const OobeUI::Screen screen) {
  return screen == OobeUI::SCREEN_GAIA_SIGNIN ||
      screen == OobeUI::SCREEN_ACCOUNT_PICKER;
}

bool IsSigninScreenError(ErrorScreen::ErrorState error_state) {
  return error_state == ErrorScreen::ERROR_STATE_PORTAL ||
      error_state == ErrorScreen::ERROR_STATE_OFFLINE ||
      error_state == ErrorScreen::ERROR_STATE_PROXY ||
      error_state == ErrorScreen::ERROR_STATE_AUTH_EXT_TIMEOUT;
}

// Returns network name by service path.
std::string GetNetworkName(const std::string& service_path) {
  const NetworkState* network = NetworkHandler::Get()->network_state_handler()->
      GetNetworkState(service_path);
  if (!network)
    return std::string();
  return network->name();
}

// Returns captive portal state for a network by its service path.
NetworkPortalDetector::CaptivePortalState GetCaptivePortalState(
    const std::string& service_path) {
  NetworkPortalDetector* detector = NetworkPortalDetector::GetInstance();
  const NetworkState* network = NetworkHandler::Get()->network_state_handler()->
      GetNetworkState(service_path);
  if (!detector || !network)
    return NetworkPortalDetector::CaptivePortalState();
  return detector->GetCaptivePortalState(network);
}

void RecordDiscrepancyWithShill(
    const NetworkState* network,
    const NetworkPortalDetector::CaptivePortalStatus status) {
  if (network->connection_state() == flimflam::kStateOnline) {
    UMA_HISTOGRAM_ENUMERATION(
        "CaptivePortal.OOBE.DiscrepancyWithShill_Online",
        status,
        NetworkPortalDetector::CAPTIVE_PORTAL_STATUS_COUNT);
  } else if (network->connection_state() == flimflam::kStatePortal) {
    UMA_HISTOGRAM_ENUMERATION(
        "CaptivePortal.OOBE.DiscrepancyWithShill_RestrictedPool",
        status,
        NetworkPortalDetector::CAPTIVE_PORTAL_STATUS_COUNT);
  } else {
    UMA_HISTOGRAM_ENUMERATION(
        "CaptivePortal.OOBE.DiscrepancyWithShill_Offline",
        status,
        NetworkPortalDetector::CAPTIVE_PORTAL_STATUS_COUNT);
  }
}

// Record state and descripancies with shill (e.g. shill thinks that
// network is online but NetworkPortalDetector claims that it's behind
// portal) for the network identified by |service_path|.
void RecordNetworkPortalDetectorStats(const std::string& service_path) {
  const NetworkState* network = NetworkHandler::Get()->network_state_handler()->
      GetNetworkState(service_path);
  if (!network)
    return;
  NetworkPortalDetector::CaptivePortalState state =
      GetCaptivePortalState(service_path);
  if (state.status == NetworkPortalDetector::CAPTIVE_PORTAL_STATUS_UNKNOWN)
    return;

  UMA_HISTOGRAM_ENUMERATION("CaptivePortal.OOBE.DetectionResult",
                            state.status,
                            NetworkPortalDetector::CAPTIVE_PORTAL_STATUS_COUNT);

  switch (state.status) {
    case NetworkPortalDetector::CAPTIVE_PORTAL_STATUS_UNKNOWN:
      NOTREACHED();
      break;
    case NetworkPortalDetector::CAPTIVE_PORTAL_STATUS_OFFLINE:
      if (network->connection_state() == flimflam::kStateOnline ||
          network->connection_state() == flimflam::kStatePortal)
        RecordDiscrepancyWithShill(network, state.status);
      break;
    case NetworkPortalDetector::CAPTIVE_PORTAL_STATUS_ONLINE:
      if (network->connection_state() != flimflam::kStateOnline)
        RecordDiscrepancyWithShill(network, state.status);
      break;
    case NetworkPortalDetector::CAPTIVE_PORTAL_STATUS_PORTAL:
      if (network->connection_state() != flimflam::kStatePortal)
        RecordDiscrepancyWithShill(network, state.status);
      break;
    case NetworkPortalDetector::CAPTIVE_PORTAL_STATUS_PROXY_AUTH_REQUIRED:
      if (network->connection_state() != flimflam::kStateOnline)
        RecordDiscrepancyWithShill(network, state.status);
      break;
    case NetworkPortalDetector::CAPTIVE_PORTAL_STATUS_COUNT:
      NOTREACHED();
      break;
  }
}

static bool SetUserInputMethodImpl(
    const std::string& username,
    chromeos::input_method::InputMethodManager* manager) {
  PrefService* const local_state = g_browser_process->local_state();

  const base::DictionaryValue* users_lru_input_methods =
      local_state->GetDictionary(prefs::kUsersLRUInputMethod);

  if (users_lru_input_methods == NULL) {
    DLOG(WARNING) << "SetUserInputMethod('" << username
                  << "'): no kUsersLRUInputMethod";
    return false;
  }

  std::string input_method;

  if (!users_lru_input_methods->GetStringWithoutPathExpansion(username,
                                                              &input_method)) {
    DLOG(INFO) << "SetUserInputMethod('" << username
               << "'): no input method for this user";
    return false;
  }

  if (input_method.empty())
    return false;

  if (!manager->IsFullLatinKeyboard(input_method)) {
    LOG(WARNING) << "SetUserInputMethod('" << username
                 << "'): stored user LRU input method '" << input_method
                 << "' is no longer Full Latin Keyboard Language"
                 << " (entry dropped). Use hardware default instead.";

    DictionaryPrefUpdate updater(local_state, prefs::kUsersLRUInputMethod);

    base::DictionaryValue* const users_lru_input_methods = updater.Get();
    if (users_lru_input_methods != NULL) {
      users_lru_input_methods->SetStringWithoutPathExpansion(username, "");
    }
    return false;
  }

  if (!Contains(manager->GetActiveInputMethodIds(), input_method)) {
    if (!manager->EnableInputMethod(input_method)) {
      DLOG(ERROR) << "SigninScreenHandler::SetUserInputMethod('" << username
                  << "'): user input method '" << input_method
                  << "' is not enabled and enabling failed (ignored!).";
    }
  }
  manager->ChangeInputMethod(input_method);

  return true;
}

}  // namespace

// SigninScreenHandler implementation ------------------------------------------

SigninScreenHandler::SigninScreenHandler(
    const scoped_refptr<NetworkStateInformer>& network_state_informer,
    ErrorScreenActor* error_screen_actor,
    CoreOobeActor* core_oobe_actor)
    : ui_state_(UI_STATE_UNKNOWN),
      frame_state_(FRAME_STATE_UNKNOWN),
      frame_error_(net::OK),
      delegate_(NULL),
      native_window_delegate_(NULL),
      show_on_init_(false),
      oobe_ui_(false),
      focus_stolen_(false),
      gaia_silent_load_(false),
      is_account_picker_showing_first_time_(false),
      dns_cleared_(false),
      dns_clear_task_running_(false),
      cookies_cleared_(false),
      network_state_informer_(network_state_informer),
      test_expects_complete_login_(false),
      weak_factory_(this),
      webui_visible_(false),
      preferences_changed_delayed_(false),
      error_screen_actor_(error_screen_actor),
      core_oobe_actor_(core_oobe_actor),
      is_first_update_state_call_(true),
      offline_login_active_(false),
      last_network_state_(NetworkStateInformer::UNKNOWN),
      has_pending_auth_ui_(false) {
  DCHECK(network_state_informer_.get());
  DCHECK(error_screen_actor_);
  DCHECK(core_oobe_actor_);
  network_state_informer_->AddObserver(this);
  CrosSettings::Get()->AddSettingsObserver(kAccountsPrefAllowNewUser, this);
  CrosSettings::Get()->AddSettingsObserver(kAccountsPrefAllowGuest, this);

  registrar_.Add(this,
                 chrome::NOTIFICATION_AUTH_NEEDED,
                 content::NotificationService::AllSources());
  registrar_.Add(this,
                 chrome::NOTIFICATION_AUTH_SUPPLIED,
                 content::NotificationService::AllSources());
  registrar_.Add(this,
                 chrome::NOTIFICATION_AUTH_CANCELLED,
                 content::NotificationService::AllSources());
}

SigninScreenHandler::~SigninScreenHandler() {
  weak_factory_.InvalidateWeakPtrs();
  SystemKeyEventListener* key_event_listener =
      SystemKeyEventListener::GetInstance();
  if (key_event_listener)
    key_event_listener->RemoveCapsLockObserver(this);
  if (delegate_)
    delegate_->SetWebUIHandler(NULL);
  network_state_informer_->RemoveObserver(this);
  CrosSettings::Get()->RemoveSettingsObserver(kAccountsPrefAllowNewUser, this);
  CrosSettings::Get()->RemoveSettingsObserver(kAccountsPrefAllowGuest, this);
}

void SigninScreenHandler::DeclareLocalizedValues(
    LocalizedValuesBuilder* builder) {
  builder->Add("signinScreenTitle", IDS_SIGNIN_SCREEN_TITLE);
  builder->Add("signinScreenPasswordChanged",
               IDS_SIGNIN_SCREEN_PASSWORD_CHANGED);
  builder->Add("passwordHint", IDS_LOGIN_POD_EMPTY_PASSWORD_TEXT);
  builder->Add("podMenuButtonAccessibleName",
               IDS_LOGIN_POD_MENU_BUTTON_ACCESSIBLE_NAME);
  builder->Add("podMenuRemoveItemAccessibleName",
               IDS_LOGIN_POD_MENU_REMOVE_ITEM_ACCESSIBLE_NAME);
  builder->Add("passwordFieldAccessibleName",
               IDS_LOGIN_POD_PASSWORD_FIELD_ACCESSIBLE_NAME);
  builder->Add("signedIn", IDS_SCREEN_LOCK_ACTIVE_USER);
  builder->Add("signinButton", IDS_LOGIN_BUTTON);
  builder->Add("shutDown", IDS_SHUTDOWN_BUTTON);
  builder->Add("addUser", IDS_ADD_USER_BUTTON);
  builder->Add("cancelUserAdding", IDS_CANCEL_USER_ADDING);
  builder->Add("browseAsGuest", IDS_GO_INCOGNITO_BUTTON);
  builder->Add("cancel", IDS_CANCEL);
  builder->Add("signOutUser", IDS_SCREEN_LOCK_SIGN_OUT);
  builder->Add("createAccount", IDS_CREATE_ACCOUNT_HTML);
  builder->Add("guestSignin", IDS_BROWSE_WITHOUT_SIGNING_IN_HTML);
  builder->Add("createLocallyManagedUser",
               IDS_CREATE_LOCALLY_MANAGED_USER_HTML);
  builder->Add("createManagedUserFeatureName",
               IDS_CREATE_LOCALLY_MANAGED_USER_FEATURE_NAME);
  builder->Add("offlineLogin", IDS_OFFLINE_LOGIN_HTML);
  builder->Add("ownerUserPattern", IDS_LOGIN_POD_OWNER_USER);
  builder->Add("removeUser", IDS_LOGIN_POD_REMOVE_USER);
  builder->Add("errorTpmFailureTitle", IDS_LOGIN_ERROR_TPM_FAILURE_TITLE);
  builder->Add("errorTpmFailureReboot", IDS_LOGIN_ERROR_TPM_FAILURE_REBOOT);
  builder->Add("errorTpmFailureRebootButton",
               IDS_LOGIN_ERROR_TPM_FAILURE_REBOOT_BUTTON);
  builder->Add(
      "disabledAddUserTooltip",
      g_browser_process->browser_policy_connector()->IsEnterpriseManaged() ?
          IDS_DISABLED_ADD_USER_TOOLTIP_ENTERPRISE :
          IDS_DISABLED_ADD_USER_TOOLTIP);

  // Strings used by password changed dialog.
  builder->Add("passwordChangedTitle", IDS_LOGIN_PASSWORD_CHANGED_TITLE);
  builder->Add("passwordChangedDesc", IDS_LOGIN_PASSWORD_CHANGED_DESC);
  builder->AddF("passwordChangedMoreInfo",
                IDS_LOGIN_PASSWORD_CHANGED_MORE_INFO,
                IDS_SHORT_PRODUCT_OS_NAME);

  builder->Add("oldPasswordHint", IDS_LOGIN_PASSWORD_CHANGED_OLD_PASSWORD_HINT);
  builder->Add("oldPasswordIncorrect",
               IDS_LOGIN_PASSWORD_CHANGED_INCORRECT_OLD_PASSWORD);
  builder->Add("passwordChangedCantRemember",
               IDS_LOGIN_PASSWORD_CHANGED_CANT_REMEMBER);
  builder->Add("passwordChangedBackButton",
               IDS_LOGIN_PASSWORD_CHANGED_BACK_BUTTON);
  builder->Add("passwordChangedsOkButton", IDS_OK);
  builder->Add("passwordChangedProceedAnyway",
               IDS_LOGIN_PASSWORD_CHANGED_PROCEED_ANYWAY);
  builder->Add("proceedAnywayButton",
               IDS_LOGIN_PASSWORD_CHANGED_PROCEED_ANYWAY_BUTTON);
  builder->Add("publicAccountInfoFormat", IDS_LOGIN_PUBLIC_ACCOUNT_INFO_FORMAT);
  builder->Add("publicAccountReminder",
               IDS_LOGIN_PUBLIC_ACCOUNT_SIGNOUT_REMINDER);
  builder->Add("publicAccountEnter", IDS_LOGIN_PUBLIC_ACCOUNT_ENTER);
  builder->Add("publicAccountEnterAccessibleName",
               IDS_LOGIN_PUBLIC_ACCOUNT_ENTER_ACCESSIBLE_NAME);
  builder->AddF("removeUserWarningText",
               IDS_LOGIN_POD_USER_REMOVE_WARNING,
               UTF8ToUTF16(chrome::kSupervisedUserManagementDisplayURL));
  builder->Add("removeUserWarningButtonTitle",
               IDS_LOGIN_POD_USER_REMOVE_WARNING_BUTTON);

  if (chromeos::KioskModeSettings::Get()->IsKioskModeEnabled())
    builder->Add("demoLoginMessage", IDS_KIOSK_MODE_LOGIN_MESSAGE);
}

void SigninScreenHandler::Show(bool oobe_ui) {
  CHECK(delegate_);
  oobe_ui_ = oobe_ui;
  if (!page_is_ready()) {
    show_on_init_ = true;
    return;
  }

  if (oobe_ui) {
    // Shows new user sign-in for OOBE.
    HandleShowAddUser(NULL);
  } else {
    // Populates account picker. Animation is turned off for now until we
    // figure out how to make it fast enough.
    SendUserList(false);

    // Reset Caps Lock state when login screen is shown.
    input_method::InputMethodManager::Get()->GetXKeyboard()->
        SetCapsLockEnabled(false);

    DictionaryValue params;
    params.SetBoolean("disableAddUser", AllWhitelistedUsersPresent());
    UpdateUIState(UI_STATE_ACCOUNT_PICKER, &params);
  }
}

void SigninScreenHandler::ShowRetailModeLoginSpinner() {
  CallJS("showLoginSpinner");
}

void SigninScreenHandler::SetDelegate(SigninScreenHandlerDelegate* delegate) {
  delegate_ = delegate;
  if (delegate_)
    delegate_->SetWebUIHandler(this);
}

void SigninScreenHandler::SetNativeWindowDelegate(
    NativeWindowDelegate* native_window_delegate) {
  native_window_delegate_ = native_window_delegate;
}

void SigninScreenHandler::OnNetworkReady() {
  MaybePreloadAuthExtension();
}

void SigninScreenHandler::UpdateState(ErrorScreenActor::ErrorReason reason) {
  UpdateStateInternal(reason, false);
}

// SigninScreenHandler, private: -----------------------------------------------

void SigninScreenHandler::UpdateUIState(UIState ui_state,
                                        DictionaryValue* params) {
  switch (ui_state) {
    case UI_STATE_GAIA_SIGNIN:
      ui_state_ = UI_STATE_GAIA_SIGNIN;
      ShowScreen(OobeUI::kScreenGaiaSignin, params);
      break;
    case UI_STATE_ACCOUNT_PICKER:
      ui_state_ = UI_STATE_ACCOUNT_PICKER;
      ShowScreen(OobeUI::kScreenAccountPicker, params);
      break;
    default:
      NOTREACHED();
      break;
  }
}

// TODO (ygorshenin@): split this method into small parts.
void SigninScreenHandler::UpdateStateInternal(
    ErrorScreenActor::ErrorReason reason,
    bool force_update) {
  // Do nothing once user has signed in or sign in is in progress.
  // TODO(ygorshenin): We will end up here when processing network state
  // notification but no ShowSigninScreen() was called so delegate_ will be
  // NULL. Network state processing logic does not belong here.
  if (delegate_ &&
      (delegate_->IsUserSigninCompleted() || delegate_->IsSigninInProgress())) {
    return;
  }

  NetworkStateInformer::State state = network_state_informer_->state();
  const std::string network_path = network_state_informer_->network_path();
  const std::string network_name = GetNetworkName(network_path);

  // Skip "update" notification about OFFLINE state from
  // NetworkStateInformer if previous notification already was
  // delayed.
  if ((state == NetworkStateInformer::OFFLINE || has_pending_auth_ui_) &&
      !force_update &&
      !update_state_closure_.IsCancelled()) {
    return;
  }

  // TODO (ygorshenin@): switch log level to INFO once signin screen
  // will be tested well.
  LOG(WARNING) << "SigninScreenHandler::UpdateStateInternal(): "
               << "state=" << NetworkStateStatusString(state) << ", "
               << "network_name=" << network_name << ", "
               << "reason=" << ErrorReasonString(reason) << ", "
               << "force_update=" << force_update;
  update_state_closure_.Cancel();

  if ((state == NetworkStateInformer::OFFLINE && !force_update) ||
      has_pending_auth_ui_) {
    update_state_closure_.Reset(
        base::Bind(
            &SigninScreenHandler::UpdateStateInternal,
            weak_factory_.GetWeakPtr(), reason, true));
    base::MessageLoop::current()->PostDelayedTask(
        FROM_HERE,
        update_state_closure_.callback(),
        base::TimeDelta::FromSeconds(kOfflineTimeoutSec));
    return;
  }

  // Don't show or hide error screen if we're in connecting state.
  if (state == NetworkStateInformer::CONNECTING && !force_update) {
    if (connecting_closure_.IsCancelled()) {
      // First notification about CONNECTING state.
      connecting_closure_.Reset(
          base::Bind(&SigninScreenHandler::UpdateStateInternal,
                     weak_factory_.GetWeakPtr(), reason, true));
      base::MessageLoop::current()->PostDelayedTask(
          FROM_HERE,
          connecting_closure_.callback(),
          base::TimeDelta::FromSeconds(kConnectingTimeoutSec));
    }
    return;
  }
  connecting_closure_.Cancel();

  const bool is_online = IsOnline(state, reason);
  const bool is_under_captive_portal = IsUnderCaptivePortal(state, reason);
  const bool is_gaia_loading_timeout =
      (reason == ErrorScreenActor::ERROR_REASON_LOADING_TIMEOUT);
  const bool is_gaia_error = frame_error_ != net::OK &&
      frame_error_ != net::ERR_NETWORK_CHANGED;
  const bool is_gaia_signin = IsGaiaVisible() || IsGaiaHiddenByError();
  const bool error_screen_should_overlay =
      !offline_login_active_ && IsGaiaVisible();
  const bool from_not_online_to_online_transition =
      is_online && last_network_state_ != NetworkStateInformer::ONLINE;
  last_network_state_ = state;

  if (is_online || !is_under_captive_portal)
    error_screen_actor_->HideCaptivePortal();

  // Hide offline message (if needed) and return if current screen is
  // not a Gaia frame.
  if (!is_gaia_signin) {
    if (!IsSigninScreenHiddenByError())
      HideOfflineMessage(state, reason);
    return;
  }

  // Reload frame if network state is changed from {!ONLINE} -> ONLINE state.
  if (reason == ErrorScreenActor::ERROR_REASON_NETWORK_STATE_CHANGED &&
      from_not_online_to_online_transition) {
    // Schedules a immediate retry.
    LOG(WARNING) << "Retry page load since network has been changed.";
    ReloadGaiaScreen();
  }

  if (reason == ErrorScreenActor::ERROR_REASON_PROXY_CONFIG_CHANGED &&
      error_screen_should_overlay) {
    // Schedules a immediate retry.
    LOG(WARNING) << "Retry page load since proxy settings has been changed.";
    ReloadGaiaScreen();
  }

  if (reason == ErrorScreenActor::ERROR_REASON_FRAME_ERROR &&
      !IsProxyError(state, reason, frame_error_)) {
    LOG(WARNING) << "Retry page load due to reason: "
                 << ErrorReasonString(reason);
    ReloadGaiaScreen();
  }

  if ((!is_online || is_gaia_loading_timeout || is_gaia_error) &&
      !offline_login_active_) {
    SetupAndShowOfflineMessage(state, reason);
  } else {
    HideOfflineMessage(state, reason);
  }
}

void SigninScreenHandler::SetupAndShowOfflineMessage(
    NetworkStateInformer:: State state,
    ErrorScreenActor::ErrorReason reason) {
  const std::string network_path = network_state_informer_->network_path();
  const bool is_under_captive_portal = IsUnderCaptivePortal(state, reason);
  const bool is_proxy_error = IsProxyError(state, reason, frame_error_);
  const bool is_gaia_loading_timeout =
      (reason == ErrorScreenActor::ERROR_REASON_LOADING_TIMEOUT);

  // Record portal detection stats only if we're going to show or
  // change state of the error screen.
  RecordNetworkPortalDetectorStats(network_path);

  if (is_proxy_error) {
    error_screen_actor_->SetErrorState(ErrorScreen::ERROR_STATE_PROXY,
                                       std::string());
  } else if (is_under_captive_portal) {
    // Do not bother a user with obsessive captive portal showing. This
    // check makes captive portal being shown only once: either when error
    // screen is shown for the first time or when switching from another
    // error screen (offline, proxy).
    if (IsGaiaVisible() ||
        (error_screen_actor_->error_state() !=
         ErrorScreen::ERROR_STATE_PORTAL)) {
      error_screen_actor_->FixCaptivePortal();
    }
    const std::string network_name = GetNetworkName(network_path);
    error_screen_actor_->SetErrorState(ErrorScreen::ERROR_STATE_PORTAL,
                                       network_name);
  } else if (is_gaia_loading_timeout) {
    error_screen_actor_->SetErrorState(
        ErrorScreen::ERROR_STATE_AUTH_EXT_TIMEOUT, std::string());
  } else {
    error_screen_actor_->SetErrorState(ErrorScreen::ERROR_STATE_OFFLINE,
                                       std::string());
  }

  const bool guest_signin_allowed = IsGuestSigninAllowed() &&
      IsSigninScreenError(error_screen_actor_->error_state());
  error_screen_actor_->AllowGuestSignin(guest_signin_allowed);

  const bool offline_login_allowed = IsOfflineLoginAllowed() &&
      IsSigninScreenError(error_screen_actor_->error_state()) &&
      error_screen_actor_->error_state() !=
      ErrorScreen::ERROR_STATE_AUTH_EXT_TIMEOUT;
  error_screen_actor_->AllowOfflineLogin(offline_login_allowed);

  if (GetCurrentScreen() != OobeUI::SCREEN_ERROR_MESSAGE) {
    DictionaryValue params;
    const std::string network_type = network_state_informer_->network_type();
    params.SetString("lastNetworkType", network_type);
    error_screen_actor_->SetUIState(ErrorScreen::UI_STATE_SIGNIN);
    error_screen_actor_->Show(OobeUI::SCREEN_GAIA_SIGNIN, &params);
  }
}

void SigninScreenHandler::HideOfflineMessage(
    NetworkStateInformer::State state,
    ErrorScreenActor::ErrorReason reason) {
  if (!IsSigninScreenHiddenByError())
    return;

  error_screen_actor_->Hide();

  // Forces a reload for Gaia screen on hiding error message.
  if (IsGaiaVisible() || IsGaiaHiddenByError())
    ReloadGaiaScreen();
}

void SigninScreenHandler::ReloadGaiaScreen() {
  if (frame_state_ == FRAME_STATE_LOADING)
    return;
  NetworkStateInformer::State state = network_state_informer_->state();
  if (state != NetworkStateInformer::ONLINE) {
    LOG(WARNING) << "Skipping reload of auth extension frame since "
                 << "network state=" << NetworkStateStatusString(state);
    return;
  }
  LOG(WARNING) << "Reload auth extension frame.";
  frame_state_ = FRAME_STATE_LOADING;
  CallJS("login.GaiaSigninScreen.doReload");
}

void SigninScreenHandler::Initialize() {
  // If delegate_ is NULL here (e.g. WebUIScreenLocker has been destroyed),
  // don't do anything, just return.
  if (!delegate_)
    return;

  // Register for Caps Lock state change notifications;
  SystemKeyEventListener* key_event_listener =
      SystemKeyEventListener::GetInstance();
  if (key_event_listener)
    key_event_listener->AddCapsLockObserver(this);

  if (show_on_init_) {
    show_on_init_ = false;
    Show(oobe_ui_);
  }
}

gfx::NativeWindow SigninScreenHandler::GetNativeWindow() {
  if (native_window_delegate_)
    return native_window_delegate_->GetNativeWindow();
  return NULL;
}

void SigninScreenHandler::RegisterMessages() {
  AddCallback("authenticateUser", &SigninScreenHandler::HandleAuthenticateUser);
  AddCallback("completeLogin", &SigninScreenHandler::HandleCompleteLogin);
  AddCallback("completeAuthentication",
              &SigninScreenHandler::HandleCompleteAuthentication);
  AddCallback("getUsers", &SigninScreenHandler::HandleGetUsers);
  AddCallback("launchDemoUser", &SigninScreenHandler::HandleLaunchDemoUser);
  AddCallback("launchIncognito", &SigninScreenHandler::HandleLaunchIncognito);
  AddCallback("showLocallyManagedUserCreationScreen",
              &SigninScreenHandler::HandleShowLocallyManagedUserCreationScreen);
  AddCallback("launchPublicAccount",
              &SigninScreenHandler::HandleLaunchPublicAccount);
  AddRawCallback("offlineLogin", &SigninScreenHandler::HandleOfflineLogin);
  AddCallback("rebootSystem", &SigninScreenHandler::HandleRebootSystem);
  AddRawCallback("showAddUser", &SigninScreenHandler::HandleShowAddUser);
  AddCallback("shutdownSystem", &SigninScreenHandler::HandleShutdownSystem);
  AddCallback("loadWallpaper", &SigninScreenHandler::HandleLoadWallpaper);
  AddCallback("removeUser", &SigninScreenHandler::HandleRemoveUser);
  AddCallback("toggleEnrollmentScreen",
              &SigninScreenHandler::HandleToggleEnrollmentScreen);
  AddCallback("toggleKioskEnableScreen",
              &SigninScreenHandler::HandleToggleKioskEnableScreen);
  AddCallback("toggleResetScreen",
              &SigninScreenHandler::HandleToggleResetScreen);
  AddCallback("launchHelpApp", &SigninScreenHandler::HandleLaunchHelpApp);
  AddCallback("createAccount", &SigninScreenHandler::HandleCreateAccount);
  AddCallback("accountPickerReady",
              &SigninScreenHandler::HandleAccountPickerReady);
  AddCallback("wallpaperReady", &SigninScreenHandler::HandleWallpaperReady);
  AddCallback("loginWebuiReady", &SigninScreenHandler::HandleLoginWebuiReady);
  AddCallback("signOutUser", &SigninScreenHandler::HandleSignOutUser);
  AddCallback("networkErrorShown",
              &SigninScreenHandler::HandleNetworkErrorShown);
  AddCallback("openProxySettings",
              &SigninScreenHandler::HandleOpenProxySettings);
  AddCallback("loginVisible", &SigninScreenHandler::HandleLoginVisible);
  AddCallback("cancelPasswordChangedFlow",
              &SigninScreenHandler::HandleCancelPasswordChangedFlow);
  AddCallback("cancelUserAdding",
              &SigninScreenHandler::HandleCancelUserAdding);
  AddCallback("migrateUserData", &SigninScreenHandler::HandleMigrateUserData);
  AddCallback("resyncUserData", &SigninScreenHandler::HandleResyncUserData);
  AddCallback("loginUIStateChanged",
              &SigninScreenHandler::HandleLoginUIStateChanged);
  AddCallback("unlockOnLoginSuccess",
              &SigninScreenHandler::HandleUnlockOnLoginSuccess);
  AddCallback("frameLoadingCompleted",
              &SigninScreenHandler::HandleFrameLoadingCompleted);
  AddCallback("showLoadingTimeoutError",
              &SigninScreenHandler::HandleShowLoadingTimeoutError);
  AddCallback("updateOfflineLogin",
              &SigninScreenHandler::HandleUpdateOfflineLogin);
}

void SigninScreenHandler::RegisterPrefs(PrefRegistrySimple* registry) {
  registry->RegisterDictionaryPref(prefs::kUsersLRUInputMethod);
}

void SigninScreenHandler::HandleGetUsers() {
  SendUserList(false);
}

void SigninScreenHandler::ClearAndEnablePassword() {
  core_oobe_actor_->ResetSignInUI(false);
}

void SigninScreenHandler::ClearUserPodPassword() {
  core_oobe_actor_->ClearUserPodPassword();
}

void SigninScreenHandler::RefocusCurrentPod() {
  core_oobe_actor_->RefocusCurrentPod();
}

void SigninScreenHandler::OnLoginSuccess(const std::string& username) {
  core_oobe_actor_->OnLoginSuccess(username);
}

void SigninScreenHandler::OnUserRemoved(const std::string& username) {
  SendUserList(false);
}

void SigninScreenHandler::OnUserImageChanged(const User& user) {
  if (page_is_ready())
    CallJS("login.AccountPickerScreen.updateUserImage", user.email());
}

void SigninScreenHandler::OnPreferencesChanged() {
  // Make sure that one of the login UI is fully functional now, otherwise
  // preferences update would be picked up next time it will be shown.
  if (!webui_visible_) {
    LOG(WARNING) << "Login UI is not active - postponed prefs change.";
    preferences_changed_delayed_ = true;
    return;
  }

  if (delegate_ && !delegate_->IsShowUsers()) {
    HandleShowAddUser(NULL);
  } else {
    SendUserList(false);
    UpdateUIState(UI_STATE_ACCOUNT_PICKER, NULL);
  }
  preferences_changed_delayed_ = false;
}

void SigninScreenHandler::ResetSigninScreenHandlerDelegate() {
  SetDelegate(NULL);
}

void SigninScreenHandler::ShowError(int login_attempts,
                                    const std::string& error_text,
                                    const std::string& help_link_text,
                                    HelpAppLauncher::HelpTopic help_topic_id) {
  core_oobe_actor_->ShowSignInError(login_attempts, error_text, help_link_text,
                                    help_topic_id);
}

void SigninScreenHandler::ShowErrorScreen(LoginDisplay::SigninError error_id) {
  switch (error_id) {
    case LoginDisplay::TPM_ERROR:
      core_oobe_actor_->ShowTpmError();
      break;
    default:
      NOTREACHED() << "Unknown sign in error";
      break;
  }
}

void SigninScreenHandler::ShowSigninUI(const std::string& email) {

}

void SigninScreenHandler::ShowGaiaPasswordChanged(const std::string& username) {
  email_ = username;
  password_changed_for_.insert(email_);
  core_oobe_actor_->ShowSignInUI(email_);
  CallJS("login.AccountPickerScreen.updateUserGaiaNeeded", email_);
}

void SigninScreenHandler::ShowPasswordChangedDialog(bool show_password_error) {
  core_oobe_actor_->ShowPasswordChangedScreen(show_password_error);
}

void SigninScreenHandler::ShowSigninScreenForCreds(
    const std::string& username,
    const std::string& password) {
  VLOG(2) << "ShowSigninScreenForCreds  for user " << username
          << ", frame_state=" << frame_state_;

  test_user_ = username;
  test_pass_ = password;
  test_expects_complete_login_ = true;

  // Submit login form for test if gaia is ready. If gaia is loading, login
  // will be attempted in HandleLoginWebuiReady after gaia is ready. Otherwise,
  // reload gaia then follow the loading case.
  if (frame_state_ == FRAME_STATE_LOADED)
    SubmitLoginFormForTest();
  else if (frame_state_ != FRAME_STATE_LOADING)
    HandleShowAddUser(NULL);
}

void SigninScreenHandler::OnCookiesCleared(base::Closure on_clear_callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  cookies_cleared_ = true;
  on_clear_callback.Run();
}

void SigninScreenHandler::OnCapsLockChange(bool enabled) {
  if (page_is_ready())
    CallJS("login.AccountPickerScreen.setCapsLockState", enabled);
}

void SigninScreenHandler::Observe(int type,
                                  const content::NotificationSource& source,
                                  const content::NotificationDetails& details) {
  switch (type) {
    case chrome::NOTIFICATION_SYSTEM_SETTING_CHANGED: {
      UpdateAuthExtension();
      UpdateAddButtonStatus();
      break;
    }
    case chrome::NOTIFICATION_AUTH_NEEDED: {
      has_pending_auth_ui_ = true;
      break;
    }
    case chrome::NOTIFICATION_AUTH_SUPPLIED:
      has_pending_auth_ui_ = false;
      if (IsSigninScreenHiddenByError()) {
        // Hide error screen and reload auth extension.
        HideOfflineMessage(network_state_informer_->state(),
                           ErrorScreenActor::ERROR_REASON_PROXY_AUTH_SUPPLIED);
      } else if (ui_state_ == UI_STATE_GAIA_SIGNIN) {
        // Reload auth extension as proxy credentials are supplied.
        ReloadGaiaScreen();
      }
      break;
    case chrome::NOTIFICATION_AUTH_CANCELLED: {
      // Don't reload auth extension if proxy auth dialog was cancelled.
      has_pending_auth_ui_ = false;
      break;
    }
    default:
      NOTREACHED() << "Unexpected notification " << type;
  }
}

void SigninScreenHandler::OnDnsCleared() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  dns_clear_task_running_ = false;
  dns_cleared_ = true;
  ShowSigninScreenIfReady();
}

void SigninScreenHandler::SetUserInputMethodHWDefault() {
  chromeos::input_method::InputMethodManager* manager =
      chromeos::input_method::InputMethodManager::Get();
  manager->ChangeInputMethod(
      manager->GetInputMethodUtil()->GetHardwareInputMethodId());
}

// Update keyboard layout to least recently used by the user.
void SigninScreenHandler::SetUserInputMethod(const std::string& username) {
  chromeos::input_method::InputMethodManager* const manager =
      chromeos::input_method::InputMethodManager::Get();

  const chromeos::input_method::InputMethodUtil& ime_util =
      *manager->GetInputMethodUtil();

  const bool succeed = SetUserInputMethodImpl(username, manager);

  // This is also a case when LRU layout is set only for a few local users,
  // thus others need to be switched to default locale.
  // Otherwise they will end up using another user's locale to log in.
  if (!succeed) {
    DLOG(INFO) << "SetUserInputMethod('" << username
               << "'): failed to set user layout. Switching to default '"
               << ime_util.GetHardwareInputMethodId() << "'";

    SetUserInputMethodHWDefault();
  }
}

void SigninScreenHandler::ShowSigninScreenIfReady() {
  if (!dns_cleared_ || !cookies_cleared_ || !delegate_)
    return;

  std::string active_network_path = network_state_informer_->network_path();
  if (gaia_silent_load_ &&
      (network_state_informer_->state() != NetworkStateInformer::ONLINE ||
       gaia_silent_load_network_ != active_network_path)) {
    // Network has changed. Force Gaia reload.
    gaia_silent_load_ = false;
    // Gaia page will be realoded, so focus isn't stolen anymore.
    focus_stolen_ = false;
  }

  // Note that LoadAuthExtension clears |email_|.
  if (email_.empty())
    delegate_->LoadSigninWallpaper();
  else
    delegate_->LoadWallpaper(email_);

  // Set Least Recently Used input method for the user.
  if (!email_.empty())
    SetUserInputMethod(email_);

  LoadAuthExtension(!gaia_silent_load_, false, false);
  UpdateUIState(UI_STATE_GAIA_SIGNIN, NULL);

  if (gaia_silent_load_) {
    // The variable is assigned to false because silently loaded Gaia page was
    // used.
    gaia_silent_load_ = false;
    if (focus_stolen_)
      HandleLoginWebuiReady();
  }

  UpdateState(ErrorScreenActor::ERROR_REASON_UPDATE);
}


void SigninScreenHandler::UpdateAuthParams(DictionaryValue* params) {
  if (!delegate_)
    return;

  UpdateAuthParamsFromSettings(params, CrosSettings::Get());

  // Allow locally managed user creation only if:
  // 1. Enterprise managed device > is allowed by policy.
  // 2. Consumer device > owner exists.
  // 3. New users are allowed by owner.

  CrosSettings* cros_settings = CrosSettings::Get();
  bool allow_new_user = false;
  cros_settings->GetBoolean(kAccountsPrefAllowNewUser, &allow_new_user);

  bool managed_users_allowed =
      UserManager::Get()->AreLocallyManagedUsersAllowed();
  bool managed_users_can_create = true;
  int message_id = -1;
  if (delegate_->GetUsers().size() == 0) {
    managed_users_can_create = false;
    message_id = IDS_CREATE_LOCALLY_MANAGED_USER_NO_MANAGER_TEXT;
  }
  if (!allow_new_user) {
    managed_users_can_create = false;
    message_id = IDS_CREATE_LOCALLY_MANAGED_USER_CREATION_RESTRICTED_TEXT;
  }

  params->SetBoolean("managedUsersEnabled", managed_users_allowed);
  params->SetBoolean("managedUsersCanCreate", managed_users_can_create);
  if (!managed_users_can_create) {
    params->SetString("managedUsersRestrictionReason",
        l10n_util::GetStringUTF16(message_id));
  }
}

void SigninScreenHandler::LoadAuthExtension(
    bool force, bool silent_load, bool offline) {
  DictionaryValue params;

  params.SetBoolean("forceReload", force);
  params.SetBoolean("silentLoad", silent_load);
  params.SetBoolean("isLocal", offline);
  params.SetBoolean("passwordChanged",
                    !email_.empty() && password_changed_for_.count(email_));
  if (delegate_)
    params.SetBoolean("isShowUsers", delegate_->IsShowUsers());
  params.SetBoolean("useOffline", offline);
  params.SetString("email", email_);
  email_.clear();

  UpdateAuthParams(&params);

  if (!offline) {
    const std::string app_locale = g_browser_process->GetApplicationLocale();
    if (!app_locale.empty())
      params.SetString("hl", app_locale);
  } else {
    base::DictionaryValue *localized_strings = new base::DictionaryValue();
    localized_strings->SetString("stringEmail",
        l10n_util::GetStringUTF16(IDS_LOGIN_OFFLINE_EMAIL));
    localized_strings->SetString("stringPassword",
        l10n_util::GetStringUTF16(IDS_LOGIN_OFFLINE_PASSWORD));
    localized_strings->SetString("stringSignIn",
        l10n_util::GetStringUTF16(IDS_LOGIN_OFFLINE_SIGNIN));
    localized_strings->SetString("stringEmptyEmail",
        l10n_util::GetStringUTF16(IDS_LOGIN_OFFLINE_EMPTY_EMAIL));
    localized_strings->SetString("stringEmptyPassword",
        l10n_util::GetStringUTF16(IDS_LOGIN_OFFLINE_EMPTY_PASSWORD));
    localized_strings->SetString("stringError",
        l10n_util::GetStringUTF16(IDS_LOGIN_OFFLINE_ERROR));
    params.Set("localizedStrings", localized_strings);
  }

  const GURL gaia_url =
      CommandLine::ForCurrentProcess()->HasSwitch(::switches::kGaiaUrl) ?
          GURL(CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
                    ::switches::kGaiaUrl)) :
          GaiaUrls::GetInstance()->gaia_url();
  params.SetString("gaiaUrl", gaia_url.spec());

  frame_state_ = FRAME_STATE_LOADING;
  CallJS("login.GaiaSigninScreen.loadAuthExtension", params);
}

void SigninScreenHandler::UpdateAuthExtension() {
  DictionaryValue params;
  UpdateAuthParams(&params);
  CallJS("login.GaiaSigninScreen.updateAuthExtension", params);
}

void SigninScreenHandler::UpdateAddButtonStatus() {
  CallJS("cr.ui.login.DisplayManager.updateAddUserButtonStatus",
         AllWhitelistedUsersPresent());
}

void SigninScreenHandler::HandleCompleteLogin(const std::string& typed_email,
                                              const std::string& password) {
  if (!delegate_)
    return;
  const std::string sanitized_email = gaia::SanitizeEmail(typed_email);
  delegate_->SetDisplayEmail(sanitized_email);
  delegate_->CompleteLogin(UserContext(sanitized_email,
                                       password,
                                       std::string()));  // auth_code

  if (test_expects_complete_login_) {
    VLOG(2) << "Complete test login for " << typed_email
            << ", requested=" << test_user_;

    test_expects_complete_login_ = false;
    test_user_.clear();
    test_pass_.clear();
  }
}

void SigninScreenHandler::HandleCompleteAuthentication(
    const std::string& email,
    const std::string& password,
    const std::string& auth_code) {
  if (!delegate_)
    return;
  const std::string sanitized_email = gaia::SanitizeEmail(email);
  delegate_->SetDisplayEmail(sanitized_email);
  delegate_->CompleteLogin(UserContext(sanitized_email, password, auth_code));
}

void SigninScreenHandler::HandleAuthenticateUser(const std::string& username,
                                                 const std::string& password) {
  if (!delegate_)
    return;
  delegate_->Login(UserContext(gaia::SanitizeEmail(username),
                               password,
                               std::string()));  // auth_code
}

void SigninScreenHandler::HandleLaunchDemoUser() {
  if (delegate_)
    delegate_->LoginAsRetailModeUser();
}

void SigninScreenHandler::HandleLaunchIncognito() {
  if (delegate_)
    delegate_->LoginAsGuest();
}

void SigninScreenHandler::HandleShowLocallyManagedUserCreationScreen() {
  if (!UserManager::Get()->AreLocallyManagedUsersAllowed()) {
    LOG(ERROR) << "Managed users not allowed.";
    return;
  }
  scoped_ptr<DictionaryValue> params(new DictionaryValue());
  LoginDisplayHostImpl::default_host()->
      StartWizard(WizardController::kLocallyManagedUserCreationScreenName,
      params.Pass());
}

void SigninScreenHandler::HandleLaunchPublicAccount(
    const std::string& username) {
  if (delegate_)
    delegate_->LoginAsPublicAccount(username);
}

void SigninScreenHandler::HandleOfflineLogin(const base::ListValue* args) {
  if (!delegate_ || delegate_->IsShowUsers()) {
    NOTREACHED();
    return;
  }
  if (!args->GetString(0, &email_))
    email_.clear();
  // Load auth extension. Parameters are: force reload, do not load extension in
  // background, use offline version.
  LoadAuthExtension(true, false, true);
  UpdateUIState(UI_STATE_GAIA_SIGNIN, NULL);
}

void SigninScreenHandler::HandleShutdownSystem() {
  ash::Shell::GetInstance()->lock_state_controller()->RequestShutdown();
}

void SigninScreenHandler::HandleLoadWallpaper(const std::string& email) {
  if (delegate_)
    delegate_->LoadWallpaper(email);
}

void SigninScreenHandler::HandleRebootSystem() {
  chromeos::DBusThreadManager::Get()->GetPowerManagerClient()->RequestRestart();
}

void SigninScreenHandler::HandleRemoveUser(const std::string& email) {
  if (!delegate_)
    return;
  delegate_->RemoveUser(email);
  UpdateAddButtonStatus();
}

void SigninScreenHandler::HandleShowAddUser(const base::ListValue* args) {
  email_.clear();
  // |args| can be null if it's OOBE.
  if (args)
    args->GetString(0, &email_);
  is_account_picker_showing_first_time_ = false;

  if (gaia_silent_load_ && email_.empty()) {
    dns_cleared_ = true;
    cookies_cleared_ = true;
    ShowSigninScreenIfReady();
  } else {
    StartClearingDnsCache();
    StartClearingCookies(base::Bind(
        &SigninScreenHandler::ShowSigninScreenIfReady,
        weak_factory_.GetWeakPtr()));
  }
  SetUserInputMethodHWDefault();
}

void SigninScreenHandler::HandleToggleEnrollmentScreen() {
  if (delegate_)
    delegate_->ShowEnterpriseEnrollmentScreen();
}

void SigninScreenHandler::HandleToggleKioskEnableScreen() {
  if (delegate_ &&
      !g_browser_process->browser_policy_connector()->IsEnterpriseManaged()) {
    delegate_->ShowKioskEnableScreen();
  }
}

void SigninScreenHandler::HandleToggleResetScreen() {
  if (delegate_ &&
      !g_browser_process->browser_policy_connector()->IsEnterpriseManaged()) {
    delegate_->ShowResetScreen();
  }
}

void SigninScreenHandler::HandleToggleKioskAutolaunchScreen() {
  if (delegate_ &&
      !g_browser_process->browser_policy_connector()->IsEnterpriseManaged()) {
    delegate_->ShowKioskAutolaunchScreen();
  }
}

void SigninScreenHandler::HandleLaunchHelpApp(double help_topic_id) {
  if (!delegate_)
    return;
  if (!help_app_.get())
    help_app_ = new HelpAppLauncher(GetNativeWindow());
  help_app_->ShowHelpTopic(
      static_cast<HelpAppLauncher::HelpTopic>(help_topic_id));
}

void SigninScreenHandler::FillUserDictionary(User* user,
                                             bool is_owner,
                                             DictionaryValue* user_dict) {
  const std::string& email = user->email();
  bool is_public_account =
      user->GetType() == User::USER_TYPE_PUBLIC_ACCOUNT;
  bool is_locally_managed_user =
      user->GetType() == User::USER_TYPE_LOCALLY_MANAGED;

  user_dict->SetString(kKeyUsername, email);
  user_dict->SetString(kKeyEmailAddress, user->display_email());
  user_dict->SetString(kKeyDisplayName, user->GetDisplayName());
  user_dict->SetBoolean(kKeyPublicAccount, is_public_account);
  user_dict->SetBoolean(kKeyLocallyManagedUser, is_locally_managed_user);
  user_dict->SetInteger(kKeyOauthTokenStatus, user->oauth_token_status());
  user_dict->SetBoolean(kKeySignedIn, user->is_logged_in());
  user_dict->SetBoolean(kKeyIsOwner, is_owner);

  if (is_public_account) {
    policy::BrowserPolicyConnector* policy_connector =
        g_browser_process->browser_policy_connector();

    if (policy_connector->IsEnterpriseManaged()) {
      user_dict->SetString(kKeyEnterpriseDomain,
                           policy_connector->GetEnterpriseDomain());
    }
  }
}

void SigninScreenHandler::SendUserList(bool animated) {
  if (!delegate_)
    return;

  size_t max_non_owner_users = kMaxUsers - 1;
  size_t non_owner_count = 0;

  ListValue users_list;
  const UserList& users = delegate_->GetUsers();

  // TODO(nkostylev): Show optional intro dialog about multi-profiles feature
  // based on user preferences. http://crbug.com/230862

  // TODO(nkostylev): Move to a separate method in UserManager.
  // http://crbug.com/230852
  bool is_signin_to_add = LoginDisplayHostImpl::default_host() &&
      UserManager::Get()->IsUserLoggedIn();

  bool single_user = users.size() == 1;
  for (UserList::const_iterator it = users.begin(); it != users.end(); ++it) {
    const std::string& email = (*it)->email();

    std::string owner;
    chromeos::CrosSettings::Get()->GetString(chromeos::kDeviceOwner, &owner);
    bool is_owner = (email == owner);

    if (non_owner_count < max_non_owner_users || is_owner) {
      DictionaryValue* user_dict = new DictionaryValue();
      FillUserDictionary(*it, is_owner, user_dict);
      bool is_public_account =
          ((*it)->GetType() == User::USER_TYPE_PUBLIC_ACCOUNT);
      bool signed_in = (*it)->is_logged_in();
      // Single user check here is necessary because owner info might not be
      // available when running into login screen on first boot.
      // See http://crosbug.com/12723
      user_dict->SetBoolean(kKeyCanRemove,
                            !single_user &&
                            !email.empty() &&
                            !is_owner &&
                            !is_public_account &&
                            !signed_in &&
                            !is_signin_to_add);

      users_list.Append(user_dict);
      if (!is_owner)
        ++non_owner_count;
    }
  }

  CallJS("login.AccountPickerScreen.loadUsers", users_list, animated,
         delegate_->IsShowGuest());
}

void SigninScreenHandler::HandleAccountPickerReady() {
  LOG(INFO) << "Login WebUI >> AccountPickerReady";

  if (delegate_ && !ScreenLocker::default_screen_locker() &&
      !chromeos::IsMachineHWIDCorrect() &&
      !oobe_ui_) {
    delegate_->ShowWrongHWIDScreen();
    return;
  }

  PrefService* prefs = g_browser_process->local_state();
  if (prefs->GetBoolean(prefs::kFactoryResetRequested)) {
    prefs->SetBoolean(prefs::kFactoryResetRequested, false);
    prefs->CommitPendingWrite();
    HandleToggleResetScreen();
    return;
  }

  is_account_picker_showing_first_time_ = true;
  MaybePreloadAuthExtension();

  if (ScreenLocker::default_screen_locker()) {
    content::NotificationService::current()->Notify(
        chrome::NOTIFICATION_LOCK_WEBUI_READY,
        content::NotificationService::AllSources(),
        content::NotificationService::NoDetails());
  }

  if (delegate_)
    delegate_->OnSigninScreenReady();
}

void SigninScreenHandler::HandleWallpaperReady() {
  if (ScreenLocker::default_screen_locker()) {
    content::NotificationService::current()->Notify(
        chrome::NOTIFICATION_LOCK_BACKGROUND_DISPLAYED,
        content::NotificationService::AllSources(),
        content::NotificationService::NoDetails());
  }
}

void SigninScreenHandler::HandleLoginWebuiReady() {
  if (focus_stolen_) {
    // Set focus to the Gaia page.
    // TODO(altimofeev): temporary solution, until focus parameters are
    // implemented on the Gaia side.
    // Do this only once. Any subsequent call would relod GAIA frame.
    focus_stolen_ = false;
    const char code[] = "gWindowOnLoad();";
    RenderViewHost* rvh = web_ui()->GetWebContents()->GetRenderViewHost();
    rvh->ExecuteJavascriptInWebFrame(
        ASCIIToUTF16("//iframe[@id='signin-frame']\n//iframe"),
        ASCIIToUTF16(code));
  }
  if (!gaia_silent_load_) {
    content::NotificationService::current()->Notify(
        chrome::NOTIFICATION_LOGIN_WEBUI_LOADED,
        content::NotificationService::AllSources(),
        content::NotificationService::NoDetails());
  } else {
    focus_stolen_ = true;
    // Prevent focus stealing by the Gaia page.
    // TODO(altimofeev): temporary solution, until focus parameters are
    // implemented on the Gaia side.
    const char code[] = "var gWindowOnLoad = window.onload; "
                        "window.onload=function() {};";
    RenderViewHost* rvh = web_ui()->GetWebContents()->GetRenderViewHost();
    rvh->ExecuteJavascriptInWebFrame(
        ASCIIToUTF16("//iframe[@id='signin-frame']\n//iframe"),
        ASCIIToUTF16(code));
    // As we could miss and window.onload could already be called, restore
    // focus to current pod (see crbug/175243).
    RefocusCurrentPod();
  }
  HandleFrameLoadingCompleted(0);

  if (test_expects_complete_login_)
    SubmitLoginFormForTest();
}

void SigninScreenHandler::HandleSignOutUser() {
  if (delegate_)
    delegate_->Signout();
}

void SigninScreenHandler::HandleNetworkErrorShown() {
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_LOGIN_NETWORK_ERROR_SHOWN,
      content::NotificationService::AllSources(),
      content::NotificationService::NoDetails());
}

void SigninScreenHandler::HandleCreateAccount() {
  if (delegate_)
    delegate_->CreateAccount();
}

void SigninScreenHandler::HandleOpenProxySettings() {
  LoginDisplayHostImpl::default_host()->OpenProxySettings();
}

void SigninScreenHandler::HandleLoginVisible(const std::string& source) {
  LOG(WARNING) << "Login WebUI >> loginVisible, src: " << source << ", "
               << "webui_visible_: " << webui_visible_;
  if (!webui_visible_) {
    // There might be multiple messages from OOBE UI so send notifications after
    // the first one only.
    content::NotificationService::current()->Notify(
        chrome::NOTIFICATION_LOGIN_OR_LOCK_WEBUI_VISIBLE,
        content::NotificationService::AllSources(),
        content::NotificationService::NoDetails());
    TRACE_EVENT_ASYNC_END0(
        "ui", "ShowLoginWebUI", LoginDisplayHostImpl::kShowLoginWebUIid);
  }
  webui_visible_ = true;
  if (preferences_changed_delayed_)
    OnPreferencesChanged();
}

void SigninScreenHandler::HandleCancelPasswordChangedFlow() {
  StartClearingCookies(base::Bind(
      &SigninScreenHandler::CancelPasswordChangedFlowInternal,
      weak_factory_.GetWeakPtr()));
}

void SigninScreenHandler::HandleCancelUserAdding() {
  if (delegate_)
    delegate_->CancelUserAdding();
}

void SigninScreenHandler::HandleMigrateUserData(
    const std::string& old_password) {
  if (delegate_)
    delegate_->MigrateUserData(old_password);
}

void SigninScreenHandler::HandleResyncUserData() {
  if (delegate_)
    delegate_->ResyncUserData();
}

void SigninScreenHandler::HandleLoginUIStateChanged(const std::string& source,
                                                    bool new_value) {
  LOG(INFO) << "Login WebUI >> active: " << new_value << ", "
            << "source: " << source;

  if (!KioskAppManager::Get()->GetAutoLaunchApp().empty() &&
      KioskAppManager::Get()->IsAutoLaunchRequested()) {
    LOG(INFO) << "Showing auto-launch warning";
    HandleToggleKioskAutolaunchScreen();
    return;
  }

  if (source == kSourceGaiaSignin) {
    ui_state_ = UI_STATE_GAIA_SIGNIN;
  } else if (source == kSourceAccountPicker) {
    ui_state_ = UI_STATE_ACCOUNT_PICKER;
  } else {
    NOTREACHED();
    return;
  }
}

void SigninScreenHandler::HandleUnlockOnLoginSuccess() {
  DCHECK(UserManager::Get()->IsUserLoggedIn());
  if (ScreenLocker::default_screen_locker())
    ScreenLocker::default_screen_locker()->UnlockOnLoginSuccess();
}

void SigninScreenHandler::HandleFrameLoadingCompleted(int status) {
  const net::Error frame_error = static_cast<net::Error>(-status);
  if (frame_error == net::ERR_ABORTED) {
    LOG(WARNING) << "Ignore gaia frame error: " << frame_error;
    return;
  }
  frame_error_ = frame_error;
  if (frame_error == net::OK) {
    LOG(INFO) << "Gaia frame is loaded";
    frame_state_ = FRAME_STATE_LOADED;
  } else {
    LOG(WARNING) << "Gaia frame error: "  << frame_error_;
    frame_state_ = FRAME_STATE_ERROR;
  }

  if (network_state_informer_->state() != NetworkStateInformer::ONLINE)
    return;
  if (frame_state_ == FRAME_STATE_LOADED)
    UpdateState(ErrorScreenActor::ERROR_REASON_UPDATE);
  else if (frame_state_ == FRAME_STATE_ERROR)
    UpdateState(ErrorScreenActor::ERROR_REASON_FRAME_ERROR);
}

void SigninScreenHandler::HandleShowLoadingTimeoutError() {
  UpdateState(ErrorScreenActor::ERROR_REASON_LOADING_TIMEOUT);
}

void SigninScreenHandler::HandleUpdateOfflineLogin(bool offline_login_active) {
  offline_login_active_ = offline_login_active;
}

void SigninScreenHandler::StartClearingDnsCache() {
  if (dns_clear_task_running_ || !g_browser_process->io_thread())
    return;

  dns_cleared_ = false;
  BrowserThread::PostTaskAndReply(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&ClearDnsCache, g_browser_process->io_thread()),
      base::Bind(&SigninScreenHandler::OnDnsCleared,
                 weak_factory_.GetWeakPtr()));
  dns_clear_task_running_ = true;
}

void SigninScreenHandler::StartClearingCookies(
    const base::Closure& on_clear_callback) {
  cookies_cleared_ = false;
  ProfileHelper* profile_helper =
      g_browser_process->platform_part()->profile_helper();
  LOG_ASSERT(
      Profile::FromWebUI(web_ui()) == profile_helper->GetSigninProfile());
  profile_helper->ClearSigninProfile(base::Bind(
      &SigninScreenHandler::OnCookiesCleared,
      weak_factory_.GetWeakPtr(), on_clear_callback));
}

void SigninScreenHandler::MaybePreloadAuthExtension() {
  // Fetching of the extension is not started before account picker page is
  // loaded because it can affect the loading speed. Also if cookies clearing
  // was initiated or |dns_clear_task_running_| then auth extension showing has
  // already been initiated and preloading is senseless.
  // Do not load the extension for the screen locker, see crosbug.com/25018.
  if (is_account_picker_showing_first_time_ &&
      !gaia_silent_load_ &&
      !ScreenLocker::default_screen_locker() &&
      !cookies_cleared_ &&
      !dns_clear_task_running_ &&
      network_state_informer_->state() == NetworkStateInformer::ONLINE) {
    gaia_silent_load_ = true;
    gaia_silent_load_network_ = network_state_informer_->network_path();
    LoadAuthExtension(true, true, false);
  }
}

bool SigninScreenHandler::AllWhitelistedUsersPresent() {
  CrosSettings* cros_settings = CrosSettings::Get();
  bool allow_new_user = false;
  cros_settings->GetBoolean(kAccountsPrefAllowNewUser, &allow_new_user);
  if (allow_new_user)
    return false;
  UserManager* user_manager = UserManager::Get();
  const UserList& users = user_manager->GetUsers();
  if (!delegate_ || users.size() > kMaxUsers) {
    return false;
  }
  const base::ListValue* whitelist = NULL;
  if (!cros_settings->GetList(kAccountsPrefUsers, &whitelist) || !whitelist)
    return false;
  for (size_t i = 0; i < whitelist->GetSize(); ++i) {
    std::string whitelisted_user;
    // NB: Wildcards in the whitelist are also detected as not present here.
    if (!whitelist->GetString(i, &whitelisted_user) ||
        !user_manager->IsKnownUser(whitelisted_user)) {
      return false;
    }
  }
  return true;
}

void SigninScreenHandler::CancelPasswordChangedFlowInternal() {
  if (delegate_) {
    Show(oobe_ui_);
    delegate_->CancelPasswordChangedFlow();
  }
}

OobeUI::Screen SigninScreenHandler::GetCurrentScreen() const {
  OobeUI::Screen screen = OobeUI::SCREEN_UNKNOWN;
  OobeUI* oobe_ui = static_cast<OobeUI*>(web_ui()->GetController());
  if (oobe_ui)
    screen = oobe_ui->current_screen();
  return screen;
}

bool SigninScreenHandler::IsGaiaVisible() const {
  return IsSigninScreen(GetCurrentScreen()) &&
      ui_state_ == UI_STATE_GAIA_SIGNIN;
}

bool SigninScreenHandler::IsGaiaHiddenByError() const {
  return IsSigninScreenHiddenByError() &&
      ui_state_ == UI_STATE_GAIA_SIGNIN;
}

bool SigninScreenHandler::IsSigninScreenHiddenByError() const {
  return (GetCurrentScreen() == OobeUI::SCREEN_ERROR_MESSAGE) &&
      (IsSigninScreen(error_screen_actor_->parent_screen()));
}

bool SigninScreenHandler::IsGuestSigninAllowed() const {
  CrosSettings* cros_settings = CrosSettings::Get();
  if (!cros_settings)
    return false;
  bool allow_guest;
  cros_settings->GetBoolean(kAccountsPrefAllowGuest, &allow_guest);
  return allow_guest;
}

bool SigninScreenHandler::IsOfflineLoginAllowed() const {
  CrosSettings* cros_settings = CrosSettings::Get();
  if (!cros_settings)
    return false;

  // Offline login is allowed only when user pods are hidden.
  bool show_pods;
  cros_settings->GetBoolean(kAccountsPrefShowUserNamesOnSignIn, &show_pods);
  return !show_pods;
}

void SigninScreenHandler::SubmitLoginFormForTest() {
  VLOG(2) << "Submit login form for test, user=" << test_user_;

  std::string code;
  code += "document.getElementById('Email').value = '" + test_user_ + "';";
  code += "document.getElementById('Passwd').value = '" + test_pass_ + "';";
  code += "document.getElementById('signIn').click();";

  RenderViewHost* rvh = web_ui()->GetWebContents()->GetRenderViewHost();
  rvh->ExecuteJavascriptInWebFrame(
      ASCIIToUTF16("//iframe[@id='signin-frame']\n//iframe"),
      ASCIIToUTF16(code));

  // Test properties are cleared in HandleCompleteLogin because the form
  // submission might fail and login will not be attempted after reloading
  // if they are cleared here.
}

}  // namespace chromeos
