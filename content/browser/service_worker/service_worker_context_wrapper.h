// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_SERVICE_WORKER_SERVICE_WORKER_CONTEXT_WRAPPER_H_
#define CONTENT_BROWSER_SERVICE_WORKER_SERVICE_WORKER_CONTEXT_WRAPPER_H_

#include <vector>

#include "base/files/file_path.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "content/browser/service_worker/service_worker_context_core.h"
#include "content/common/content_export.h"
#include "content/public/browser/service_worker_context.h"

namespace base {
class FilePath;
class SequencedTaskRunner;
class SingleThreadTaskRunner;
}

namespace storage {
class QuotaManagerProxy;
class SpecialStoragePolicy;
}

namespace content {

class BrowserContext;
class ResourceContext;
class ServiceWorkerContextCore;
class ServiceWorkerContextObserver;
class StoragePartitionImpl;

// A refcounted wrapper class for our core object. Higher level content lib
// classes keep references to this class on mutliple threads. The inner core
// instance is strictly single threaded and is not refcounted, the core object
// is what is used internally in the service worker lib.
class CONTENT_EXPORT ServiceWorkerContextWrapper
    : NON_EXPORTED_BASE(public ServiceWorkerContext),
      public base::RefCountedThreadSafe<ServiceWorkerContextWrapper> {
 public:
  using StatusCallback = base::Callback<void(ServiceWorkerStatusCode)>;
  using BoolCallback = base::Callback<void(bool)>;
  using FindRegistrationCallback =
      ServiceWorkerStorage::FindRegistrationCallback;
  using GetRegistrationsInfosCallback =
      ServiceWorkerStorage::GetRegistrationsInfosCallback;
  using GetUserDataCallback = ServiceWorkerStorage::GetUserDataCallback;
  using GetUserDataForAllRegistrationsCallback =
      ServiceWorkerStorage::GetUserDataForAllRegistrationsCallback;

  ServiceWorkerContextWrapper(BrowserContext* browser_context);

  // Init and Shutdown are for use on the UI thread when the profile,
  // storagepartition is being setup and torn down.
  void Init(const base::FilePath& user_data_directory,
            storage::QuotaManagerProxy* quota_manager_proxy,
            storage::SpecialStoragePolicy* special_storage_policy);
  void Shutdown();

  // Deletes all files on disk and restarts the system asynchronously. This
  // leaves the system in a disabled state until it's done. This should be
  // called on the IO thread.
  void DeleteAndStartOver();

  // The StoragePartition should only be used on the UI thread.
  // Can be null before/during init and during/after shutdown.
  StoragePartitionImpl* storage_partition() const;

  void set_storage_partition(StoragePartitionImpl* storage_partition);

  // The ResourceContext for the associated BrowserContext. This should only
  // be accessed on the IO thread, and can be null during initialization and
  // shutdown.
  ResourceContext* resource_context();

  void set_resource_context(ResourceContext* resource_context);

  // The process manager can be used on either UI or IO.
  ServiceWorkerProcessManager* process_manager() {
    return process_manager_.get();
  }

  // ServiceWorkerContext implementation:
  void RegisterServiceWorker(const GURL& pattern,
                             const GURL& script_url,
                             const ResultCallback& continuation) override;
  void UnregisterServiceWorker(const GURL& pattern,
                               const ResultCallback& continuation) override;
  void GetAllOriginsInfo(const GetUsageInfoCallback& callback) override;
  void DeleteForOrigin(const GURL& origin,
                       const ResultCallback& callback) override;
  void CheckHasServiceWorker(
      const GURL& url,
      const GURL& other_url,
      const CheckHasServiceWorkerCallback& callback) override;
  void StopAllServiceWorkersForOrigin(const GURL& origin) override;
  void ClearAllServiceWorkersForTest(const base::Closure& callback) override;

  ServiceWorkerRegistration* GetLiveRegistration(int64_t registration_id);
  ServiceWorkerVersion* GetLiveVersion(int64_t version_id);
  std::vector<ServiceWorkerRegistrationInfo> GetAllLiveRegistrationInfo();
  std::vector<ServiceWorkerVersionInfo> GetAllLiveVersionInfo();

  void HasMainFrameProviderHost(const GURL& origin,
                                const BoolCallback& callback) const;

  // Returns the registration whose scope longest matches |document_url|.
  // Returns ERROR_NOT_FOUND if it is not found.
  void FindRegistrationForDocument(const GURL& document_url,
                                   const FindRegistrationCallback& callback);

  // Returns the registration for |registration_id|. It is guaranteed that the
  // returned registration has the activated worker.
  //
  //  - If the registration is not found, returns ERROR_NOT_FOUND.
  //  - If the registration has neither the waiting version nor the active
  //    version, returns ERROR_NOT_FOUND.
  //  - If the registration does not have the active version but has the waiting
  //    version, activates the waiting version and runs |callback| when it is
  //    activated.
  void FindReadyRegistrationForId(int64_t registration_id,
                                  const GURL& origin,
                                  const FindRegistrationCallback& callback);

  void GetAllRegistrations(const GetRegistrationsInfosCallback& callback);
  void GetRegistrationUserData(int64_t registration_id,
                               const std::string& key,
                               const GetUserDataCallback& callback);
  void StoreRegistrationUserData(int64_t registration_id,
                                 const GURL& origin,
                                 const std::string& key,
                                 const std::string& data,
                                 const StatusCallback& callback);
  void ClearRegistrationUserData(int64_t registration_id,
                                 const std::string& key,
                                 const StatusCallback& callback);
  void GetUserDataForAllRegistrations(
      const std::string& key,
      const GetUserDataForAllRegistrationsCallback& callback);

  void StartServiceWorker(const GURL& pattern, const StatusCallback& callback);
  void UpdateRegistration(const GURL& pattern);
  void SetForceUpdateOnPageLoad(int64_t registration_id,
                                bool force_update_on_page_load);
  void AddObserver(ServiceWorkerContextObserver* observer);
  void RemoveObserver(ServiceWorkerContextObserver* observer);

  bool is_incognito() const { return is_incognito_; }

 private:
  friend class BackgroundSyncManagerTest;
  friend class base::RefCountedThreadSafe<ServiceWorkerContextWrapper>;
  friend class EmbeddedWorkerTestHelper;
  friend class EmbeddedWorkerBrowserTest;
  friend class ServiceWorkerDispatcherHost;
  friend class ServiceWorkerInternalsUI;
  friend class ServiceWorkerNavigationHandleCore;
  friend class ServiceWorkerProcessManager;
  friend class ServiceWorkerRequestHandler;
  friend class ServiceWorkerVersionBrowserTest;
  friend class MockServiceWorkerContextWrapper;

  ~ServiceWorkerContextWrapper() override;

  void InitInternal(
      const base::FilePath& user_data_directory,
      scoped_ptr<ServiceWorkerDatabaseTaskManager> database_task_manager,
      const scoped_refptr<base::SingleThreadTaskRunner>& disk_cache_thread,
      storage::QuotaManagerProxy* quota_manager_proxy,
      storage::SpecialStoragePolicy* special_storage_policy);
  void ShutdownOnIO();

  void DidFindRegistrationForFindReady(
      const FindRegistrationCallback& callback,
      ServiceWorkerStatusCode status,
      const scoped_refptr<ServiceWorkerRegistration>& registration);
  void OnStatusChangedForFindReadyRegistration(
      const FindRegistrationCallback& callback,
      const scoped_refptr<ServiceWorkerRegistration>& registration);

  void DidDeleteAndStartOver(ServiceWorkerStatusCode status);

  void DidGetAllRegistrationsForGetAllOrigins(
      const GetUsageInfoCallback& callback,
      const std::vector<ServiceWorkerRegistrationInfo>& registrations);

  void DidCheckHasServiceWorker(const CheckHasServiceWorkerCallback& callback,
                                bool has_service_worker);

  void DidFindRegistrationForUpdate(
      ServiceWorkerStatusCode status,
      const scoped_refptr<content::ServiceWorkerRegistration>& registration);

  // The core context is only for use on the IO thread.
  // Can be null before/during init, during/after shutdown, and after
  // DeleteAndStartOver fails.
  ServiceWorkerContextCore* context();

  const scoped_refptr<base::ObserverListThreadSafe<
      ServiceWorkerContextObserver>> observer_list_;
  const scoped_ptr<ServiceWorkerProcessManager> process_manager_;
  // Cleared in Shutdown():
  scoped_ptr<ServiceWorkerContextCore> context_core_;

  // Initialized in Init(); true if the user data directory is empty.
  bool is_incognito_;

  // Raw pointer to the StoragePartitionImpl owning |this|.
  StoragePartitionImpl* storage_partition_;

  // The ResourceContext associated with this context.
  ResourceContext* resource_context_;

  DISALLOW_COPY_AND_ASSIGN(ServiceWorkerContextWrapper);
};

}  // namespace content

#endif  // CONTENT_BROWSER_SERVICE_WORKER_SERVICE_WORKER_CONTEXT_WRAPPER_H_
