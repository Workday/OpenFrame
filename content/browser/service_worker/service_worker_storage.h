// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_SERVICE_WORKER_SERVICE_WORKER_STORAGE_H_
#define CONTENT_BROWSER_SERVICE_WORKER_SERVICE_WORKER_STORAGE_H_

#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/bind.h"
#include "base/files/file_path.h"
#include "base/gtest_prod_util.h"
#include "base/memory/weak_ptr.h"
#include "content/browser/service_worker/service_worker_database.h"
#include "content/browser/service_worker/service_worker_database_task_manager.h"
#include "content/browser/service_worker/service_worker_metrics.h"
#include "content/browser/service_worker/service_worker_version.h"
#include "content/common/content_export.h"
#include "content/common/service_worker/service_worker_status_code.h"
#include "url/gurl.h"

namespace base {
class SequencedTaskRunner;
class SingleThreadTaskRunner;
}

namespace storage {
class QuotaManagerProxy;
class SpecialStoragePolicy;
}

namespace content {

class ServiceWorkerContextCore;
class ServiceWorkerDiskCache;
class ServiceWorkerDiskCacheMigrator;
class ServiceWorkerRegistration;
class ServiceWorkerResponseMetadataWriter;
class ServiceWorkerResponseReader;
class ServiceWorkerResponseWriter;
struct ServiceWorkerRegistrationInfo;

// This class provides an interface to store and retrieve ServiceWorker
// registration data. The lifetime is equal to ServiceWorkerContextCore that is
// an owner of this class.
class CONTENT_EXPORT ServiceWorkerStorage
    : NON_EXPORTED_BASE(public ServiceWorkerVersion::Listener) {
 public:
  typedef std::vector<ServiceWorkerDatabase::ResourceRecord> ResourceList;
  typedef base::Callback<void(ServiceWorkerStatusCode status)> StatusCallback;
  typedef base::Callback<void(ServiceWorkerStatusCode status,
                              const scoped_refptr<ServiceWorkerRegistration>&
                                  registration)> FindRegistrationCallback;
  typedef base::Callback<void(
      const std::vector<scoped_refptr<ServiceWorkerRegistration>>&
          registrations)> GetRegistrationsCallback;
  typedef base::Callback<void(const std::vector<ServiceWorkerRegistrationInfo>&
                                  registrations)> GetRegistrationsInfosCallback;
  typedef base::Callback<
      void(const std::string& data, ServiceWorkerStatusCode status)>
          GetUserDataCallback;
  typedef base::Callback<void(
      const std::vector<std::pair<int64, std::string>>& user_data,
      ServiceWorkerStatusCode status)>
          GetUserDataForAllRegistrationsCallback;

  ~ServiceWorkerStorage() override;

  static scoped_ptr<ServiceWorkerStorage> Create(
      const base::FilePath& path,
      const base::WeakPtr<ServiceWorkerContextCore>& context,
      scoped_ptr<ServiceWorkerDatabaseTaskManager> database_task_manager,
      const scoped_refptr<base::SingleThreadTaskRunner>& disk_cache_thread,
      storage::QuotaManagerProxy* quota_manager_proxy,
      storage::SpecialStoragePolicy* special_storage_policy);

  // Used for DeleteAndStartOver. Creates new storage based on |old_storage|.
  static scoped_ptr<ServiceWorkerStorage> Create(
      const base::WeakPtr<ServiceWorkerContextCore>& context,
      ServiceWorkerStorage* old_storage);

  // Finds registration for |document_url| or |pattern| or |registration_id|.
  // The Find methods will find stored and initially installing registrations.
  // Returns SERVICE_WORKER_OK with non-null registration if registration
  // is found, or returns SERVICE_WORKER_ERROR_NOT_FOUND if no matching
  // registration is found.  The FindRegistrationForPattern method is
  // guaranteed to return asynchronously. However, the methods to find
  // for |document_url| or |registration_id| may complete immediately
  // (the callback may be called prior to the method returning) or
  // asynchronously.
  void FindRegistrationForDocument(const GURL& document_url,
                                   const FindRegistrationCallback& callback);
  void FindRegistrationForPattern(const GURL& scope,
                                  const FindRegistrationCallback& callback);
  void FindRegistrationForId(int64 registration_id,
                             const GURL& origin,
                             const FindRegistrationCallback& callback);

  // Generally |FindRegistrationForId| should be used to look up a registration
  // by |registration_id| since it's more efficient. But if a |registration_id|
  // is all that is available this method can be used instead.
  // Like |FindRegistrationForId| this method may complete immediately (the
  // callback may be called prior to the method returning) or asynchronously.
  void FindRegistrationForIdOnly(int64 registration_id,
                                 const FindRegistrationCallback& callback);

  ServiceWorkerRegistration* GetUninstallingRegistration(const GURL& scope);

  // Returns all stored registrations for a given origin.
  void GetRegistrationsForOrigin(const GURL& origin,
                                 const GetRegistrationsCallback& callback);

  // Returns info about all stored and initially installing registrations.
  void GetAllRegistrationsInfos(const GetRegistrationsInfosCallback& callback);

  // Commits |registration| with the installed but not activated |version|
  // to storage, overwritting any pre-existing registration data for the scope.
  // A pre-existing version's script resources remain available if that version
  // is live. PurgeResources should be called when it's OK to delete them.
  void StoreRegistration(ServiceWorkerRegistration* registration,
                         ServiceWorkerVersion* version,
                         const StatusCallback& callback);

  // Updates the state of the registration's stored version to active.
  void UpdateToActiveState(
      ServiceWorkerRegistration* registration,
      const StatusCallback& callback);

  // Updates the stored time to match the value of
  // registration->last_update_check().
  void UpdateLastUpdateCheckTime(ServiceWorkerRegistration* registration);

  // Deletes the registration data for |registration_id|. If the registration's
  // version is live, its script resources will remain available.
  // PurgeResources should be called when it's OK to delete them.
  void DeleteRegistration(int64 registration_id,
                          const GURL& origin,
                          const StatusCallback& callback);

  scoped_ptr<ServiceWorkerResponseReader> CreateResponseReader(
      int64 resource_id);
  scoped_ptr<ServiceWorkerResponseWriter> CreateResponseWriter(
      int64 resource_id);
  scoped_ptr<ServiceWorkerResponseMetadataWriter> CreateResponseMetadataWriter(
      int64 resource_id);

  // Adds |resource_id| to the set of resources that are in the disk cache
  // but not yet stored with a registration.
  void StoreUncommittedResourceId(int64 resource_id);

  // Removes resource ids from uncommitted list, adds them to the purgeable list
  // and purges them.
  void DoomUncommittedResource(int64 resource_id);
  void DoomUncommittedResources(const std::set<int64>& resource_ids);

  // Provide a storage mechanism to read/write arbitrary data associated with
  // a registration. Each registration has its own key namespace. Stored data
  // is deleted when the associated registraton is deleted.
  void GetUserData(int64 registration_id,
                   const std::string& key,
                   const GetUserDataCallback& callback);
  void StoreUserData(int64 registration_id,
                     const GURL& origin,
                     const std::string& key,
                     const std::string& data,
                     const StatusCallback& callback);
  void ClearUserData(int64 registration_id,
                     const std::string& key,
                     const StatusCallback& callback);
  // Returns all registrations that have user data with a particular key, as
  // well as that user data.
  void GetUserDataForAllRegistrations(
      const std::string& key,
      const GetUserDataForAllRegistrationsCallback& callback);

  // Returns true if any service workers at |origin| have registered for foreign
  // fetch.
  bool OriginHasForeignFetchRegistrations(const GURL& origin);

  // Deletes the storage and starts over.
  void DeleteAndStartOver(const StatusCallback& callback);

  // Returns a new registration id which is guaranteed to be unique in the
  // storage. Returns kInvalidServiceWorkerRegistrationId if the storage is
  // disabled.
  int64 NewRegistrationId();

  // Returns a new version id which is guaranteed to be unique in the storage.
  // Returns kInvalidServiceWorkerVersionId if the storage is disabled.
  int64 NewVersionId();

  // Returns a new resource id which is guaranteed to be unique in the storage.
  // Returns kInvalidServiceWorkerResourceId if the storage is disabled.
  int64 NewResourceId();

  // Intended for use only by ServiceWorkerRegisterJob and
  // ServiceWorkerRegistration.
  void NotifyInstallingRegistration(
      ServiceWorkerRegistration* registration);
  void NotifyDoneInstallingRegistration(
      ServiceWorkerRegistration* registration,
      ServiceWorkerVersion* version,
      ServiceWorkerStatusCode status);
  void NotifyUninstallingRegistration(ServiceWorkerRegistration* registration);
  void NotifyDoneUninstallingRegistration(
      ServiceWorkerRegistration* registration);

  void Disable();
  bool IsDisabled() const;

  // |resources| must already be on the purgeable list.
  void PurgeResources(const ResourceList& resources);

 private:
  friend class ServiceWorkerHandleTest;
  friend class ServiceWorkerStorageTest;
  friend class ServiceWorkerResourceStorageTest;
  friend class ServiceWorkerControlleeRequestHandlerTest;
  friend class ServiceWorkerContextRequestHandlerTest;
  friend class ServiceWorkerDiskCacheMigratorTest;
  friend class ServiceWorkerRequestHandlerTest;
  friend class ServiceWorkerURLRequestJobTest;
  friend class ServiceWorkerVersionBrowserTest;
  friend class ServiceWorkerVersionTest;
  friend class ServiceWorkerWriteToCacheJobTest;
  FRIEND_TEST_ALL_PREFIXES(ServiceWorkerDiskCacheMigratorTest,
                           MigrateOnDiskCacheAccess);
  FRIEND_TEST_ALL_PREFIXES(ServiceWorkerDiskCacheMigratorTest,
                           NotMigrateOnDatabaseAccess);
  FRIEND_TEST_ALL_PREFIXES(ServiceWorkerDiskCacheMigratorTest,
                           NotMigrateForEmptyDatabase);
  FRIEND_TEST_ALL_PREFIXES(ServiceWorkerDispatcherHostTest,
                           CleanupOnRendererCrash);
  FRIEND_TEST_ALL_PREFIXES(ServiceWorkerResourceStorageTest,
                           DeleteRegistration_NoLiveVersion);
  FRIEND_TEST_ALL_PREFIXES(ServiceWorkerResourceStorageTest,
                           DeleteRegistration_WaitingVersion);
  FRIEND_TEST_ALL_PREFIXES(ServiceWorkerResourceStorageTest,
                           DeleteRegistration_ActiveVersion);
  FRIEND_TEST_ALL_PREFIXES(ServiceWorkerResourceStorageTest,
                           UpdateRegistration);
  FRIEND_TEST_ALL_PREFIXES(ServiceWorkerResourceStorageDiskTest,
                           CleanupOnRestart);
  FRIEND_TEST_ALL_PREFIXES(ServiceWorkerResourceStorageDiskTest,
                           ClearOnExit);
  FRIEND_TEST_ALL_PREFIXES(ServiceWorkerResourceStorageDiskTest,
                           DeleteAndStartOver);
  FRIEND_TEST_ALL_PREFIXES(ServiceWorkerResourceStorageDiskTest,
                           DeleteAndStartOver_UnrelatedFileExists);
  FRIEND_TEST_ALL_PREFIXES(ServiceWorkerResourceStorageDiskTest,
                           DeleteAndStartOver_OpenedFileExists);

  struct InitialData {
    int64 next_registration_id;
    int64 next_version_id;
    int64 next_resource_id;
    std::set<GURL> origins;
    bool disk_cache_migration_needed;
    bool old_disk_cache_deletion_needed;
    std::set<GURL> foreign_fetch_origins;

    InitialData();
    ~InitialData();
  };

  // Because there are too many params for base::Bind to wrap a closure around.
  struct DidDeleteRegistrationParams {
    int64 registration_id;
    GURL origin;
    StatusCallback callback;

    DidDeleteRegistrationParams();
    ~DidDeleteRegistrationParams();
  };

  enum class OriginState {
    // Other registrations with foreign fetch scopes exist for the origin.
    KEEP_ALL,
    // Other registrations exist at this origin, but none of them have foreign
    // fetch scopes.
    DELETE_FROM_FOREIGN_FETCH,
    // No other registrations exist at this origin.
    DELETE_FROM_ALL
  };

  typedef std::vector<ServiceWorkerDatabase::RegistrationData> RegistrationList;
  typedef std::map<int64, scoped_refptr<ServiceWorkerRegistration> >
      RegistrationRefsById;
  typedef base::Callback<void(scoped_ptr<InitialData> data,
                              ServiceWorkerDatabase::Status status)>
      InitializeCallback;
  typedef base::Callback<void(ServiceWorkerDatabase::Status status)>
      DatabaseStatusCallback;
  typedef base::Callback<void(
      const GURL& origin,
      const ServiceWorkerDatabase::RegistrationData& deleted_version_data,
      const std::vector<int64>& newly_purgeable_resources,
      ServiceWorkerDatabase::Status status)> WriteRegistrationCallback;
  typedef base::Callback<void(
      OriginState origin_state,
      const ServiceWorkerDatabase::RegistrationData& deleted_version_data,
      const std::vector<int64>& newly_purgeable_resources,
      ServiceWorkerDatabase::Status status)> DeleteRegistrationCallback;
  typedef base::Callback<void(
      const ServiceWorkerDatabase::RegistrationData& data,
      const ResourceList& resources,
      ServiceWorkerDatabase::Status status)> FindInDBCallback;
  typedef base::Callback<void(
      const std::string& data,
      ServiceWorkerDatabase::Status)> GetUserDataInDBCallback;
  typedef base::Callback<void(
      const std::vector<std::pair<int64, std::string>>& user_data,
      ServiceWorkerDatabase::Status)>
      GetUserDataForAllRegistrationsInDBCallback;
  typedef base::Callback<void(const std::vector<int64>& resource_ids,
                              ServiceWorkerDatabase::Status status)>
      GetResourcesCallback;

  ServiceWorkerStorage(
      const base::FilePath& path,
      base::WeakPtr<ServiceWorkerContextCore> context,
      scoped_ptr<ServiceWorkerDatabaseTaskManager> database_task_manager,
      const scoped_refptr<base::SingleThreadTaskRunner>& disk_cache_thread,
      storage::QuotaManagerProxy* quota_manager_proxy,
      storage::SpecialStoragePolicy* special_storage_policy);

  base::FilePath GetDatabasePath();
  base::FilePath GetDiskCachePath();

  // Returns a path to an old diskcache backed with BlockFile. This is used for
  // the diskcache migration (see service_worker_disk_cache_migrator.h).
  // TODO(nhiroki): Remove this after several milestones pass
  // (http://crbug.com/487482)
  base::FilePath GetOldDiskCachePath();

  bool LazyInitialize(
      const base::Closure& callback);
  void DidReadInitialData(scoped_ptr<InitialData> data,
                          ServiceWorkerDatabase::Status status);
  void DidFindRegistrationForDocument(
      const GURL& document_url,
      const FindRegistrationCallback& callback,
      int64 callback_id,
      const ServiceWorkerDatabase::RegistrationData& data,
      const ResourceList& resources,
      ServiceWorkerDatabase::Status status);
  void DidFindRegistrationForPattern(
      const GURL& scope,
      const FindRegistrationCallback& callback,
      const ServiceWorkerDatabase::RegistrationData& data,
      const ResourceList& resources,
      ServiceWorkerDatabase::Status status);
  void DidFindRegistrationForId(
      const FindRegistrationCallback& callback,
      const ServiceWorkerDatabase::RegistrationData& data,
      const ResourceList& resources,
      ServiceWorkerDatabase::Status status);
  void DidGetRegistrations(const GetRegistrationsCallback& callback,
                           RegistrationList* registration_data_list,
                           std::vector<ResourceList>* resources_list,
                           const GURL& origin_filter,
                           ServiceWorkerDatabase::Status status);
  void DidGetRegistrationsInfos(const GetRegistrationsInfosCallback& callback,
                                RegistrationList* registration_data_list,
                                const GURL& origin_filter,
                                ServiceWorkerDatabase::Status status);
  void DidStoreRegistration(
      const StatusCallback& callback,
      const ServiceWorkerDatabase::RegistrationData& new_version,
      const GURL& origin,
      const ServiceWorkerDatabase::RegistrationData& deleted_version,
      const std::vector<int64>& newly_purgeable_resources,
      ServiceWorkerDatabase::Status status);
  void DidUpdateToActiveState(
      const StatusCallback& callback,
      ServiceWorkerDatabase::Status status);
  void DidDeleteRegistration(
      const DidDeleteRegistrationParams& params,
      OriginState origin_state,
      const ServiceWorkerDatabase::RegistrationData& deleted_version,
      const std::vector<int64>& newly_purgeable_resources,
      ServiceWorkerDatabase::Status status);
  void DidWriteUncommittedResourceIds(ServiceWorkerDatabase::Status status);
  void DidPurgeUncommittedResourceIds(const std::set<int64>& resource_ids,
                                      ServiceWorkerDatabase::Status status);
  void DidStoreUserData(
      const StatusCallback& callback,
      ServiceWorkerDatabase::Status status);
  void DidGetUserData(
      const GetUserDataCallback& callback,
      const std::string& data,
      ServiceWorkerDatabase::Status status);
  void DidDeleteUserData(
      const StatusCallback& callback,
      ServiceWorkerDatabase::Status status);
  void DidGetUserDataForAllRegistrations(
      const GetUserDataForAllRegistrationsCallback& callback,
      const std::vector<std::pair<int64, std::string>>& user_data,
      ServiceWorkerDatabase::Status status);
  void ReturnFoundRegistration(
      const FindRegistrationCallback& callback,
      const ServiceWorkerDatabase::RegistrationData& data,
      const ResourceList& resources);

  scoped_refptr<ServiceWorkerRegistration> GetOrCreateRegistration(
      const ServiceWorkerDatabase::RegistrationData& data,
      const ResourceList& resources);
  ServiceWorkerRegistration* FindInstallingRegistrationForDocument(
      const GURL& document_url);
  ServiceWorkerRegistration* FindInstallingRegistrationForPattern(
      const GURL& scope);
  ServiceWorkerRegistration* FindInstallingRegistrationForId(
      int64 registration_id);

  // Lazy disk_cache getter.
  ServiceWorkerDiskCache* disk_cache();
  void MigrateDiskCache();
  void DidMigrateDiskCache(ServiceWorkerStatusCode status);
  void DidSetDiskCacheMigrationNotNeeded(ServiceWorkerDatabase::Status status);
  void OnDiskCacheMigrationFailed(
      ServiceWorkerMetrics::DiskCacheMigrationResult result);
  void InitializeDiskCache();
  void OnDiskCacheInitialized(int rv);

  void DeleteOldDiskCache();

  void StartPurgingResources(const std::set<int64>& resource_ids);
  void StartPurgingResources(const std::vector<int64>& resource_ids);
  void StartPurgingResources(const ResourceList& resources);
  void ContinuePurgingResources();
  void PurgeResource(int64 id);
  void OnResourcePurged(int64 id, int rv);

  // Deletes purgeable and uncommitted resources left over from the previous
  // browser session. This must be called once per session before any database
  // operation that may mutate the purgeable or uncommitted resource lists.
  void DeleteStaleResources();
  void DidCollectStaleResources(const std::vector<int64>& stale_resource_ids,
                                ServiceWorkerDatabase::Status status);

  void ClearSessionOnlyOrigins();

  // Static cross-thread helpers.
  static void CollectStaleResourcesFromDB(
      ServiceWorkerDatabase* database,
      scoped_refptr<base::SequencedTaskRunner> original_task_runner,
      const GetResourcesCallback& callback);
  static void ReadInitialDataFromDB(
      ServiceWorkerDatabase* database,
      scoped_refptr<base::SequencedTaskRunner> original_task_runner,
      const InitializeCallback& callback);
  static void DeleteOldDiskCacheInDB(ServiceWorkerDatabase* database,
                                     const base::FilePath& disk_cache_path);
  static void DeleteRegistrationFromDB(
      ServiceWorkerDatabase* database,
      scoped_refptr<base::SequencedTaskRunner> original_task_runner,
      int64 registration_id,
      const GURL& origin,
      const DeleteRegistrationCallback& callback);
  static void WriteRegistrationInDB(
      ServiceWorkerDatabase* database,
      scoped_refptr<base::SequencedTaskRunner> original_task_runner,
      const ServiceWorkerDatabase::RegistrationData& registration,
      const ResourceList& resources,
      const WriteRegistrationCallback& callback);
  static void FindForDocumentInDB(
      ServiceWorkerDatabase* database,
      scoped_refptr<base::SequencedTaskRunner> original_task_runner,
      const GURL& document_url,
      const FindInDBCallback& callback);
  static void FindForPatternInDB(
      ServiceWorkerDatabase* database,
      scoped_refptr<base::SequencedTaskRunner> original_task_runner,
      const GURL& scope,
      const FindInDBCallback& callback);
  static void FindForIdInDB(
      ServiceWorkerDatabase* database,
      scoped_refptr<base::SequencedTaskRunner> original_task_runner,
      int64 registration_id,
      const GURL& origin,
      const FindInDBCallback& callback);
  static void FindForIdOnlyInDB(
      ServiceWorkerDatabase* database,
      scoped_refptr<base::SequencedTaskRunner> original_task_runner,
      int64 registration_id,
      const FindInDBCallback& callback);
  static void GetUserDataInDB(
      ServiceWorkerDatabase* database,
      scoped_refptr<base::SequencedTaskRunner> original_task_runner,
      int64 registration_id,
      const std::string& key,
      const GetUserDataInDBCallback& callback);
  static void GetUserDataForAllRegistrationsInDB(
      ServiceWorkerDatabase* database,
      scoped_refptr<base::SequencedTaskRunner> original_task_runner,
      const std::string& key,
      const GetUserDataForAllRegistrationsInDBCallback& callback);
  static void DeleteAllDataForOriginsFromDB(
      ServiceWorkerDatabase* database,
      const std::set<GURL>& origins);

  void ScheduleDeleteAndStartOver();
  void DidDeleteDatabase(
      const StatusCallback& callback,
      ServiceWorkerDatabase::Status status);
  void DidDeleteDiskCache(
      const StatusCallback& callback,
      bool result);

  // For finding registrations being installed or uninstalled.
  RegistrationRefsById installing_registrations_;
  RegistrationRefsById uninstalling_registrations_;

  // Origins having registations.
  std::set<GURL> registered_origins_;
  std::set<GURL> foreign_fetch_origins_;

  // Pending database tasks waiting for initialization.
  std::vector<base::Closure> pending_tasks_;

  int64 next_registration_id_;
  int64 next_version_id_;
  int64 next_resource_id_;

  enum State {
    UNINITIALIZED,
    INITIALIZING,
    INITIALIZED,
    DISABLED,
  };
  State state_;

  base::FilePath path_;

  // The context should be valid while the storage is alive.
  base::WeakPtr<ServiceWorkerContextCore> context_;

  // Only accessed using |database_task_manager_|.
  scoped_ptr<ServiceWorkerDatabase> database_;

  scoped_ptr<ServiceWorkerDatabaseTaskManager> database_task_manager_;
  scoped_refptr<base::SingleThreadTaskRunner> disk_cache_thread_;
  scoped_refptr<storage::QuotaManagerProxy> quota_manager_proxy_;
  scoped_refptr<storage::SpecialStoragePolicy> special_storage_policy_;

  scoped_ptr<ServiceWorkerDiskCache> disk_cache_;
  scoped_ptr<ServiceWorkerDiskCacheMigrator> disk_cache_migrator_;
  bool disk_cache_migration_needed_;
  bool old_disk_cache_deletion_needed_;

  std::deque<int64> purgeable_resource_ids_;
  bool is_purge_pending_;
  bool has_checked_for_stale_resources_;
  std::set<int64> pending_deletions_;

  base::WeakPtrFactory<ServiceWorkerStorage> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(ServiceWorkerStorage);
};

}  // namespace content

#endif  // CONTENT_BROWSER_SERVICE_WORKER_SERVICE_WORKER_STORAGE_H_
