// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_WIZARD_CONTROLLER_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_WIZARD_CONTROLLER_H_

#include <string>

#include "base/compiler_specific.h"
#include "base/gtest_prod_util.h"
#include "base/memory/scoped_ptr.h"
#include "base/observer_list.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/chromeos/login/screens/screen_observer.h"
#include "chrome/browser/chromeos/login/screens/wizard_screen.h"
#include "ui/gfx/rect.h"
#include "url/gurl.h"

class PrefRegistrySimple;

namespace base {
class DictionaryValue;
}

namespace chromeos {

class EnrollmentScreen;
class ErrorScreen;
class EulaScreen;
class KioskAutolaunchScreen;
class KioskEnableScreen;
class LocallyManagedUserCreationScreen;
class LoginDisplayHost;
class NetworkScreen;
class OobeDisplay;
class ResetScreen;
class TermsOfServiceScreen;
class UpdateScreen;
class UserImageScreen;
class WizardScreen;
class WrongHWIDScreen;

// Class that manages control flow between wizard screens. Wizard controller
// interacts with screen controllers to move the user between screens.
class WizardController : public ScreenObserver {
 public:
  // Observes screen changes.
  class Observer {
   public:
    // Called before a screen change happens.
    virtual void OnScreenChanged(WizardScreen* next_screen) = 0;

    // Called after the browser session has started.
    virtual void OnSessionStart() = 0;
  };

  WizardController(LoginDisplayHost* host, OobeDisplay* oobe_display);
  virtual ~WizardController();

  // Returns the default wizard controller if it has been created.
  static WizardController* default_controller() {
    return default_controller_;
  }

  // Whether to skip any screens that may normally be shown after login
  // (registration, Terms of Service, user image selection).
  static bool skip_post_login_screens() {
    return skip_post_login_screens_;
  }

  // Sets delays to zero. MUST be used only for tests.
  static void SetZeroDelays();

  // If true zero delays have been enabled (for browser tests).
  static bool IsZeroDelayEnabled();

  // Skips any screens that may normally be shown after login (registration,
  // Terms of Service, user image selection).
  static void SkipPostLoginScreensForTesting();

  // Shows the first screen defined by |first_screen_name| or by default
  // if the parameter is empty. Takes ownership of |screen_parameters|.
  void Init(const std::string& first_screen_name,
            scoped_ptr<base::DictionaryValue> screen_parameters);

  // Advances to screen defined by |screen_name| and shows it.
  void AdvanceToScreen(const std::string& screen_name);

  // Advances to screen defined by |screen_name| and shows it.
  // Takes ownership of |screen_parameters|.
  void AdvanceToScreenWithParams(const std::string& first_screen_name,
                                 base::DictionaryValue* screen_parameters);

  // Advances to login screen. Should be used in for testing only.
  void SkipToLoginForTesting();

  // Adds and removes an observer.
  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Called right after the browser session has started.
  void OnSessionStart();

  // Skip update, go straight to enrollment after EULA is accepted.
  void SkipUpdateEnrollAfterEula();

  // TODO(antrim) : temporary hack. Should be removed once screen system is
  // reworked at hackaton.
  void EnableUserImageScreenReturnToPreviousHack();

  // Lazy initializers and getters for screens.
  NetworkScreen* GetNetworkScreen();
  UpdateScreen* GetUpdateScreen();
  UserImageScreen* GetUserImageScreen();
  EulaScreen* GetEulaScreen();
  EnrollmentScreen* GetEnrollmentScreen();
  ResetScreen* GetResetScreen();
  KioskAutolaunchScreen* GetKioskAutolaunchScreen();
  KioskEnableScreen* GetKioskEnableScreen();
  TermsOfServiceScreen* GetTermsOfServiceScreen();
  WrongHWIDScreen* GetWrongHWIDScreen();
  LocallyManagedUserCreationScreen* GetLocallyManagedUserCreationScreen();

  // Returns a pointer to the current screen or NULL if there's no such
  // screen.
  WizardScreen* current_screen() const { return current_screen_; }

  // Returns true if the current wizard instance has reached the login screen.
  bool login_screen_started() const { return login_screen_started_; }

  static const char kNetworkScreenName[];
  static const char kLoginScreenName[];
  static const char kUpdateScreenName[];
  static const char kUserImageScreenName[];
  static const char kOutOfBoxScreenName[];
  static const char kTestNoScreenName[];
  static const char kEulaScreenName[];
  static const char kEnrollmentScreenName[];
  static const char kResetScreenName[];
  static const char kKioskEnableScreenName[];
  static const char kKioskAutolaunchScreenName[];
  static const char kErrorScreenName[];
  static const char kTermsOfServiceScreenName[];
  static const char kWrongHWIDScreenName[];
  static const char kLocallyManagedUserCreationScreenName[];

 private:
  // Show specific screen.
  void ShowNetworkScreen();
  void ShowUpdateScreen();
  void ShowUserImageScreen();
  void ShowEulaScreen();
  void ShowEnrollmentScreen();
  void ShowResetScreen();
  void ShowKioskAutolaunchScreen();
  void ShowKioskEnableScreen();
  void ShowTermsOfServiceScreen();
  void ShowWrongHWIDScreen();
  void ShowLocallyManagedUserCreationScreen();

  // Shows images login screen.
  void ShowLoginScreen();

  // Resumes a pending login screen.
  void ResumeLoginScreen();

