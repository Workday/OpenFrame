// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_DRIVE_JOB_SCHEDULER_H_
#define CHROME_BROWSER_CHROMEOS_DRIVE_JOB_SCHEDULER_H_

#include <vector>

#include "base/id_map.h"
#include "base/memory/scoped_ptr.h"
#include "base/observer_list.h"
#include "chrome/browser/chromeos/drive/file_system_interface.h"
#include "chrome/browser/chromeos/drive/job_list.h"
#include "chrome/browser/chromeos/drive/job_queue.h"
#include "chrome/browser/drive/drive_service_interface.h"
#include "chrome/browser/drive/drive_uploader.h"
#include "net/base/network_change_notifier.h"

class PrefService;

namespace base {
class SeqencedTaskRunner;
}

namespace drive {

// The JobScheduler is responsible for queuing and scheduling drive
// jobs.
//
// All jobs are processed in order of priority.
//   - Jobs that occur as a result of a direct user action are handled
//     immediately (i.e. the client context is USER_INITIATED).
//   - Jobs that are done in response to state changes or server actions are run
//     in the background (i.e. the client context is BACKGROUND).
//
// All jobs are retried a maximum of kMaxRetryCount when they fail due to
// throttling or server error.  The delay before retrying a job is shared
// between jobs.  It doubles in length on each failure, up to 16 seconds.
//
// Jobs are grouped into two types:
//   - File jobs are any job that transfer the contents of files.
//     By default, they are only run when connected to WiFi.
//   - Metadata jobs are any jobs that operate on File metadata or
//     the directory structure.  Up to kMaxJobCount[METADATA_QUEUE] jobs are run
//     concurrently.
//
// Because jobs are executed by priority and the potential for network failures,
// there is no guarantee of ordering of operations.
class JobScheduler
    : public net::NetworkChangeNotifier::ConnectionTypeObserver,
      public JobListInterface {
 public:
  JobScheduler(PrefService* pref_service,
               DriveServiceInterface* drive_service,
               base::SequencedTaskRunner* blocking_task_runner);
  virtual ~JobScheduler();

  // JobListInterface overrides.
  virtual std::vector<JobInfo> GetJobInfoList() OVERRIDE;
  virtual void AddObserver(JobListObserver* observer) OVERRIDE;
  virtual void RemoveObserver(JobListObserver* observer) OVERRIDE;
  virtual void CancelJob(JobID job_id) OVERRIDE;
  virtual void CancelAllJobs() OVERRIDE;

  // Adds a GetAppList operation to the queue.
  // |callback| must not be null.
  void GetAppList(const google_apis::GetAppListCallback& callback);

  // Adds a GetAboutResource operation to the queue.
  // |callback| must not be null.
  void GetAboutResource(const google_apis::GetAboutResourceCallback& callback);

  // Adds a GetAllResourceList operation to the queue.
  // |callback| must not be null.
  void GetAllResourceList(const google_apis::GetResourceListCallback& callback);

  // Adds a GetResourceListInDirectory operation to the queue.
  // |callback| must not be null.
  void GetResourceListInDirectory(
      const std::string& directory_resource_id,
      const google_apis::GetResourceListCallback& callback);

  // Adds a Search operation to the queue.
  // |callback| must not be null.
  void Search(const std::string& search_query,
              const google_apis::GetResourceListCallback& callback);

  // Adds a GetChangeList operation to the queue.
  // |callback| must not be null.
  void GetChangeList(int64 start_changestamp,
                     const google_apis::GetResourceListCallback& callback);

  // Adds ContinueGetResourceList operation to the queue.
  // |callback| must not be null.
  void ContinueGetResourceList(
      const GURL& next_url,
      const google_apis::GetResourceListCallback& callback);

  // Adds a GetResourceEntry operation to the queue.
  void GetResourceEntry(const std::string& resource_id,
                        const ClientContext& context,
                        const google_apis::GetResourceEntryCallback& callback);

  // Adds a GetShareUrl operation to the queue.
  void GetShareUrl(const std::string& resource_id,
                   const GURL& embed_origin,
                   const ClientContext& context,
                   const google_apis::GetShareUrlCallback& callback);

  // Adds a DeleteResource operation to the queue.
  void DeleteResource(const std::string& resource_id,
                      const google_apis::EntryActionCallback& callback);

  // Adds a CopyResource operation to the queue.
  void CopyResource(
      const std::string& resource_id,
      const std::string& parent_resource_id,
      const std::string& new_title,
      const google_apis::GetResourceEntryCallback& callback);

  // Adds a CopyHostedDocument operation to the queue.
  void CopyHostedDocument(
      const std::string& resource_id,
      const std::string& new_title,
      const google_apis::GetResourceEntryCallback& callback);

  // Adds a RenameResource operation to the queue.
  void RenameResource(const std::string& resource_id,
                      const std::string& new_title,
                      const google_apis::EntryActionCallback& callback);

  // Adds a TouchResource operation to the queue.
  void TouchResource(const std::string& resource_id,
                     const base::Time& modified_date,
                     const base::Time& last_viewed_by_me_date,
                     const google_apis::GetResourceEntryCallback& callback);

  // Adds a AddResourceToDirectory operation to the queue.
  void AddResourceToDirectory(const std::string& parent_resource_id,
                              const std::string& resource_id,
                              const google_apis::EntryActionCallback& callback);

  // Adds a RemoveResourceFromDirectory operation to the queue.
  void RemoveResourceFromDirectory(
      const std::string& parent_resource_id,
      const std::string& resource_id,
      const google_apis::EntryActionCallback& callback);

  // Adds a AddNewDirectory operation to the queue.
  void AddNewDirectory(const std::string& parent_resource_id,
                       const std::string& directory_title,
                       const google_apis::GetResourceEntryCallback& callback);

  // Adds a DownloadFile operation to the queue.
  JobID DownloadFile(
      const base::FilePath& virtual_path,
      const base::FilePath& local_cache_path,
      const std::string& resource_id,
      const ClientContext& context,
      const google_apis::DownloadActionCallback& download_action_callback,
      const google_apis::GetContentCallback& get_content_callback);

  // Adds an UploadNewFile operation to the queue.
  void UploadNewFile(const std::string& parent_resource_id,
                     const base::FilePath& drive_file_path,
                     const base::FilePath& local_file_path,
                     const std::string& title,
                     const std::string& content_type,
                     const ClientContext& context,
                     const google_apis::GetResourceEntryCallback& callback);

  // Adds an UploadExistingFile operation to the queue.
  void UploadExistingFile(
      const std::string& resource_id,
      const base::FilePath& drive_file_path,
      const base::FilePath& local_file_path,
      const std::string& content_type,
      const std::string& etag,
      const ClientContext& context,
      const google_apis::GetResourceEntryCallback& callback);

  // Adds a CreateFile operation to the queue.
  void CreateFile(const std::string& parent_resource_id,
                  const base::FilePath& drive_file_path,
                  const std::string& title,
                  const std::string& content_type,
                  const ClientContext& context,
                  const google_apis::GetResourceEntryCallback& callback);

 private:
  friend class JobSchedulerTest;

  enum QueueType {
    METADATA_QUEUE,
    FILE_QUEUE,
    NUM_QUEUES
  };

  static const int kMaxJobCount[NUM_QUEUES];

  // Represents a single entry in the job map.
  struct JobEntry {
    explicit JobEntry(JobType type);
    ~JobEntry();

    // General user-visible information on the job.
    JobInfo job_info;

    // Context of the job.
    ClientContext context;

    // The number of times the jobs is retried due to server errors.
    int retry_count;

    // The callback to start the job. Called each time it is retry.
    base::Callback<google_apis::CancelCallback()> task;

    // The callback to cancel the running job. It is returned from task.Run().
    google_apis::CancelCallback cancel_callback;

    // The callback to notify an error to the client of JobScheduler.
    // This is used to notify cancel of a job that is not running yet.
    base::Callback<void(google_apis::GDataErrorCode)> abort_callback;
  };

  // Parameters for DriveUploader::ResumeUploadFile.
  struct ResumeUploadParams;

  // Creates a new job and add it to the job map.
  JobEntry* CreateNewJob(JobType type);

  // Adds the specified job to the queue and starts the job loop for the queue
  // if needed.
  void StartJob(JobEntry* job);

  // Adds the specified job to the queue.
  void QueueJob(JobID job_id);

  // Determines the next job that should run, and starts it.
  void DoJobLoop(QueueType queue_type);

  // Returns the lowest acceptable priority level of the operations that is
  // currently allowed to start for the |queue_type|.
  int GetCurrentAcceptedPriority(QueueType queue_type);

  // Updates |wait_until_| to throttle requests.
  void UpdateWait();

  // Retries the job if needed and returns false. Otherwise returns true.
  bool OnJobDone(JobID job_id, google_apis::GDataErrorCode error);

  // Callback for job finishing with a GetResourceListCallback.
  void OnGetResourceListJobDone(
      JobID job_id,
      const google_apis::GetResourceListCallback& callback,
      google_apis::GDataErrorCode error,
      scoped_ptr<google_apis::ResourceList> resource_list);

  // Callback for job finishing with a GetResourceEntryCallback.
  void OnGetResourceEntryJobDone(
      JobID job_id,
      const google_apis::GetResourceEntryCallback& callback,
      google_apis::GDataErrorCode error,
      scoped_ptr<google_apis::ResourceEntry> entry);

  // Callback for job finishing with a GetAboutResourceCallback.
  void OnGetAboutResourceJobDone(
      JobID job_id,
      const google_apis::GetAboutResourceCallback& callback,
      google_apis::GDataErrorCode error,
      scoped_ptr<google_apis::AboutResource> about_resource);

  // Callback for job finishing with a GetShareUrlCallback.
  void OnGetShareUrlJobDone(
      JobID job_id,
      const google_apis::GetShareUrlCallback& callback,
      google_apis::GDataErrorCode error,
      const GURL& share_url);

  // Callback for job finishing with a GetAppListCallback.
  void OnGetAppListJobDone(
      JobID job_id,
      const google_apis::GetAppListCallback& callback,
      google_apis::GDataErrorCode error,
      scoped_ptr<google_apis::AppList> app_list);

  // Callback for job finishing with a EntryActionCallback.
  void OnEntryActionJobDone(JobID job_id,
                            const google_apis::EntryActionCallback& callback,
                            google_apis::GDataErrorCode error);

  // Callback for job finishing with a DownloadActionCallback.
  void OnDownloadActionJobDone(
      JobID job_id,
      const google_apis::DownloadActionCallback& callback,
      google_apis::GDataErrorCode error,
      const base::FilePath& temp_file);

  // Callback for job finishing with a UploadCompletionCallback.
  void OnUploadCompletionJobDone(
      JobID job_id,
      const ResumeUploadParams& resume_params,
      const google_apis::GetResourceEntryCallback& callback,
      google_apis::GDataErrorCode error,
      const GURL& upload_location,
      scoped_ptr<google_apis::ResourceEntry> resource_entry);

  // Callback for DriveUploader::ResumeUploadFile().
  void OnResumeUploadFileDone(
      JobID job_id,
      const base::Callback<google_apis::CancelCallback()>& original_task,
      const google_apis::GetResourceEntryCallback& callback,
      google_apis::GDataErrorCode error,
      const GURL& upload_location,
      scoped_ptr<google_apis::ResourceEntry> resource_entry);

  // Updates the progress status of the specified job.
  void UpdateProgress(JobID job_id, int64 progress, int64 total);

  // net::NetworkChangeNotifier::ConnectionTypeObserver override.
  virtual void OnConnectionTypeChanged(
      net::NetworkChangeNotifier::ConnectionType type) OVERRIDE;

  // Get the type of queue the specified job should be put in.
  QueueType GetJobQueueType(JobType type);

  // For testing only.  Disables throttling so that testing is faster.
  void SetDisableThrottling(bool disable) { disable_throttling_ = disable; }

  // Aborts a job which is not in STATE_RUNNING.
  void AbortNotRunningJob(JobEntry* job, google_apis::GDataErrorCode error);

  // Notifies updates to observers.
  void NotifyJobAdded(const JobInfo& job_info);
  void NotifyJobDone(const JobInfo& job_info,
                     google_apis::GDataErrorCode error);
  void NotifyJobUpdated(const JobInfo& job_info);

  // Gets information of the queue of the given type as string.
  std::string GetQueueInfo(QueueType type) const;

  // Returns a string representation of QueueType.
  static std::string QueueTypeToString(QueueType type);

  // The number of times operations have failed in a row, capped at
  // kMaxThrottleCount.  This is used to calculate the delay before running the
  // next task.
  int throttle_count_;

  // Jobs should not start running until this time. Used for throttling.
  base::Time wait_until_;

  // Disables throttling for testing.
  bool disable_throttling_;

  // The queues of jobs.
  scoped_ptr<JobQueue> queue_[NUM_QUEUES];

  // The list of queued job info indexed by job IDs.
  typedef IDMap<JobEntry, IDMapOwnPointer> JobIDMap;
  JobIDMap job_map_;

  // The list of observers for the scheduler.
  ObserverList<JobListObserver> observer_list_;

  DriveServiceInterface* drive_service_;
  scoped_ptr<DriveUploaderInterface> uploader_;

  PrefService* pref_service_;

  // Note: This should remain the last member so it'll be destroyed and
  // invalidate its weak pointers before any other members are destroyed.
  base::WeakPtrFactory<JobScheduler> weak_ptr_factory_;
  DISALLOW_COPY_AND_ASSIGN(JobScheduler);
};

}  // namespace drive

#endif  // CHROME_BROWSER_CHROMEOS_DRIVE_JOB_SCHEDULER_H_
