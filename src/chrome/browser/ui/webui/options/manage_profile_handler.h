// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_OPTIONS_MANAGE_PROFILE_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_OPTIONS_MANAGE_PROFILE_HANDLER_H_

#include <string>

#include "base/memory/weak_ptr.h"
#include "base/prefs/pref_change_registrar.h"
#include "chrome/browser/sync/profile_sync_service_observer.h"
#include "chrome/browser/ui/webui/options/options_ui.h"

namespace base {
class StringValue;
}

namespace options {

// Chrome personal stuff profiles manage overlay UI handler.
class ManageProfileHandler : public OptionsPageUIHandler,
                             public ProfileSyncServiceObserver {
 public:
  ManageProfileHandler();
  virtual ~ManageProfileHandler();

  // OptionsPageUIHandler:
  virtual void GetLocalizedValues(
      base::DictionaryValue* localized_strings) OVERRIDE;
  virtual void InitializeHandler() OVERRIDE;
  virtual void InitializePage() OVERRIDE;

  // WebUIMessageHandler:
  virtual void RegisterMessages() OVERRIDE;

  // content::NotificationObserver:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // ProfileSyncServiceObserver:
  virtual void OnStateChanged() OVERRIDE;

 private:
  // Callback for the "requestDefaultProfileIcons" message.
  // Sends the array of default profile icon URLs to WebUI.
  // |args| is of the form: [ {string} iconURL ]
  void RequestDefaultProfileIcons(const base::ListValue* args);

  // Callback for the "requestNewProfileDefaults" message.
  // Sends an object to WebUI of the form:
  //   { "name": profileName, "iconURL": iconURL }
  void RequestNewProfileDefaults(const base::ListValue* args);

  // Callback for the "requestExistingManagedUsers" message.
  // Sends an object to WebUI of the form:
  //   managedProfiles = {
  //     "Profile ID 1": "Profile Name 1",
  //     "Profile ID 2": "Profile Name 2",
  //     ...
  //   }
  // The object holds all existing managed users attached to the
  // custodian's profile who initiated the request except for
  // those managed users that are already existing of this machine.
  void RequestExistingManagedUsers(const base::ListValue* args);

  // Send all profile icons to the overlay.
  // |iconGrid| is the name of the grid to populate with icons (i.e.
  // "create-profile-icon-grid" or "manage-profile-icon-grid").
  void SendProfileIcons(const base::StringValue& icon_grid);

  // Sends an object to WebUI of the form:
  //   profileNames = {
  //     "Profile Name 1": true,
  //     "Profile Name 2": true,
  //     ...
  //   };
  // This is used to detect duplicate profile names.
  void SendProfileNames();

  // Callback for the "setProfileIconAndName" message. Sets the name and icon
  // of a given profile.
  // |args| is of the form: [
  //   /*string*/ profileFilePath,
  //   /*string*/ newProfileIconURL
  //   /*string*/ newProfileName,
  // ]
  void SetProfileIconAndName(const base::ListValue* args);

#if defined(ENABLE_SETTINGS_APP)
  // Callback for the "switchAppListProfile" message. Asks the
  // app_list_controller to change the profile registered for the AppList.
  // |args| is of the form: [ {string} profileFilePath ]
  void SwitchAppListProfile(const base::ListValue* args);
#endif

  // Callback for the 'profileIconSelectionChanged' message. Used to update the
  // name in the manager profile dialog based on the selected icon.
  void ProfileIconSelectionChanged(const base::ListValue* args);

  // Callback for the "requestHasProfileShortcuts" message, which is called
  // when editing an existing profile. Asks the profile shortcut manager whether
  // the profile has shortcuts and gets the result in |OnHasProfileShortcuts()|.
  // |args| is of the form: [ {string} profileFilePath ]
  void RequestHasProfileShortcuts(const base::ListValue* args);

  // Callback for the "RequestCreateProfileUpdate" message.
  // Sends the email address of the signed-in user, or an empty string if the
  // user is not signed in. Also sends information about whether managed users
  // may be created.
  void RequestCreateProfileUpdate(const base::ListValue* args);

  // When the pref allowing managed-user creation changes, sends the new value
  // to the UI.
  void OnCreateManagedUserPrefChange();

  // Callback invoked from the profile manager indicating whether the profile
  // being edited has any desktop shortcuts.
  void OnHasProfileShortcuts(bool has_shortcuts);

  // Callback for the "addProfileShortcut" message, which is called when editing
  // an existing profile and the user clicks the "Add desktop shortcut" button.
  // Adds a desktop shortcut for the profile.
  void AddProfileShortcut(const base::ListValue* args);

  // Callback for the "removeProfileShortcut" message, which is called when
  // editing an existing profile and the user clicks the "Remove desktop
  // shortcut" button. Removes the desktop shortcut for the profile.
  void RemoveProfileShortcut(const base::ListValue* args);

  // URL for the current profile's GAIA picture.
  std::string gaia_picture_url_;

  // For generating weak pointers to itself for callbacks.
  base::WeakPtrFactory<ManageProfileHandler> weak_factory_;

  // Used to observe the preference that allows creating managed users, which
  // can be changed by policy.
  PrefChangeRegistrar pref_change_registrar_;

  DISALLOW_COPY_AND_ASSIGN(ManageProfileHandler);
};

}  // namespace options

#endif  // CHROME_BROWSER_UI_WEBUI_OPTIONS_MANAGE_PROFILE_HANDLER_H_
