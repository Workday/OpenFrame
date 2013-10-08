// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SEARCH_ENGINES_TEMPLATE_URL_SERVICE_H_
#define CHROME_BROWSER_SEARCH_ENGINES_TEMPLATE_URL_SERVICE_H_

#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/gtest_prod_util.h"
#include "base/memory/scoped_ptr.h"
#include "base/observer_list.h"
#include "base/prefs/pref_change_registrar.h"
#include "chrome/browser/search_engines/template_url_id.h"
#include "chrome/browser/webdata/web_data_service.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "sync/api/sync_change.h"
#include "sync/api/syncable_service.h"

class GURL;
class PrefService;
class Profile;
class SearchHostToURLsMap;
class SearchTermsData;
class TemplateURLServiceObserver;

namespace syncer {
class SyncData;
class SyncErrorFactory;
}

namespace history {
struct URLVisitedDetails;
}

// TemplateURLService is the backend for keywords. It's used by
// KeywordAutocomplete.
//
// TemplateURLService stores a vector of TemplateURLs. The TemplateURLs are
// persisted to the database maintained by WebDataService. *ALL* mutations
// to the TemplateURLs must funnel through TemplateURLService. This allows
// TemplateURLService to notify listeners of changes as well as keep the
// database in sync.
//
// There is a TemplateURLService per Profile.
//
// TemplateURLService does not load the vector of TemplateURLs in its
// constructor (except for testing). Use the Load method to trigger a load.
// When TemplateURLService has completed loading, observers are notified via
// OnTemplateURLServiceChanged as well as the TEMPLATE_URL_SERVICE_LOADED
// notification message.
//
// TemplateURLService takes ownership of any TemplateURL passed to it. If there
// is a WebDataService, deletion is handled by WebDataService, otherwise
// TemplateURLService handles deletion.

