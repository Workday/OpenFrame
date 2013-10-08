// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_SYNC_ONE_CLICK_SIGNIN_SYNC_STARTER_H_
#define CHROME_BROWSER_UI_SYNC_ONE_CLICK_SIGNIN_SYNC_STARTER_H_

#include <string>

#include "base/callback_forward.h"
#include "base/gtest_prod_util.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/signin_promo.h"
#include "chrome/browser/signin/signin_tracker.h"
#include "chrome/browser/ui/browser_list_observer.h"
#include "chrome/browser/ui/host_desktop.h"
#include "chrome/browser/ui/sync/profile_signin_confirmation_helper.h"
#include "content/public/browser/web_contents_observer.h"

class Browser;
class ProfileSyncService;

namespace content {
class WebContents;
}  // namespace content

namespace policy {
class CloudPolicyClient;
}

// Waits for successful singin notification from the signin manager and then
// starts the sync machine.  Instances of this class delete themselves once
// the job is done.
class OneClickSigninSyncStarter : public SigninTracker::Observer,
                                  public chrome::BrowserListObserver,
                                  public content::WebContentsObserver {
 public:
  enum StartSyncMode {
    // Starts the process of signing the user in with the SigninManager, and
    // once completed automatically starts sync with all data types enabled.
    SYNC_WITH_DEFAULT_SETTINGS,

    // Starts the process of signing the user in with the SigninManager, and
    // once completed redirects the user to the settings page to allow them
    // to configure which data types to sync before sync is enabled.
    CONFIGURE_SYNC_FIRST,

    // Starts the process of re-authenticating the user via SigninManager,
    // and once completed, redirects the user to the settings page, but doesn't
    // display the configure sync UI.
    SHOW_SETTINGS_WITHOUT_CONFIGURE,

    // The process should be aborted because the undo button has been pressed.
    UNDO_SYNC
  };

  enum ConfirmationRequired {
    // No need to display a "post-signin" confirmation bubble (for example, if
    // the user was doing a re-auth flow).
    NO_CONFIRMATION,

    // Signin flow redirected outside of trusted domains, so ask the user to
    // confirm before signing in.
    CONFIRM_UNTRUSTED_SIGNIN,

    // Display a confirmation after signing in.
    CONFIRM_AFTER_SIGNIN
  };

  // Result of the sync setup.
  enum SyncSetupResult {
    SYNC_SETUP_SUCCESS,
    SYNC_SETUP_FAILURE
  };

  typedef base::Callback<void(SyncSetupResult)> Callback;

  // |profile| must not be NULL, however |browser| can be. When using the
  // OneClickSigninSyncStarter from a browser, provide both.
  // If |display_confirmation| is true, the user will be prompted to confirm the
  // signin before signin completes.
  // |web_contents| is used to show the sync setup page, if necessary. If NULL,
  // the sync setup page will be loaded in either a new tab or a tab that is
  // already showing it.
  // |callback| is always executed before OneClickSigninSyncStarter is deleted.
  // It can be empty.
  OneClickSigninSyncStarter(Profile* profile,
                            Browser* browser,
                            const std::string& session_index,
                            const std::string& email,
                            const std::string& password,
                            StartSyncMode start_mode,
                            content::WebContents* web_contents,
                            ConfirmationRequired display_confirmation,
                            signin::Source source,
                            Callback callback);

  // chrome::BrowserListObserver override.
  virtual void OnBrowserRemoved(Browser* browser) OVERRIDE;

 private:
  friend class OneClickSigninSyncStarterTest;
  FRIEND_TEST_ALL_PREFIXES(OneClickSigninSyncStarterTest,
                           CallbackSigninFailed);
  FRIEND_TEST_ALL_PREFIXES(OneClickSigninSyncStarterTest,
                           CallbackSigninSucceeded);
  FRIEND_TEST_ALL_PREFIXES(OneClickSigninSyncStarterTest,
                           CallbackNull);

  virtual ~OneClickSigninSyncStarter();

  // Initializes the internals of the OneClickSigninSyncStarter object. Can also
  // be used to re-initialize the object to refer to a newly created profile.
  void Initialize(Profile* profile, Browser* browser);

  // SigninTracker::Observer override.
  virtual void SigninFailed(const GoogleServiceAuthError& error) OVERRIDE;
  virtual void SigninSuccess() OVERRIDE;

#if defined(ENABLE_CONFIGURATION_POLICY)
  // User input handler for the signin confirmation dialog.
  class SigninDialogDelegate
    : public ui::ProfileSigninConfirmationDelegate {
   public:
    SigninDialogDelegate(
        base::WeakPtr<OneClickSigninSyncStarter> sync_starter);
    virtual ~SigninDialogDelegate();
    virtual void OnCancelSignin() OVERRIDE;
    virtual void OnContinueSignin() OVERRIDE;
    virtual void OnSigninWithNewProfile() OVERRIDE;
   private:
    base::WeakPtr<OneClickSigninSyncStarter> sync_starter_;
  };
  friend class SigninDialogDelegate;

  // Callback invoked once policy registration is complete. If registration
  // fails, |client| will be null.
  void OnRegisteredForPolicy(scoped_ptr<policy::CloudPolicyClient> client);

  // Callback invoked when a policy fetch request has completed. |success| is
  // true if policy was successfully fetched.
  void OnPolicyFetchComplete(bool success);

  // Called to create a new profile, which is then signed in with the
  // in-progress auth credentials currently stored in this object.
  void CreateNewSignedInProfile();

  // Helper function that loads policy with the passed CloudPolicyClient, then
  // completes the signin process.
  void LoadPolicyWithCachedClient();

  // Callback invoked once a profile is created, so we can complete the
  // credentials transfer, load policy, and open the first window.
  void CompleteInitForNewProfile(chrome::HostDesktopType desktop_type,
                                 Profile* profile,
                                 Profile::CreateStatus status);

  // Cancels the in-progress signin for this profile.
  void CancelSigninAndDelete();
#endif  // defined(ENABLE_CONFIGURATION_POLICY)

  // Callback invoked to check whether the user needs policy or if a
  // confirmation is required (in which case we have to prompt the user first).
  void ConfirmSignin(const std::string& oauth_token);

  // Displays confirmation UI to the user if confirmation_required_ ==
  // CONFIRM_UNTRUSTED_SIGNIN, otherwise completes the pending signin process.
  void ConfirmAndSignin();

  // Callback invoked once the user has responded to the signin confirmation UI.
  // If response == UNDO_SYNC, the signin is cancelled, otherwise the pending
  // signin is completed.
  void UntrustedSigninConfirmed(StartSyncMode response);

  // GetProfileSyncService returns non-NULL pointer if sync is enabled.
  // There is a scenario when when ProfileSyncService discovers that sync is
  // disabled during setup. In this case GetProfileSyncService will return NULL,
  // but we still need to call PSS::SetSetupInProgress(false). For this purpose
  // call FinishProfileSyncServiceSetup() function.
  ProfileSyncService* GetProfileSyncService();

  void FinishProfileSyncServiceSetup();

  // Displays the settings UI in a new tab. Brings up the advanced sync settings
  // dialog if |configure_sync| is true.
  void ShowSettingsPageInNewTab(bool configure_sync);

  // Displays the sync configuration UI in the provided web contents.
  void ShowSyncSettingsPageInWebContents(content::WebContents* contents);

  // Shows the post-signin confirmation bubble. If |custom_message| is empty,
  // the default "You are signed in" message is displayed.
  void DisplayFinalConfirmationBubble(const string16& custom_message);

  // Makes sure browser_ points to a valid browser (opens a new browser if
  // necessary). Useful in the case where the user has created a new Profile as
  // part of the signin process.
  void EnsureBrowser();

  Profile* profile_;
  Browser* browser_;
  scoped_ptr<SigninTracker> signin_tracker_;
  StartSyncMode start_mode_;
  chrome::HostDesktopType desktop_type_;
  bool force_same_tab_navigation_;
  ConfirmationRequired confirmation_required_;
  signin::Source source_;

  // Callback executed when sync setup succeeds or fails.
  Callback sync_setup_completed_callback_;

  base::WeakPtrFactory<OneClickSigninSyncStarter> weak_pointer_factory_;

#if defined(ENABLE_CONFIGURATION_POLICY)
  // CloudPolicyClient reference we keep while determining whether to create
  // a new profile for an enterprise user or not.
  scoped_ptr<policy::CloudPolicyClient> policy_client_;
#endif

  DISALLOW_COPY_AND_ASSIGN(OneClickSigninSyncStarter);
};


#endif  // CHROME_BROWSER_UI_SYNC_ONE_CLICK_SIGNIN_SYNC_STARTER_H_