  // Exit handlers:
  void OnNetworkConnected();
  void OnNetworkOffline();
  void OnConnectionFailed();
  void OnUpdateCompleted();
  void OnEulaAccepted();
  void OnUpdateErrorCheckingForUpdate();
  void OnUpdateErrorUpdating();
  void OnUserImageSelected();
  void OnUserImageSkipped();
  void OnEnrollmentDone();
  void OnAutoEnrollmentDone();
  void OnResetCanceled();
  void OnKioskAutolaunchCanceled();
  void OnKioskAutolaunchConfirmed();
  void OnKioskEnableCompleted();
  void OnWrongHWIDWarningSkipped();
  void OnOOBECompleted();
  void OnTermsOfServiceDeclined();
  void OnTermsOfServiceAccepted();

  // Loads brand code on I/O enabled thread and stores to Local State.
  void LoadBrandCodeFromFile();

  // Called after all post-EULA blocking tasks have been completed.
  void OnEulaBlockingTasksDone();

  // Shows update screen and starts update process.
  void InitiateOOBEUpdate();

  // Actions that should be done right after EULA is accepted,
  // before update check.
  void PerformPostEulaActions();

  // Actions that should be done right after update stage is finished.
  void PerformPostUpdateActions();

  // Overridden from ScreenObserver:
  virtual void OnExit(ExitCodes exit_code) OVERRIDE;
  virtual void ShowCurrentScreen() OVERRIDE;
  virtual void OnSetUserNamePassword(const std::string& username,
                                     const std::string& password) OVERRIDE;
  virtual void SetUsageStatisticsReporting(bool val) OVERRIDE;
  virtual bool GetUsageStatisticsReporting() const OVERRIDE;
  virtual ErrorScreen* GetErrorScreen() OVERRIDE;
  virtual void ShowErrorScreen() OVERRIDE;
  virtual void HideErrorScreen(WizardScreen* parent_screen) OVERRIDE;

  // Switches from one screen to another.
  void SetCurrentScreen(WizardScreen* screen);

  // Switches from one screen to another with delay before showing. Calling
  // ShowCurrentScreen directly forces screen to be shown immediately.
  void SetCurrentScreenSmooth(WizardScreen* screen, bool use_smoothing);

  // Changes status area visibility.
  void SetStatusAreaVisible(bool visible);

  // Logs in the specified user via default login screen.
  void Login(const std::string& username, const std::string& password);

  // Launched kiosk app configured for auto-launch.
  void AutoLaunchKioskApp();

  // Checks whether OOBE should start enrollment automatically.
  bool ShouldAutoStartEnrollment() const;

  // Checks whether the user is allowed to exit enrollment.
  bool CanExitEnrollment() const;

  // Whether to skip any screens that may normally be shown after login
  // (registration, Terms of Service, user image selection).
  static bool skip_post_login_screens_;

  static bool zero_delay_enabled_;

  // Screens.
  scoped_ptr<NetworkScreen> network_screen_;
  scoped_ptr<UpdateScreen> update_screen_;
  scoped_ptr<UserImageScreen> user_image_screen_;
  scoped_ptr<EulaScreen> eula_screen_;
  scoped_ptr<ResetScreen> reset_screen_;
  scoped_ptr<KioskAutolaunchScreen> autolaunch_screen_;
  scoped_ptr<KioskEnableScreen> kiosk_enable_screen_;
  scoped_ptr<EnrollmentScreen> enrollment_screen_;
  scoped_ptr<ErrorScreen> error_screen_;
  scoped_ptr<TermsOfServiceScreen> terms_of_service_screen_;
  scoped_ptr<WrongHWIDScreen> wrong_hwid_screen_;
  scoped_ptr<LocallyManagedUserCreationScreen>
      locally_managed_user_creation_screen_;

  // Screen that's currently active.
  WizardScreen* current_screen_;

  // Screen that was active before, or NULL for login screen.
  WizardScreen* previous_screen_;

  std::string username_;
  std::string password_;

  // True if running official BUILD.
  bool is_official_build_;

  // True if full OOBE flow should be shown.
  bool is_out_of_box_;

  // Value of the screen name that WizardController was started with.
  std::string first_screen_name_;

  // OOBE/login display host.
  LoginDisplayHost* host_;

  // Default WizardController.
  static WizardController* default_controller_;

  // Parameters for the first screen. May be NULL.
  scoped_ptr<base::DictionaryValue> screen_parameters_;

  base::OneShotTimer<WizardController> smooth_show_timer_;

  OobeDisplay* oobe_display_;

  // State of Usage stat/error reporting checkbox on EULA screen
  // during wizard lifetime.
  bool usage_statistics_reporting_;

  // If true then update check is cancelled and enrollment is started after
  // EULA is accepted.
  bool skip_update_enroll_after_eula_;

  // Time when the EULA was accepted. Used to measure the duration from the EULA
  // acceptance until the Sign-In screen is displayed.
  base::Time time_eula_accepted_;

  ObserverList<Observer> observer_list_;

  bool login_screen_started_;

  // Indicates that once image selection screen finishes we should return to
  // a previous screen instead of proceeding with usual flow.
  bool user_image_screen_return_to_previous_hack_;

  FRIEND_TEST_ALL_PREFIXES(EnrollmentScreenTest, TestCancel);
  FRIEND_TEST_ALL_PREFIXES(WizardControllerFlowTest, Accelerators);
  friend class WizardControllerFlowTest;
  friend class WizardInProcessBrowserTest;

  DISALLOW_COPY_AND_ASSIGN(WizardController);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_WIZARD_CONTROLLER_H_