class TemplateURLService : public WebDataServiceConsumer,
                           public BrowserContextKeyedService,
                           public content::NotificationObserver,
                           public syncer::SyncableService {
 public:
  typedef std::map<std::string, std::string> QueryTerms;
  typedef std::vector<TemplateURL*> TemplateURLVector;
  // Type for a static function pointer that acts as a time source.
  typedef base::Time(TimeProvider)();
  typedef std::map<std::string, syncer::SyncData> SyncDataMap;

  // Struct used for initializing the data store with fake data.
  // Each initializer is mapped to a TemplateURL.
  struct Initializer {
    const char* const keyword;
    const char* const url;
    const char* const content;
  };

  // Struct describes a search engine added by an extension.
  struct ExtensionKeyword {
    ExtensionKeyword(const std::string& id,
                     const std::string& name,
                     const std::string& keyword);
    ~ExtensionKeyword();

    std::string extension_id;
    std::string extension_name;
    std::string extension_keyword;
  };

  explicit TemplateURLService(Profile* profile);
  // The following is for testing.
  TemplateURLService(const Initializer* initializers, const int count);
  virtual ~TemplateURLService();

  // Generates a suitable keyword for the specified url, which must be valid.
  // This is guaranteed not to return an empty string, since TemplateURLs should
  // never have an empty keyword.
  static string16 GenerateKeyword(const GURL& url);

  // Removes any unnecessary characters from a user input keyword.
  // This removes the leading scheme, "www." and any trailing slash.
  static string16 CleanUserInputKeyword(const string16& keyword);

  // Returns the search url for t_url.  Returns an empty GURL if t_url has no
  // url().
  // NOTE: |t_url| is non-const in this version because of the need to access
  // t_url->profile().
  static GURL GenerateSearchURL(TemplateURL* t_url);

  // Just like GenerateSearchURL except that it takes SearchTermsData to supply
  // the data for some search terms, e.g. so this can be used on threads other
  // than the UI thread.  See the various TemplateURLRef::XXXUsingTermsData()
  // functions.
  static GURL GenerateSearchURLUsingTermsData(
      const TemplateURL* t_url,
      const SearchTermsData& search_terms_data);

  // Returns true if there is no TemplateURL that conflicts with the
  // keyword/url pair, or there is one but it can be replaced. If there is an
  // existing keyword that can be replaced and template_url_to_replace is
  // non-NULL, template_url_to_replace is set to the keyword to replace.
  //
  // url gives the url of the search query. The url is used to avoid generating
  // a TemplateURL for an existing TemplateURL that shares the same host.
  bool CanReplaceKeyword(const string16& keyword,
                         const GURL& url,
                         TemplateURL** template_url_to_replace);

  // Returns (in |matches|) all TemplateURLs whose keywords begin with |prefix|,
  // sorted shortest keyword-first. If |support_replacement_only| is true, only
  // TemplateURLs that support replacement are returned.
  void FindMatchingKeywords(const string16& prefix,
                            bool support_replacement_only,
                            TemplateURLVector* matches) const;

  // Looks up |keyword| and returns the element it maps to.  Returns NULL if
  // the keyword was not found.
  // The caller should not try to delete the returned pointer; the data store
  // retains ownership of it.
  TemplateURL* GetTemplateURLForKeyword(const string16& keyword);

  // Returns that TemplateURL with the specified GUID, or NULL if not found.
  // The caller should not try to delete the returned pointer; the data store
  // retains ownership of it.
  TemplateURL* GetTemplateURLForGUID(const std::string& sync_guid);

  // Returns the first TemplateURL found with a URL using the specified |host|,
  // or NULL if there are no such TemplateURLs
  TemplateURL* GetTemplateURLForHost(const std::string& host);

  // Takes ownership of |template_url| and adds it to this model.  For obvious
  // reasons, it is illegal to Add() the same |template_url| pointer twice.
  void Add(TemplateURL* template_url);

  // Like Add(), but overwrites the |template_url|'s values with the provided
  // ones.
  void AddAndSetProfile(TemplateURL* template_url, Profile* profile);
  void AddWithOverrides(TemplateURL* template_url,
                        const string16& short_name,
                        const string16& keyword,
                        const std::string& url);

  // Removes the keyword from the model. This deletes the supplied TemplateURL.
  // This fails if the supplied template_url is the default search provider.
  void Remove(TemplateURL* template_url);

  // Removes all auto-generated keywords that were created on or after the
  // date passed in.
  void RemoveAutoGeneratedSince(base::Time created_after);

  // Removes all auto-generated keywords that were created in the specified
  // range.
  void RemoveAutoGeneratedBetween(base::Time created_after,
                                  base::Time created_before);

  // Removes all auto-generated keywords that were created in the specified
  // range for a specified |origin|. If |origin| is empty, deletes all
  // auto-generated keywords in the range.
  void RemoveAutoGeneratedForOriginBetween(const GURL& origin,
                                           base::Time created_after,
                                           base::Time created_before);

  // Adds a TemplateURL for an extension with an omnibox keyword.
  // Only 1 keyword is allowed for a given extension. If a keyword
  // already exists for this extension, does nothing.
  void RegisterExtensionKeyword(const std::string& extension_id,
                                const std::string& extension_name,
                                const std::string& keyword);

  // Removes the TemplateURL containing the keyword for the extension with the
  // given ID, if any.
  void UnregisterExtensionKeyword(const std::string& extension_id);

  // Returns the TemplateURL associated with the keyword for this extension.
  // This will work even if the user changed the keyword.
  TemplateURL* GetTemplateURLForExtension(const std::string& extension_id);

  // Returns the set of URLs describing the keywords. The elements are owned
  // by TemplateURLService and should not be deleted.
  TemplateURLVector GetTemplateURLs();

  // Increment the usage count of a keyword.
  // Called when a URL is loaded that was generated from a keyword.
  void IncrementUsageCount(TemplateURL* url);

  // Resets the title, keyword and search url of the specified TemplateURL.
  // The TemplateURL is marked as not replaceable.
  void ResetTemplateURL(TemplateURL* url,
                        const string16& title,
                        const string16& keyword,
                        const std::string& search_url);

  // Return true if the given |url| can be made the default.
  bool CanMakeDefault(const TemplateURL* url);

  // Set the default search provider.  |url| may be null.
  // This will assert if the default search is managed; the UI should not be
  // invoking this method in that situation.
  void SetDefaultSearchProvider(TemplateURL* url);

  // Returns the default search provider. If the TemplateURLService hasn't been
  // loaded, the default search provider is pulled from preferences.
  //
  // NOTE: At least in unittest mode, this may return NULL.
  TemplateURL* GetDefaultSearchProvider();

  // Returns true if the |url| is a search results page from the default search
  // provider.
  bool IsSearchResultsPageFromDefaultSearchProvider(const GURL& url);

  // Returns true if the default search is managed through group policy.
  bool is_default_search_managed() const { return is_default_search_managed_; }

  // Returns the default search specified in the prepopulated data, if it
  // exists.  If not, returns first URL in |template_urls_|, or NULL if that's
  // empty. The returned object is owned by TemplateURLService and can be
  // destroyed at any time so should be used right after the call.
  TemplateURL* FindNewDefaultSearchProvider();

  // Resets the search providers to the prepopulated engines plus any keywords
  // from currently-installed extensions.  The user will lose all auto-added
  // keywords from webpages, all edits to both normal and extension keywords,
  // and any keywords belonging to no-longer-installed extensions.
  // Modifications will be synced later.
  void ResetURLs();

  // Observers used to listen for changes to the model.
  // TemplateURLService does NOT delete the observers when deleted.
  void AddObserver(TemplateURLServiceObserver* observer);
  void RemoveObserver(TemplateURLServiceObserver* observer);

  // Loads the keywords. This has no effect if the keywords have already been
  // loaded.
  // Observers are notified when loading completes via the method
  // OnTemplateURLServiceChanged.
  void Load();

#if defined(UNIT_TEST)
  void set_loaded(bool value) { loaded_ = value; }
#endif

  // Whether or not the keywords have been loaded.
  bool loaded() { return loaded_; }

  // Notification that the keywords have been loaded.
  // This is invoked from WebDataService, and should not be directly
  // invoked.
  virtual void OnWebDataServiceRequestDone(
      WebDataService::Handle h,
      const WDTypedResult* result) OVERRIDE;

  // Returns the locale-direction-adjusted short name for the given keyword.
  // Also sets the out param to indicate whether the keyword belongs to an
  // extension.
  string16 GetKeywordShortName(const string16& keyword,
                               bool* is_extension_keyword);

  // content::NotificationObserver implementation.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // BrowserContextKeyedService implementation.
  virtual void Shutdown() OVERRIDE;

  // syncer::SyncableService implementation.

  // Returns all syncable TemplateURLs from this model as SyncData. This should
  // include every search engine and no Extension keywords.
  virtual syncer::SyncDataList GetAllSyncData(
      syncer::ModelType type) const OVERRIDE;
  // Process new search engine changes from Sync, merging them into our local
  // data. This may send notifications if local search engines are added,
  // updated or removed.
  virtual syncer::SyncError ProcessSyncChanges(
      const tracked_objects::Location& from_here,
      const syncer::SyncChangeList& change_list) OVERRIDE;
  // Merge initial search engine data from Sync and push any local changes up
  // to Sync. This may send notifications if local search engines are added,
  // updated or removed.
  virtual syncer::SyncMergeResult MergeDataAndStartSyncing(
      syncer::ModelType type,
      const syncer::SyncDataList& initial_sync_data,
      scoped_ptr<syncer::SyncChangeProcessor> sync_processor,
      scoped_ptr<syncer::SyncErrorFactory> sync_error_factory) OVERRIDE;
  virtual void StopSyncing(syncer::ModelType type) OVERRIDE;

  // Processes a local TemplateURL change for Sync. |turl| is the TemplateURL
  // that has been modified, and |type| is the Sync ChangeType that took place.
  // This may send a new SyncChange to the cloud. If our model has not yet been
  // associated with Sync, or if this is triggered by a Sync change, then this
  // does nothing.
  void ProcessTemplateURLChange(const tracked_objects::Location& from_here,
                                const TemplateURL* turl,
                                syncer::SyncChange::SyncChangeType type);

  Profile* profile() const { return profile_; }

  // Returns a SyncData with a sync representation of the search engine data
  // from |turl|.
  static syncer::SyncData CreateSyncDataFromTemplateURL(
      const TemplateURL& turl);

  // Creates a new heap-allocated TemplateURL* which is populated by overlaying
  // |sync_data| atop |existing_turl|.  |existing_turl| may be NULL; if not it
  // remains unmodified.  The caller owns the returned TemplateURL*.
  //
  // If the created TemplateURL is migrated in some way from out-of-date sync
  // data, an appropriate SyncChange is added to |change_list|.  If the sync
  // data is bad for some reason, an ACTION_DELETE change is added and the
  // function returns NULL.
  static TemplateURL* CreateTemplateURLFromTemplateURLAndSyncData(
      Profile* profile,
      TemplateURL* existing_turl,
      const syncer::SyncData& sync_data,
      syncer::SyncChangeList* change_list);

  // Returns a map mapping Sync GUIDs to pointers to syncer::SyncData.
  static SyncDataMap CreateGUIDToSyncDataMap(
      const syncer::SyncDataList& sync_data);

#if defined(UNIT_TEST)
  // Set a different time provider function, such as
  // base::MockTimeProvider::StaticNow, when testing calls to base::Time::Now.
  void set_time_provider(TimeProvider* time_provider) {
    time_provider_ = time_provider;
  }
#endif

 protected:
  // Cover method for the method of the same name on the HistoryService.
  // url is the one that was visited with the given search terms.
  //
  // This exists and is virtual for testing.
  virtual void SetKeywordSearchTermsForURL(const TemplateURL* t_url,
                                           const GURL& url,
                                           const string16& term);

 private:
  FRIEND_TEST_ALL_PREFIXES(TemplateURLServiceTest, TestManagedDefaultSearch);
  FRIEND_TEST_ALL_PREFIXES(TemplateURLServiceTest,
                           UpdateKeywordSearchTermsForURL);
  FRIEND_TEST_ALL_PREFIXES(TemplateURLServiceTest,
                           DontUpdateKeywordSearchForNonReplaceable);
  FRIEND_TEST_ALL_PREFIXES(TemplateURLServiceTest, ChangeGoogleBaseValue);
  FRIEND_TEST_ALL_PREFIXES(TemplateURLServiceTest, MergeDeletesUnusedProviders);
  FRIEND_TEST_ALL_PREFIXES(TemplateURLServiceSyncTest,
                           CreateSyncDataFromTemplateURL);
  FRIEND_TEST_ALL_PREFIXES(TemplateURLServiceSyncTest,
                           CreateTemplateURLFromSyncData);
  FRIEND_TEST_ALL_PREFIXES(TemplateURLServiceSyncTest, UniquifyKeyword);
  FRIEND_TEST_ALL_PREFIXES(TemplateURLServiceSyncTest,
                           ResolveSyncKeywordConflict);
  FRIEND_TEST_ALL_PREFIXES(TemplateURLServiceSyncTest, PreSyncDeletes);
  FRIEND_TEST_ALL_PREFIXES(TemplateURLServiceSyncTest,
                           IsLocalTemplateURLBetter);
  FRIEND_TEST_ALL_PREFIXES(TemplateURLServiceSyncTest, MergeInSyncTemplateURL);

  friend class TemplateURLServiceTestUtilBase;

  typedef std::map<string16, TemplateURL*> KeywordToTemplateMap;
  typedef std::map<std::string, TemplateURL*> GUIDToTemplateMap;

  // Declaration of values to be used in an enumerated histogram to tally
  // changes to the default search provider from various entry points. In
  // particular, we use this to see what proportion of changes are from Sync
  // entry points, to help spot erroneous Sync activity.
  enum DefaultSearchChangeOrigin {
    // Various known Sync entry points.
    DSP_CHANGE_SYNC_PREF,
    DSP_CHANGE_SYNC_ADD,
    DSP_CHANGE_SYNC_DELETE,
    DSP_CHANGE_SYNC_NOT_MANAGED,
    // "Other" origins. We differentiate between Sync and not Sync so we know if
    // certain changes were intentionally from the system, or possibly some
    // unintentional change from when we were Syncing.
    DSP_CHANGE_SYNC_UNINTENTIONAL,
    // All non-sync changes save PROFILE_RESET; we can't reorder the list for
    // clarity as this would screw up stat collection.
    DSP_CHANGE_OTHER,
    // Changed through "Profile Reset" feature.
    DSP_CHANGE_PROFILE_RESET,
    // Boundary value.
    DSP_CHANGE_MAX,
  };

  // Helper functor for FindMatchingKeywords(), for finding the range of
  // keywords which begin with a prefix.
  class LessWithPrefix;

  void Init(const Initializer* initializers, int num_initializers);

  void RemoveFromMaps(TemplateURL* template_url);

  // Removes the supplied template_url from the keyword maps. This searches
  // through all entries in the keyword map and does not generate the host or
  // keyword. This is used when the cached content of the TemplateURL changes.
  void RemoveFromKeywordMapByPointer(TemplateURL* template_url);

  void AddToMaps(TemplateURL* template_url);

  // Sets the keywords. This is used once the keywords have been loaded.
  // This does NOT notify the delegate or the database.
  void SetTemplateURLs(const TemplateURLVector& urls);

  // Transitions to the loaded state.
  void ChangeToLoadedState();

  // If there is a notification service, sends TEMPLATE_URL_SERVICE_LOADED
  // notification.
  void NotifyLoaded();

  // Saves enough of url to preferences so that it can be loaded from
  // preferences on start up.
  void SaveDefaultSearchProviderToPrefs(const TemplateURL* url);

  // Creates a TemplateURL that was previously saved to prefs via
  // SaveDefaultSearchProviderToPrefs or set via policy.
  // Returns true if successful, false otherwise.
  // If the user or the policy has opted for no default search, this
  // returns true but default_provider is set to NULL.
  // |*is_managed| specifies whether the default is managed via policy.
  bool LoadDefaultSearchProviderFromPrefs(
      scoped_ptr<TemplateURL>* default_provider,
      bool* is_managed);

  // Clears user preferences describing the default search engine.
  void ClearDefaultProviderFromPrefs();

  // Returns true if there is no TemplateURL that has a search url with the
  // specified host, or the only TemplateURLs matching the specified host can
  // be replaced.
  bool CanReplaceKeywordForHost(const std::string& host,
                                TemplateURL** to_replace);

  // Returns true if the TemplateURL is replaceable. This doesn't look at the
  // uniqueness of the keyword or host and is intended to be called after those
  // checks have been done. This returns true if the TemplateURL doesn't appear
  // in the default list and is marked as safe_for_autoreplace.
  bool CanReplace(const TemplateURL* t_url);

  // Like GetTemplateURLForKeyword(), but ignores extension-provided keywords.
  TemplateURL* FindNonExtensionTemplateURLForKeyword(const string16& keyword);

  // Updates the information in |existing_turl| using the information from
  // |new_values|, but the ID for |existing_turl| is retained.  Notifying
  // observers is the responsibility of the caller.  Returns whether
  // |existing_turl| was found in |template_urls_| and thus could be updated.
  // |old_search_terms_data| is passed to SearchHostToURLsMap::Remove().
  //
  // NOTE: This should not be called with an extension keyword as there are no
  // updates needed in that case.
  bool UpdateNoNotify(TemplateURL* existing_turl,
                      const TemplateURL& new_values,
                      const SearchTermsData& old_search_terms_data);

  // If the TemplateURL comes from a prepopulated URL available in the current
  // country, update all its fields save for the keyword, short name and id so
  // that they match the internal prepopulated URL. TemplateURLs not coming from
  // a prepopulated URL are not modified.
  static void UpdateTemplateURLIfPrepopulated(TemplateURL* existing_turl,
                                              Profile* profile);

  // Returns the preferences we use.
  PrefService* GetPrefs();

  // Iterates through the TemplateURLs to see if one matches the visited url.
  // For each TemplateURL whose url matches the visited url
  // SetKeywordSearchTermsForURL is invoked.
  void UpdateKeywordSearchTermsForURL(
      const history::URLVisitedDetails& details);

  // If necessary, generates a visit for the site http:// + t_url.keyword().
  void AddTabToSearchVisit(const TemplateURL& t_url);

  // Invoked when the Google base URL has changed. Updates the mapping for all
  // TemplateURLs that have a replacement term of {google:baseURL} or
  // {google:baseSuggestURL}.
  void GoogleBaseURLChanged(const GURL& old_base_url);

  // Update the default search.  Called at initialization or when a managed
  // preference has changed.
  void UpdateDefaultSearch();

  // Set the default search provider even if it is managed. |url| may be null.
  // Caller is responsible for notifying observers.  Returns whether |url| was
  // found in |template_urls_| and thus could be made default.
  bool SetDefaultSearchProviderNoNotify(TemplateURL* url);

  // Adds a new TemplateURL to this model. TemplateURLService will own the
  // reference, and delete it when the TemplateURL is removed.
  // If |newly_adding| is false, we assume that this TemplateURL was already
  // part of the model in the past, and therefore we don't need to do things
  // like assign it an ID or notify sync.
  // This function guarantees that on return the model will not have two
  // non-extension TemplateURLs with the same keyword.  If that means that it
  // cannot add the provided argument, it will delete it and return false.
  // Caller is responsible for notifying observers if this function returns
  // true.
  bool AddNoNotify(TemplateURL* template_url, bool newly_adding);

  // Removes the keyword from the model. This deletes the supplied TemplateURL.
  // This fails if the supplied template_url is the default search provider.
  // Caller is responsible for notifying observers.
  void RemoveNoNotify(TemplateURL* template_url);

  // Notify the observers that the model has changed.  This is done only if the
  // model is loaded.
  void NotifyObservers();

  // Removes from the vector any template URL that was created because of
  // policy.  These TemplateURLs are freed and removed from the database.
  // Sets default_search_provider to NULL if it was one of them, unless it is
  // the same as the current default from preferences and it is managed.
  void RemoveProvidersCreatedByPolicy(
      TemplateURLVector* template_urls,
      TemplateURL** default_search_provider,
      TemplateURL* default_from_prefs);

  // Resets the sync GUID of the specified TemplateURL and persists the change
  // to the database. This does not notify observers.
  void ResetTemplateURLGUID(TemplateURL* url, const std::string& guid);

  // Attempts to generate a unique keyword for |turl| based on its original
  // keyword. If its keyword is already unique, that is returned. Otherwise, it
  // tries to return the autogenerated keyword if that is unique to the Service,
  // and finally it repeatedly appends special characters to the keyword until
  // it is unique to the Service. If |force| is true, then this will only
  // execute the special character appending functionality.
  string16 UniquifyKeyword(const TemplateURL& turl, bool force);

  // Returns true iff |local_turl| is considered "better" than |sync_turl| for
  // the purposes of resolving conflicts. |local_turl| must be a TemplateURL
  // known to the local model (though it may already be synced), and |sync_turl|
  // is a new TemplateURL known to Sync but not yet known to the local model.
  // The criteria for if |local_turl| is better than |sync_turl| is whether any
  // of the following are true:
  //  * |local_turl|'s last_modified timestamp is newer than sync_turl.
  //  * |local_turl| is created by policy.
  //  * |local_turl| is the local default search provider.
  bool IsLocalTemplateURLBetter(const TemplateURL* local_turl,
                                const TemplateURL* sync_turl);

  // Given two synced TemplateURLs with a conflicting keyword, one of which
  // needs to be added to or updated in the local model (|unapplied_sync_turl|)
  // and one which is already known to the local model (|applied_sync_turl|),
  // prepares the local model so that |unapplied_sync_turl| can be added to it,
  // or applied as an update to an existing TemplateURL.
  // Since both entries are known to Sync and one of their keywords will change,
  // an ACTION_UPDATE will be appended to |change_list| to reflect this change.
  // Note that |applied_sync_turl| must not be an extension keyword.
  void ResolveSyncKeywordConflict(TemplateURL* unapplied_sync_turl,
                                  TemplateURL* applied_sync_turl,
                                  syncer::SyncChangeList* change_list);

  // Adds |sync_turl| into the local model, possibly removing or updating a
  // local TemplateURL to make room for it. This expects |sync_turl| to be a new
  // entry from Sync, not currently known to the local model. |sync_data| should
  // be a SyncDataMap where the contents are entries initially known to Sync
  // during MergeDataAndStartSyncing.
  // Any necessary updates to Sync will be appended to |change_list|. This can
  // include updates on local TemplateURLs, if they are found in |sync_data|.
  // |initial_data| should be a SyncDataMap of the entries known to the local
  // model during MergeDataAndStartSyncing. If |sync_turl| replaces a local
  // entry, that entry is removed from |initial_data| to prevent it from being
  // sent up to Sync.
  // |merge_result| tracks the changes made to the local model. Added/modified/
  // deleted are updated depending on how the |sync_turl| is merged in.
  // This should only be called from MergeDataAndStartSyncing.
  void MergeInSyncTemplateURL(TemplateURL* sync_turl,
                              const SyncDataMap& sync_data,
                              syncer::SyncChangeList* change_list,
                              SyncDataMap* local_data,
                              syncer::SyncMergeResult* merge_result);

  // Checks a newly added TemplateURL from Sync by its sync_guid and sets it as
  // the default search provider if we were waiting for it.
  void SetDefaultSearchProviderIfNewlySynced(const std::string& guid);

  // Retrieve the pending default search provider according to Sync. Returns
  // NULL if there was no pending search provider from Sync.
  TemplateURL* GetPendingSyncedDefaultSearchProvider();

  // Goes through a vector of TemplateURLs and ensure that both the in-memory
  // and database copies have valid sync_guids. This is to fix crbug.com/102038,
  // where old entries were being pushed to Sync without a sync_guid.
  void PatchMissingSyncGUIDs(TemplateURLVector* template_urls);

  void OnSyncedDefaultSearchProviderGUIDChanged();

  // Adds |template_urls| to |template_urls_| and sets up the default search
  // provider.  If |default_search_provider| is non-NULL, it must refer to one
  // of the |template_urls|, and will be used as the new default.
  void AddTemplateURLsAndSetupDefaultEngine(
      TemplateURLVector* template_urls,
      TemplateURL* default_search_provider);

  // If there is no current default search provider, sets the default to the
  // result of calling FindNewDefaultSearchProvider().
  void EnsureDefaultSearchProviderExists();

  // Returns a new TemplateURL for the given extension.
  TemplateURL* CreateTemplateURLForExtension(
      const ExtensionKeyword& extension_keyword) const;

  content::NotificationRegistrar notification_registrar_;
  PrefChangeRegistrar pref_change_registrar_;

  // Mapping from keyword to the TemplateURL.
  KeywordToTemplateMap keyword_to_template_map_;

  // Mapping from Sync GUIDs to the TemplateURL.
  GUIDToTemplateMap guid_to_template_map_;

  TemplateURLVector template_urls_;

  ObserverList<TemplateURLServiceObserver> model_observers_;

  // Maps from host to set of TemplateURLs whose search url host is host.
  // NOTE: This is always non-NULL; we use a scoped_ptr<> to avoid circular
  // header dependencies.
  scoped_ptr<SearchHostToURLsMap> provider_map_;

  // Used to obtain the WebDataService.
  // When Load is invoked, if we haven't yet loaded, the WebDataService is
  // obtained from the Profile. This allows us to lazily access the database.
  Profile* profile_;

  // Whether the keywords have been loaded.
  bool loaded_;

  // Did loading fail? This is only valid if loaded_ is true.
  bool load_failed_;

  // If non-zero, we're waiting on a load.
  WebDataService::Handle load_handle_;

  // Service used to store entries.
  scoped_refptr<WebDataService> service_;

  // All visits that occurred before we finished loading. Once loaded
  // UpdateKeywordSearchTermsForURL is invoked for each element of the vector.
  std::vector<history::URLVisitedDetails> visits_to_add_;

  // Once loaded, the default search provider.  This is a pointer to a
  // TemplateURL owned by template_urls_.
  TemplateURL* default_search_provider_;

  // The initial search provider extracted from preferences. This is only valid
  // if we haven't been loaded or loading failed.
  scoped_ptr<TemplateURL> initial_default_search_provider_;

  // Whether the default search is managed via policy.
  bool is_default_search_managed_;

  // ID assigned to next TemplateURL added to this model. This is an ever
  // increasing integer that is initialized from the database.
  TemplateURLID next_id_;

  // Function returning current time in base::Time units.
  TimeProvider* time_provider_;

  // Do we have an active association between the TemplateURLs and sync models?
  // Set in MergeDataAndStartSyncing, reset in StopSyncing. While this is not
  // set, we ignore any local search engine changes (when we start syncing we
  // will look up the most recent values anyways).
  bool models_associated_;

  // Whether we're currently processing changes from the syncer. While this is
  // true, we ignore any local search engine changes, since we triggered them.
  bool processing_syncer_changes_;

  // Sync's syncer::SyncChange handler. We push all our changes through this.
  scoped_ptr<syncer::SyncChangeProcessor> sync_processor_;

  // Sync's error handler. We use it to create a sync error.
  scoped_ptr<syncer::SyncErrorFactory> sync_error_factory_;

  // Whether or not we are waiting on the default search provider to come in
  // from Sync. This is to facilitate the fact that changes to the value of
  // prefs::kSyncedDefaultSearchProviderGUID do not always come before the
  // TemplateURL entry it refers to, and to handle the case when we want to use
  // the Synced default when the default search provider becomes unmanaged.
  bool pending_synced_default_search_;

  // A set of sync GUIDs denoting TemplateURLs that have been removed from this
  // model or the underlying WebDataService prior to MergeDataAndStartSyncing.
  // This set is used to determine what entries from the server we want to
  // ignore locally and return a delete command for.
  std::set<std::string> pre_sync_deletes_;

  // This is used to log the origin of changes to the default search provider.
  // We set this value to increasingly specific values when we know what is the
  // cause/origin of a default search change.
  DefaultSearchChangeOrigin dsp_change_origin_;

  DISALLOW_COPY_AND_ASSIGN(TemplateURLService);
};

#endif  // CHROME_BROWSER_SEARCH_ENGINES_TEMPLATE_URL_SERVICE_H_
