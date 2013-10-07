// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_PREFS_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_PREFS_H_

#include <set>
#include <string>
#include <vector>

#include "base/memory/linked_ptr.h"
#include "base/memory/scoped_ptr.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/extensions/blacklist.h"
#include "chrome/browser/extensions/extension_scoped_prefs.h"
#include "chrome/browser/prefs/scoped_user_pref_update.h"
#include "chrome/common/extensions/extension.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service.h"
#include "extensions/common/url_pattern_set.h"
#include "sync/api/string_ordinal.h"

class ExtensionPrefValueMap;
class ExtensionSorting;
class PrefService;
class Profile;

namespace user_prefs {
class PrefRegistrySyncable;
}

namespace extensions {
class ContentSettingsStore;
class ExtensionPrefsUninstallExtension;
class URLPatternSet;

// Class for managing global and per-extension preferences.
//
// This class distinguishes the following kinds of preferences:
// - global preferences:
//       internal state for the extension system in general, not associated
//       with an individual extension, such as lastUpdateTime.
// - per-extension preferences:
//       meta-preferences describing properties of the extension like
//       installation time, whether the extension is enabled, etc.
// - extension controlled preferences:
//       browser preferences that an extension controls. For example, an
//       extension could use the proxy API to specify the browser's proxy
//       preference. Extension-controlled preferences are stored in
//       PrefValueStore::extension_prefs(), which this class populates and
//       maintains as the underlying extensions change.
class ExtensionPrefs : public ExtensionScopedPrefs,
                       public BrowserContextKeyedService {
 public:
  // Key name for a preference that keeps track of per-extension settings. This
  // is a dictionary object read from the Preferences file, keyed off of
  // extension ids.
  static const char kExtensionsPref[];

  // Key for what version chrome was last time the extension prefs were loaded.
  static const char kExtensionsLastChromeVersion[];

  typedef std::vector<linked_ptr<ExtensionInfo> > ExtensionsInfo;

  // Vector containing identifiers for preferences.
  typedef std::set<std::string> PrefKeySet;

  // This enum is used for the launch type the user wants to use for an
  // application.
  // Do not remove items or re-order this enum as it is used in preferences
  // and histograms.
  enum LaunchType {
    LAUNCH_PINNED,
    LAUNCH_REGULAR,
    LAUNCH_FULLSCREEN,
    LAUNCH_WINDOW,

    // Launch an app in the in the way a click on the NTP would,
    // if no user pref were set.  Update this constant to change
    // the default for the NTP and chrome.management.launchApp().
    LAUNCH_DEFAULT = LAUNCH_REGULAR
  };

  // This enum is used to store the reason an extension's install has been
  // delayed.  Do not remove items or re-order this enum as it is used in
  // preferences.
  enum DelayReason {
    DELAY_REASON_NONE = 0,
    DELAY_REASON_GC = 1,
    DELAY_REASON_WAIT_FOR_IDLE = 2,
    DELAY_REASON_WAIT_FOR_IMPORTS = 3,
  };


  // Creates base::Time classes. The default implementation is just to return
  // the current time, but tests can inject alternative implementations.
  class TimeProvider {
   public:
    TimeProvider();

    virtual ~TimeProvider();

    // By default, returns the current time (base::Time::Now()).
    virtual base::Time GetCurrentTime() const;

   private:
    DISALLOW_COPY_AND_ASSIGN(TimeProvider);
  };

  // A wrapper around a ScopedUserPrefUpdate, which allows us to access the
  // entry of a particular key for an extension. Use this if you need a mutable
  // record of a dictionary or list in the current settings. Otherwise, prefer
  // ReadPrefAsT() and UpdateExtensionPref() methods.
  template <typename T, base::Value::Type type_enum_value>
  class ScopedUpdate {
   public:
    ScopedUpdate(ExtensionPrefs* prefs,
                 const std::string& extension_id,
                 const std::string& key);
    virtual ~ScopedUpdate();

    // Returns a mutable value for the key (ownership remains with the prefs),
    // if one exists. Otherwise, returns NULL.
    virtual T* Get();

    // Creates and returns a mutable value for the key (the prefs own the new
    // value), if one does not already exist. Otherwise, returns the current
    // value.
    virtual T* Create();

   private:
    DISALLOW_COPY_AND_ASSIGN(ScopedUpdate);

    DictionaryPrefUpdate update_;
    const std::string extension_id_;
    const std::string key_;
  };
  typedef ScopedUpdate<base::DictionaryValue, base::Value::TYPE_DICTIONARY>
      ScopedDictionaryUpdate;
  typedef ScopedUpdate<base::ListValue, base::Value::TYPE_LIST>
      ScopedListUpdate;

  // Creates and initializes an ExtensionPrefs object.
  // Does not take ownership of |prefs| and |extension_pref_value_map|.
  // If |extensions_disabled| is true, extension controlled preferences and
  // content settings do not become effective.
  static ExtensionPrefs* Create(
      PrefService* prefs,
      const base::FilePath& root_dir,
      ExtensionPrefValueMap* extension_pref_value_map,
      bool extensions_disabled);

  // A version of Create which allows injection of a custom base::Time provider.
  // Use this as needed for testing.
  static ExtensionPrefs* Create(
      PrefService* prefs,
      const base::FilePath& root_dir,
      ExtensionPrefValueMap* extension_pref_value_map,
      bool extensions_disabled,
      scoped_ptr<TimeProvider> time_provider);

  virtual ~ExtensionPrefs();

  // Convenience function to get the ExtensionPrefs for a Profile.
  static ExtensionPrefs* Get(Profile* profile);

  // Returns all installed extensions from extension preferences provided by
  // |pref_service|. This is exposed for ProtectedPrefsWatcher because it needs
  // access to the extension ID list before the ExtensionService is initialized.
  static ExtensionIdList GetExtensionsFrom(const PrefService* pref_service);

  // Returns true if the specified external extension was uninstalled by the
  // user.
  bool IsExternalExtensionUninstalled(const std::string& id) const;

  // Checks whether |extension_id| is disabled. If there's no state pref for
  // the extension, this will return false. Generally you should use
  // ExtensionService::IsExtensionEnabled instead.
  bool IsExtensionDisabled(const std::string& id) const;

  // Get/Set the order that the browser actions appear in the toolbar.
  ExtensionIdList GetToolbarOrder();
  void SetToolbarOrder(const ExtensionIdList& extension_ids);

  // Called when an extension is installed, so that prefs get created.
  // If |page_ordinal| is an invalid ordinal, then a page will be found
  // for the App.
  void OnExtensionInstalled(const Extension* extension,
                            Extension::State initial_state,
                            Blacklist::BlacklistState blacklist_state,
                            const syncer::StringOrdinal& page_ordinal);

  // Called when an extension is uninstalled, so that prefs get cleaned up.
  void OnExtensionUninstalled(const std::string& extension_id,
                              const Manifest::Location& location,
                              bool external_uninstall);

  // Called to change the extension's state when it is enabled/disabled.
  void SetExtensionState(const std::string& extension_id, Extension::State);

  // Populates |out| with the ids of all installed extensions.
  void GetExtensions(ExtensionIdList* out);

  // ExtensionScopedPrefs methods:
  virtual void UpdateExtensionPref(const std::string& id,
                                   const std::string& key,
                                   base::Value* value) OVERRIDE;

  virtual void DeleteExtensionPrefs(const std::string& id) OVERRIDE;

  virtual bool ReadPrefAsBoolean(const std::string& extension_id,
                                 const std::string& pref_key,
                                 bool* out_value) const OVERRIDE;

  virtual bool ReadPrefAsInteger(const std::string& extension_id,
                                 const std::string& pref_key,
                                 int* out_value) const OVERRIDE;

  virtual bool ReadPrefAsString(const std::string& extension_id,
                                const std::string& pref_key,
                                std::string* out_value) const OVERRIDE;

  virtual bool ReadPrefAsList(const std::string& extension_id,
                              const std::string& pref_key,
                              const base::ListValue** out_value) const OVERRIDE;

  virtual bool ReadPrefAsDictionary(
      const std::string& extension_id,
      const std::string& pref_key,
      const base::DictionaryValue** out_value) const OVERRIDE;

  virtual bool HasPrefForExtension(const std::string& extension_id) const
      OVERRIDE;

  // Did the extension ask to escalate its permission during an upgrade?
  bool DidExtensionEscalatePermissions(const std::string& id);

  // If |did_escalate| is true, the preferences for |extension| will be set to
  // require the install warning when the user tries to enable.
  void SetDidExtensionEscalatePermissions(
      const Extension* extension,
      bool did_escalate);

  // Getter and setters for disabled reason.
  int GetDisableReasons(const std::string& extension_id);
  void AddDisableReason(const std::string& extension_id,
                        Extension::DisableReason disable_reason);
  void RemoveDisableReason(const std::string& extension_id,
                           Extension::DisableReason disable_reason);
  void ClearDisableReasons(const std::string& extension_id);

  // Gets the set of extensions that have been blacklisted in prefs.
  std::set<std::string> GetBlacklistedExtensions();

  // Sets whether the extension with |id| is blacklisted.
  void SetExtensionBlacklisted(const std::string& extension_id,
                               bool is_blacklisted);

  // Returns the version string for the currently installed extension, or
  // the empty string if not found.
  std::string GetVersionString(const std::string& extension_id);

  // Re-writes the extension manifest into the prefs.
  // Called to change the extension's manifest when it's re-localized.
  void UpdateManifest(const Extension* extension);

  // Returns extension path based on extension ID, or empty FilePath on error.
  base::FilePath GetExtensionPath(const std::string& extension_id);

  // Returns base extensions install directory.
  const base::FilePath& install_directory() const { return install_directory_; }

  // Returns whether the extension with |id| has its blacklist bit set.
  //
  // WARNING: this only checks the extension's entry in prefs, so by definition
  // can only check extensions that prefs knows about. There may be other
  // sources of blacklist information, such as safebrowsing. You probably want
  // to use Blacklist::GetBlacklistedIDs rather than this method.
  bool IsExtensionBlacklisted(const std::string& id) const;

  // Increment the count of how many times we prompted the user to acknowledge
  // the given extension, and return the new count.
  int IncrementAcknowledgePromptCount(const std::string& extension_id);

  // Whether the user has acknowledged an external extension.
  bool IsExternalExtensionAcknowledged(const std::string& extension_id);
  void AcknowledgeExternalExtension(const std::string& extension_id);

  // Whether the user has acknowledged a blacklisted extension.
  bool IsBlacklistedExtensionAcknowledged(const std::string& extension_id);
  void AcknowledgeBlacklistedExtension(const std::string& extension_id);

  // Whether the external extension was installed during the first run
  // of this profile.
  bool IsExternalInstallFirstRun(const std::string& extension_id);
  void SetExternalInstallFirstRun(const std::string& extension_id);

  // Returns true if the extension notification code has already run for the
  // first time for this profile. Currently we use this flag to mean that any
  // extensions that would trigger notifications should get silently
  // acknowledged. This is a fuse. Calling it the first time returns false.
  // Subsequent calls return true. It's not possible through an API to ever
  // reset it. Don't call it unless you mean it!
  bool SetAlertSystemFirstRun();

  // Checks if extensions are blacklisted by default, by policy.
  // The ManagementPolicy::Provider methods also take this into account, and
  // should be used instead when the extension ID is known.
  bool ExtensionsBlacklistedByDefault() const;

  // Returns the last value set via SetLastPingDay. If there isn't such a
  // pref, the returned Time will return true for is_null().
  base::Time LastPingDay(const std::string& extension_id) const;

  // The time stored is based on the server's perspective of day start time, not
  // the client's.
  void SetLastPingDay(const std::string& extension_id, const base::Time& time);

  // Similar to the 2 above, but for the extensions blacklist.
  base::Time BlacklistLastPingDay() const;
  void SetBlacklistLastPingDay(const base::Time& time);

  // Similar to LastPingDay/SetLastPingDay, but for sending "days since active"
  // ping.
  base::Time LastActivePingDay(const std::string& extension_id);
  void SetLastActivePingDay(const std::string& extension_id,
                            const base::Time& time);

  // A bit we use for determining if we should send the "days since active"
  // ping. A value of true means the item has been active (launched) since the
  // last update check.
  bool GetActiveBit(const std::string& extension_id);
  void SetActiveBit(const std::string& extension_id, bool active);

  // Returns the granted permission set for the extension with |extension_id|,
  // and NULL if no preferences were found for |extension_id|.
  // This passes ownership of the returned set to the caller.
  PermissionSet* GetGrantedPermissions(const std::string& extension_id);

  // Adds |permissions| to the granted permissions set for the extension with
  // |extension_id|. The new granted permissions set will be the union of
  // |permissions| and the already granted permissions.
  void AddGrantedPermissions(const std::string& extension_id,
                             const PermissionSet* permissions);

  // As above, but subtracts the given |permissions| from the granted set.
  void RemoveGrantedPermissions(const std::string& extension_id,
                                const PermissionSet* permissions);

  // Gets the active permission set for the specified extension. This may
  // differ from the permissions in the manifest due to the optional
  // permissions API. This passes ownership of the set to the caller.
  PermissionSet* GetActivePermissions(const std::string& extension_id);

  // Sets the active |permissions| for the extension with |extension_id|.
  void SetActivePermissions(const std::string& extension_id,
                            const PermissionSet* permissions);

  // Records whether or not this extension is currently running.
  void SetExtensionRunning(const std::string& extension_id, bool is_running);

  // Returns whether or not this extension is marked as running. This is used to
  // restart apps across browser restarts.
  bool IsExtensionRunning(const std::string& extension_id);

  // Set/Get whether or not the app is active. Used to force a launch of apps
  // that don't handle onRestarted() on a restart. We can only safely do that if
  // the app was active when it was last running.
  void SetIsActive(const std::string& extension_id, bool is_active);
  bool IsActive(const std::string& extension_id);

  // Returns true if the user enabled this extension to be loaded in incognito
  // mode.
  //
  // IMPORTANT: you probably want to use ExtensionService::IsIncognitoEnabled
  // instead of this method.
  bool IsIncognitoEnabled(const std::string& extension_id);
  void SetIsIncognitoEnabled(const std::string& extension_id, bool enabled);

  // Returns true if the user has chosen to allow this extension to inject
  // scripts into pages with file URLs.
  bool AllowFileAccess(const std::string& extension_id);
  void SetAllowFileAccess(const std::string& extension_id, bool allow);
  bool HasAllowFileAccessSetting(const std::string& extension_id) const;

  // Get the launch type preference.  If no preference is set, return
  // |default_pref_value|.
  LaunchType GetLaunchType(const Extension* extension,
                           LaunchType default_pref_value);

  void SetLaunchType(const std::string& extension_id, LaunchType launch_type);

  // Find the right launch container based on the launch type.
  // If |extension|'s prefs do not have a launch type set, then
  // use |default_pref_value|.
  extension_misc::LaunchContainer GetLaunchContainer(
      const Extension* extension,
      LaunchType default_pref_value);

  // Saves ExtensionInfo for each installed extension with the path to the
  // version directory and the location. Blacklisted extensions won't be saved
  // and neither will external extensions the user has explicitly uninstalled.
  // Caller takes ownership of returned structure.
  scoped_ptr<ExtensionsInfo> GetInstalledExtensionsInfo() const;

  // Returns the ExtensionInfo from the prefs for the given extension. If the
  // extension is not present, NULL is returned.
  scoped_ptr<ExtensionInfo> GetInstalledExtensionInfo(
      const std::string& extension_id) const;

  // We've downloaded an updated .crx file for the extension, but are waiting
  // to install it.
  void SetDelayedInstallInfo(const Extension* extension,
                             Extension::State initial_state,
                             Blacklist::BlacklistState blacklist_state,
                             DelayReason delay_reason,
                             const syncer::StringOrdinal& page_ordinal);

  // Removes any delayed install information we have for the given
  // |extension_id|. Returns true if there was info to remove; false otherwise.
  bool RemoveDelayedInstallInfo(const std::string& extension_id);

  // Update the prefs to finish the update for an extension.
  bool FinishDelayedInstallInfo(const std::string& extension_id);

  // Returns the ExtensionInfo from the prefs for delayed install information
  // for |extension_id|, if we have any. Otherwise returns NULL.
  scoped_ptr<ExtensionInfo> GetDelayedInstallInfo(
      const std::string& extension_id) const;

  DelayReason GetDelayedInstallReason(const std::string& extension_id) const;

  // Returns information about all the extensions that have delayed install
  // information.
  scoped_ptr<ExtensionsInfo> GetAllDelayedInstallInfo() const;

  // Returns true if the user repositioned the app on the app launcher via drag
  // and drop.
  bool WasAppDraggedByUser(const std::string& extension_id);

  // Sets a flag indicating that the user repositioned the app on the app
  // launcher by drag and dropping it.
  void SetAppDraggedByUser(const std::string& extension_id);

  // Returns true if there is an extension which controls the preference value
  //  for |pref_key| *and* it is specific to incognito mode.
  bool HasIncognitoPrefValue(const std::string& pref_key);

  // Returns the creation flags mask for the extension.
  int GetCreationFlags(const std::string& extension_id) const;

  // Returns the creation flags mask for a delayed install extension.
  int GetDelayedInstallCreationFlags(const std::string& extension_id) const;

  // Returns true if the extension was installed from the Chrome Web Store.
  bool IsFromWebStore(const std::string& extension_id) const;

  // Returns true if the extension was installed from an App generated from a
  // bookmark.
  bool IsFromBookmark(const std::string& extension_id) const;

  // Returns true if the extension was installed as a default app.
  bool WasInstalledByDefault(const std::string& extension_id) const;

  // Helper method to acquire the installation time of an extension.
  // Returns base::Time() if the installation time could not be parsed or
  // found.
  base::Time GetInstallTime(const std::string& extension_id) const;

  static void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

  bool extensions_disabled() { return extensions_disabled_; }

  ContentSettingsStore* content_settings_store() {
    return content_settings_store_.get();
  }

  // The underlying PrefService.
  PrefService* pref_service() const { return prefs_; }

  // The underlying ExtensionSorting.
  ExtensionSorting* extension_sorting() const {
    return extension_sorting_.get();
  }

  // Describes the URLs that are able to install extensions. See
  // prefs::kExtensionAllowedInstallSites for more information.
  URLPatternSet GetAllowedInstallSites();

  // Schedules garbage collection of an extension's on-disk data on the next
  // start of this ExtensionService. Applies only to extensions with isolated
  // storage.
  void SetNeedsStorageGarbageCollection(bool value);
  bool NeedsStorageGarbageCollection();

  // Used by ShellWindowGeometryCache to persist its cache. These methods
  // should not be called directly.
  const base::DictionaryValue* GetGeometryCache(
        const std::string& extension_id) const;
  void SetGeometryCache(const std::string& extension_id,
                        scoped_ptr<base::DictionaryValue> cache);

 private:
  friend class ExtensionPrefsBlacklistedExtensions;  // Unit test.
  friend class ExtensionPrefsUninstallExtension;     // Unit test.

  // See the Create methods.
  ExtensionPrefs(PrefService* prefs,
                 const base::FilePath& root_dir,
                 ExtensionPrefValueMap* extension_pref_value_map,
                 scoped_ptr<TimeProvider> time_provider,
                 bool extensions_disabled);

  // Converts absolute paths in the pref to paths relative to the
  // install_directory_.
  void MakePathsRelative();

  // Converts internal relative paths to be absolute. Used for export to
  // consumers who expect full paths.
  void MakePathsAbsolute(base::DictionaryValue* dict);

  // Helper function used by GetInstalledExtensionInfo() and
  // GetDelayedInstallInfo() to construct an ExtensionInfo from the provided
  // |extension| dictionary.
  scoped_ptr<ExtensionInfo> GetInstalledInfoHelper(
      const std::string& extension_id,
      const base::DictionaryValue* extension) const;

  // Interprets the list pref, |pref_key| in |extension_id|'s preferences, as a
  // URLPatternSet. The |valid_schemes| specify how to parse the URLPatterns.
  bool ReadPrefAsURLPatternSet(const std::string& extension_id,
                               const std::string& pref_key,
                               URLPatternSet* result,
                               int valid_schemes);

  // Converts |new_value| to a list of strings and sets the |pref_key| pref
  // belonging to |extension_id|.
  void SetExtensionPrefURLPatternSet(const std::string& extension_id,
                                     const std::string& pref_key,
                                     const URLPatternSet& new_value);

  // Read the boolean preference entry and return true if the preference exists
  // and the preference's value is true; false otherwise.
  bool ReadPrefAsBooleanAndReturn(const std::string& extension_id,
                                  const std::string& key) const;

  // Interprets |pref_key| in |extension_id|'s preferences as an
  // PermissionSet, and passes ownership of the set to the caller.
  PermissionSet* ReadPrefAsPermissionSet(const std::string& extension_id,
                                         const std::string& pref_key);

  // Converts the |new_value| to its value and sets the |pref_key| pref
  // belonging to |extension_id|.
  void SetExtensionPrefPermissionSet(const std::string& extension_id,
                                     const std::string& pref_key,
                                     const PermissionSet* new_value);

  // Returns an immutable dictionary for extension |id|'s prefs, or NULL if it
  // doesn't exist.
  const base::DictionaryValue* GetExtensionPref(const std::string& id) const;

  // Fix missing preference entries in the extensions that are were introduced
  // in a later Chrome version.
  void FixMissingPrefs(const ExtensionIdList& extension_ids);

  // Installs the persistent extension preferences into |prefs_|'s extension
  // pref store. Does nothing if extensions_disabled_ is true.
  void InitPrefStore();

  // Migrates the permissions data in the pref store.
  void MigratePermissions(const ExtensionIdList& extension_ids);

  // Migrates the disable reasons from a single enum to a bit mask.
  void MigrateDisableReasons(const ExtensionIdList& extension_ids);

  // Checks whether there is a state pref for the extension and if so, whether
  // it matches |check_state|.
  bool DoesExtensionHaveState(const std::string& id,
                              Extension::State check_state) const;

  // Helper function to Get/Set array of strings from/to prefs.
  ExtensionIdList GetExtensionPrefAsVector(const char* pref);
  void SetExtensionPrefFromVector(const char* pref,
                                  const ExtensionIdList& extension_ids);

  // Helper function to populate |extension_dict| with the values needed
  // by a newly installed extension. Work is broken up between this
  // function and FinishExtensionInfoPrefs() to accomodate delayed
  // installations.
  void PopulateExtensionInfoPrefs(const Extension* extension,
                                  const base::Time install_time,
                                  Extension::State initial_state,
                                  Blacklist::BlacklistState blacklist_state,
                                  base::DictionaryValue* extension_dict);

  // Helper function to complete initialization of the values in
  // |extension_dict| for an extension install. Also see
  // PopulateExtensionInfoPrefs().
  void FinishExtensionInfoPrefs(
      const std::string& extension_id,
      const base::Time install_time,
      bool needs_sort_ordinal,
      const syncer::StringOrdinal& suggested_page_ordinal,
      base::DictionaryValue* extension_dict);

  // The pref service specific to this set of extension prefs. Owned by profile.
  PrefService* prefs_;

  // Base extensions install directory.
  base::FilePath install_directory_;

  // Weak pointer, owned by Profile.
  ExtensionPrefValueMap* extension_pref_value_map_;

  // Contains all the logic for handling the order for various extension
  // properties.
  scoped_ptr<ExtensionSorting> extension_sorting_;

  scoped_refptr<ContentSettingsStore> content_settings_store_;

  scoped_ptr<TimeProvider> time_provider_;

  bool extensions_disabled_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionPrefs);
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_PREFS_H_
