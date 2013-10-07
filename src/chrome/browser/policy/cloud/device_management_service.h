// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_POLICY_CLOUD_DEVICE_MANAGEMENT_SERVICE_H_
#define CHROME_BROWSER_POLICY_CLOUD_DEVICE_MANAGEMENT_SERVICE_H_

#include <deque>
#include <map>
#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/policy/cloud/cloud_policy_constants.h"
#include "chrome/browser/policy/proto/cloud/device_management_backend.pb.h"
#include "net/url_request/url_fetcher_delegate.h"

namespace net {
class URLRequestContextGetter;
}

namespace policy {

class DeviceManagementRequestJobImpl;
class DeviceManagementService;

// DeviceManagementRequestJob describes a request to send to the device
// management service. Jobs are created by DeviceManagementService. They can be
// canceled by deleting the object.
class DeviceManagementRequestJob {
 public:
  // Describes the job type.
  enum JobType {
    TYPE_AUTO_ENROLLMENT,
    TYPE_REGISTRATION,
    TYPE_API_AUTH_CODE_FETCH,
    TYPE_POLICY_FETCH,
    TYPE_UNREGISTRATION,
    TYPE_UPLOAD_CERTIFICATE,
  };

  typedef base::Callback<
      void(DeviceManagementStatus, int,
           const enterprise_management::DeviceManagementResponse&)> Callback;

  typedef base::Callback<void(DeviceManagementRequestJob*)> RetryCallback;

  virtual ~DeviceManagementRequestJob();

  // Functions for configuring the job. These should only be called before
  // Start()ing the job, but never afterwards.
  void SetGaiaToken(const std::string& gaia_token);
  void SetOAuthToken(const std::string& oauth_token);
  void SetUserAffiliation(UserAffiliation user_affiliation);
  void SetDMToken(const std::string& dm_token);
  void SetClientID(const std::string& client_id);
  enterprise_management::DeviceManagementRequest* GetRequest();

  // A job may automatically retry if it fails due to a temporary condition, or
  // due to proxy misconfigurations. If a |retry_callback| is set then it will
  // be invoked with the DeviceManagementRequestJob as an argument when that
  // happens, so that the job's owner can customize the retry request before
  // it's sent.
  void SetRetryCallback(const RetryCallback& retry_callback);

  // Starts the job. |callback| will be invoked on completion.
  void Start(const Callback& callback);

 protected:
  typedef std::vector<std::pair<std::string, std::string> > ParameterMap;

  explicit DeviceManagementRequestJob(JobType type);

  // Appends a parameter to |query_params|.
  void AddParameter(const std::string& name, const std::string& value);

  // Fires the job, to be filled in by implementations.
  virtual void Run() = 0;

  ParameterMap query_params_;
  std::string gaia_token_;
  std::string dm_token_;
  enterprise_management::DeviceManagementRequest request_;
  RetryCallback retry_callback_;

  Callback callback_;

 private:
  DISALLOW_COPY_AND_ASSIGN(DeviceManagementRequestJob);
};

// The device management service is responsible for everything related to
// communication with the device management server. It creates the backends
// objects that the device management policy provider and friends use to issue
// requests.
class DeviceManagementService : public net::URLFetcherDelegate {
 public:
  explicit DeviceManagementService(const std::string& server_url);
  virtual ~DeviceManagementService();

  // The ID of URLFetchers created by the DeviceManagementService. This can be
  // used by tests that use a TestURLFetcherFactory to get the pending fetchers
  // created by the DeviceManagementService.
  static const int kURLFetcherID;

  // Creates a new device management request job. Ownership is transferred to
  // the caller.
  virtual DeviceManagementRequestJob* CreateJob(
      DeviceManagementRequestJob::JobType type);

  // Schedules a task to run |Initialize| after |delay_milliseconds| had passed.
  void ScheduleInitialization(int64 delay_milliseconds);

  // Makes the service stop all requests and drop the reference to the request
  // context.
  void Shutdown();

 private:
  typedef std::map<const net::URLFetcher*,
                   DeviceManagementRequestJobImpl*> JobFetcherMap;
  typedef std::deque<DeviceManagementRequestJobImpl*> JobQueue;

  friend class DeviceManagementRequestJobImpl;

  // net::URLFetcherDelegate override.
  virtual void OnURLFetchComplete(const net::URLFetcher* source) OVERRIDE;

  // Does the actual initialization using the request context specified for
  // |PrepareInitialization|. This will also fire any pending network requests.
  void Initialize();

  // Starts a job.
  void StartJob(DeviceManagementRequestJobImpl* job);

  // Adds a job. Caller must make sure the job pointer stays valid until the job
  // completes or gets canceled via RemoveJob().
  void AddJob(DeviceManagementRequestJobImpl* job);

  // Removes a job. The job will be removed and won't receive a completion
  // callback.
  void RemoveJob(DeviceManagementRequestJobImpl* job);

  // Server at which to contact the service.
  const std::string server_url_;

  // The request context we use.
  scoped_refptr<net::URLRequestContextGetter> request_context_getter_;

  // The jobs we currently have in flight.
  JobFetcherMap pending_jobs_;

  // Jobs that are registered, but not started yet.
  JobQueue queued_jobs_;

  // If this service is initialized, incoming requests get fired instantly.
  // If it is not initialized, incoming requests are queued.
  bool initialized_;

  // Used to create tasks to run |Initialize| delayed on the UI thread.
  base::WeakPtrFactory<DeviceManagementService> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(DeviceManagementService);
};

}  // namespace policy

#endif  // CHROME_BROWSER_POLICY_CLOUD_DEVICE_MANAGEMENT_SERVICE_H_
