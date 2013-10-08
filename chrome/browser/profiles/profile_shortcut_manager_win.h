// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PROFILES_PROFILE_SHORTCUT_MANAGER_WIN_H_
#define CHROME_BROWSER_PROFILES_PROFILE_SHORTCUT_MANAGER_WIN_H_

#include "base/callback.h"
#include "chrome/browser/profiles/profile_shortcut_manager.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"

class BrowserDistribution;

// Internal free-standing functions that are exported here for testing.
namespace profiles {
namespace internal {

// Returns the full path to the profile icon file.
base::FilePath GetProfileIconPath(const base::FilePath& profile_path);

// Returns the default shortcut filename for the given profile name,
// given |distribution|. Returns a filename appropriate for a
// single-user installation if |profile_name| is empty.
string16 GetShortcutFilenameForProfile(const string16& profile_name,
                                       BrowserDistribution* distribution);

// Returns the command-line flags to launch Chrome with the given profile.
string16 CreateProfileShortcutFlags(const base::FilePath& profile_path);

}  // namespace internal
}  // namespace profiles

class ProfileShortcutManagerWin : public ProfileShortcutManager,
                                  public ProfileInfoCacheObserver,
                                  public content::NotificationObserver {
 public:
  // Specifies whether only the existing shortcut should be updated, a new
  // shortcut should be created if none exist, or only the icon for this profile
  // should be created in the profile directory.
  enum CreateOrUpdateMode {
    UPDATE_EXISTING_ONLY,
    CREATE_WHEN_NONE_FOUND,
    CREATE_OR_UPDATE_ICON_ONLY,
  };
  // Specifies whether non-profile shortcuts should be updated.
  enum NonProfileShortcutAction {
    IGNORE_NON_PROFILE_SHORTCUTS,
    UPDATE_NON_PROFILE_SHORTCUTS,
  };

  explicit ProfileShortcutManagerWin(ProfileManager* manager);
  virtual ~ProfileShortcutManagerWin();

  // ProfileShortcutManager implementation:
  virtual void CreateOrUpdateProfileIcon(
      const base::FilePath& profile_path,
      const base::Closure& callback) OVERRIDE;
  virtual void CreateProfileShortcut(
      const base::FilePath& profile_path) OVERRIDE;
  virtual void RemoveProfileShortcuts(
      const base::FilePath& profile_path) OVERRIDE;
  virtual void HasProfileShortcuts(
      const base::FilePath& profile_path,
      const base::Callback<void(bool)>& callback) OVERRIDE;

  // ProfileInfoCacheObserver implementation:
  virtual void OnProfileAdded(const base::FilePath& profile_path) OVERRIDE;
  virtual void OnProfileWasRemoved(const base::FilePath& profile_path,
                                   const string16& profile_name) OVERRIDE;
  virtual void OnProfileNameChanged(const base::FilePath& profile_path,
                                    const string16& old_profile_name) OVERRIDE;
  virtual void OnProfileAvatarChanged(
      const base::FilePath& profile_path) OVERRIDE;

  // content::NotificationObserver implementation:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

 private:
  // Gives the profile path of an alternate profile than |profile_path|.
  // Must only be called when the number profiles is 2.
  base::FilePath GetOtherProfilePath(const base::FilePath& profile_path);

  // Creates or updates shortcuts for the profile at |profile_path| according
  // to the specified |create_mode| and |action|. This will always involve
  // creating or updating the icon file for this profile.
  // Calls |callback| on successful icon creation.
  void CreateOrUpdateShortcutsForProfileAtPath(
      const base::FilePath& profile_path,
      CreateOrUpdateMode create_mode,
      NonProfileShortcutAction action,
      const base::Closure& callback);

  ProfileManager* profile_manager_;

  content::NotificationRegistrar registrar_;

  DISALLOW_COPY_AND_ASSIGN(ProfileShortcutManagerWin);
};

#endif  // CHROME_BROWSER_PROFILES_PROFILE_SHORTCUT_MANAGER_WIN_H_
