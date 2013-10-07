// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/sync_file_system/sync_file_system_api.h"

#include <string>
#include <utility>

#include "base/bind.h"
#include "base/logging.h"
#include "base/strings/stringprintf.h"
#include "chrome/browser/extensions/api/sync_file_system/extension_sync_event_observer.h"
#include "chrome/browser/extensions/api/sync_file_system/extension_sync_event_observer_factory.h"
#include "chrome/browser/extensions/api/sync_file_system/sync_file_system_api_helpers.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sync_file_system/drive_backend/drive_file_sync_service.h"
#include "chrome/browser/sync_file_system/sync_file_status.h"
#include "chrome/browser/sync_file_system/sync_file_system_service.h"
#include "chrome/browser/sync_file_system/sync_file_system_service_factory.h"
#include "chrome/common/extensions/api/sync_file_system.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/common/content_client.h"
#include "webkit/browser/fileapi/file_system_context.h"
#include "webkit/browser/fileapi/file_system_url.h"
#include "webkit/browser/quota/quota_manager.h"
#include "webkit/common/fileapi/file_system_types.h"
#include "webkit/common/fileapi/file_system_util.h"

using content::BrowserContext;
using content::BrowserThread;
using sync_file_system::ConflictResolutionPolicy;
using sync_file_system::SyncFileStatus;
using sync_file_system::SyncFileSystemServiceFactory;
using sync_file_system::SyncStatusCode;

namespace extensions {

namespace {

// Error messages.
const char kFileError[] = "File error %d.";
const char kQuotaError[] = "Quota error %d.";
const char kUnsupportedConflictResolutionPolicy[] =
    "Policy %s is not supported.";

sync_file_system::SyncFileSystemService* GetSyncFileSystemService(
    Profile* profile) {
  sync_file_system::SyncFileSystemService* service =
      SyncFileSystemServiceFactory::GetForProfile(profile);
  DCHECK(service);
  ExtensionSyncEventObserver* observer =
      ExtensionSyncEventObserverFactory::GetForProfile(profile);
  DCHECK(observer);
  observer->InitializeForService(service);
  return service;
}

}  // namespace

bool SyncFileSystemDeleteFileSystemFunction::RunImpl() {
  std::string url;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(0, &url));

  scoped_refptr<fileapi::FileSystemContext> file_system_context =
      BrowserContext::GetStoragePartition(
          profile(),
          render_view_host()->GetSiteInstance())->GetFileSystemContext();
  fileapi::FileSystemURL file_system_url(
      file_system_context->CrackURL(GURL(url)));

  BrowserThread::PostTask(
      BrowserThread::IO,
      FROM_HERE,
      Bind(&fileapi::FileSystemContext::DeleteFileSystem,
           file_system_context,
           source_url().GetOrigin(),
           file_system_url.type(),
           Bind(&SyncFileSystemDeleteFileSystemFunction::DidDeleteFileSystem,
                this)));
  return true;
}

void SyncFileSystemDeleteFileSystemFunction::DidDeleteFileSystem(
    base::PlatformFileError error) {
  // Repost to switch from IO thread to UI thread for SendResponse().
  if (!BrowserThread::CurrentlyOn(BrowserThread::UI)) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
    BrowserThread::PostTask(
        BrowserThread::UI,
        FROM_HERE,
        Bind(&SyncFileSystemDeleteFileSystemFunction::DidDeleteFileSystem, this,
             error));
    return;
  }

  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (error != base::PLATFORM_FILE_OK) {
    error_ = base::StringPrintf(kFileError, static_cast<int>(error));
    SetResult(base::Value::CreateBooleanValue(false));
    SendResponse(false);
    return;
  }

  SetResult(base::Value::CreateBooleanValue(true));
  SendResponse(true);
}

bool SyncFileSystemRequestFileSystemFunction::RunImpl() {
  // Initializes sync context for this extension and continue to open
  // a new file system.
  GetSyncFileSystemService(profile())->
      InitializeForApp(
          GetFileSystemContext(),
          source_url().GetOrigin(),
          base::Bind(&self::DidInitializeFileSystemContext, this));
  return true;
}

fileapi::FileSystemContext*
SyncFileSystemRequestFileSystemFunction::GetFileSystemContext() {
  DCHECK(render_view_host());
  return BrowserContext::GetStoragePartition(
      profile(),
      render_view_host()->GetSiteInstance())->GetFileSystemContext();
}

void SyncFileSystemRequestFileSystemFunction::DidInitializeFileSystemContext(
    SyncStatusCode status) {
  if (status != sync_file_system::SYNC_STATUS_OK) {
    error_ = sync_file_system::SyncStatusCodeToString(status);
    SendResponse(false);
    return;
  }

  if (!render_view_host()) {
    // The app seems to have been closed.
    return;
  }

  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      Bind(&fileapi::FileSystemContext::OpenFileSystem,
           GetFileSystemContext(),
           source_url().GetOrigin(),
           fileapi::kFileSystemTypeSyncable,
           fileapi::OPEN_FILE_SYSTEM_CREATE_IF_NONEXISTENT,
           base::Bind(&self::DidOpenFileSystem, this)));
}

void SyncFileSystemRequestFileSystemFunction::DidOpenFileSystem(
    base::PlatformFileError error,
    const std::string& file_system_name,
    const GURL& root_url) {
  // Repost to switch from IO thread to UI thread for SendResponse().
  if (!BrowserThread::CurrentlyOn(BrowserThread::UI)) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        Bind(&SyncFileSystemRequestFileSystemFunction::DidOpenFileSystem,
             this, error, file_system_name, root_url));
    return;
  }

  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (error != base::PLATFORM_FILE_OK) {
    error_ = base::StringPrintf(kFileError, static_cast<int>(error));
    SendResponse(false);
    return;
  }

  base::DictionaryValue* dict = new base::DictionaryValue();
  SetResult(dict);
  dict->SetString("name", file_system_name);
  dict->SetString("root", root_url.spec());
  SendResponse(true);
}

bool SyncFileSystemGetFileStatusFunction::RunImpl() {
  std::string url;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(0, &url));

  scoped_refptr<fileapi::FileSystemContext> file_system_context =
      BrowserContext::GetStoragePartition(
          profile(),
          render_view_host()->GetSiteInstance())->GetFileSystemContext();
  fileapi::FileSystemURL file_system_url(
      file_system_context->CrackURL(GURL(url)));

  SyncFileSystemServiceFactory::GetForProfile(profile())->GetFileSyncStatus(
      file_system_url,
      Bind(&SyncFileSystemGetFileStatusFunction::DidGetFileStatus,
           this));
  return true;
}

void SyncFileSystemGetFileStatusFunction::DidGetFileStatus(
    const SyncStatusCode sync_status_code,
    const SyncFileStatus sync_file_status) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (sync_status_code != sync_file_system::SYNC_STATUS_OK) {
    error_ = sync_file_system::SyncStatusCodeToString(sync_status_code);
    SendResponse(false);
    return;
  }

  // Convert from C++ to JavaScript enum.
  results_ = api::sync_file_system::GetFileStatus::Results::Create(
      SyncFileStatusToExtensionEnum(sync_file_status));
  SendResponse(true);
}

SyncFileSystemGetFileStatusesFunction::SyncFileSystemGetFileStatusesFunction() {
}

SyncFileSystemGetFileStatusesFunction::~SyncFileSystemGetFileStatusesFunction(
    ) {}

bool SyncFileSystemGetFileStatusesFunction::RunImpl() {
  // All FileEntries converted into array of URL Strings in JS custom bindings.
  base::ListValue* file_entry_urls = NULL;
  EXTENSION_FUNCTION_VALIDATE(args_->GetList(0, &file_entry_urls));

  scoped_refptr<fileapi::FileSystemContext> file_system_context =
      BrowserContext::GetStoragePartition(
          profile(),
          render_view_host()->GetSiteInstance())->GetFileSystemContext();

  // Map each file path->SyncFileStatus in the callback map.
  // TODO(calvinlo): Overload GetFileSyncStatus to take in URL array.
  num_expected_results_ = file_entry_urls->GetSize();
  num_results_received_ = 0;
  file_sync_statuses_.clear();
  sync_file_system::SyncFileSystemService* sync_file_system_service =
      SyncFileSystemServiceFactory::GetForProfile(profile());
  for (unsigned int i = 0; i < num_expected_results_; i++) {
    std::string url;
    file_entry_urls->GetString(i, &url);
    fileapi::FileSystemURL file_system_url(
        file_system_context->CrackURL(GURL(url)));

    sync_file_system_service->GetFileSyncStatus(
        file_system_url,
        Bind(&SyncFileSystemGetFileStatusesFunction::DidGetFileStatus,
             this, file_system_url));
  }

  return true;
}

void SyncFileSystemGetFileStatusesFunction::DidGetFileStatus(
    const fileapi::FileSystemURL& file_system_url,
    SyncStatusCode sync_status_code,
    SyncFileStatus sync_file_status) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  num_results_received_++;
  DCHECK_LE(num_results_received_, num_expected_results_);

  file_sync_statuses_[file_system_url] =
      std::make_pair(sync_status_code, sync_file_status);

  // Keep mapping file statuses until all of them have been received.
  // TODO(calvinlo): Get rid of this check when batch version of
  // GetFileSyncStatus(GURL urls[]); is added.
  if (num_results_received_ < num_expected_results_)
    return;

  // All results received. Dump array of statuses into extension enum values.
  // Note that the enum types need to be set as strings manually as the
  // autogenerated Results::Create function thinks the enum values should be
  // returned as int values.
  base::ListValue* status_array = new base::ListValue();
  for (URLToStatusMap::iterator it = file_sync_statuses_.begin();
       it != file_sync_statuses_.end(); ++it) {
    base::DictionaryValue* dict = new base::DictionaryValue();
    status_array->Append(dict);

    fileapi::FileSystemURL url = it->first;
    SyncStatusCode file_error = it->second.first;
    api::sync_file_system::FileStatus file_status =
        SyncFileStatusToExtensionEnum(it->second.second);

    dict->Set("entry", CreateDictionaryValueForFileSystemEntry(
        url, sync_file_system::SYNC_FILE_TYPE_FILE));
    dict->SetString("status", ToString(file_status));

    if (file_error == sync_file_system::SYNC_STATUS_OK)
      continue;
    dict->SetString("error",
                    sync_file_system::SyncStatusCodeToString(file_error));
  }
  SetResult(status_array);

  SendResponse(true);
}

bool SyncFileSystemGetUsageAndQuotaFunction::RunImpl() {
  std::string url;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(0, &url));

  scoped_refptr<fileapi::FileSystemContext> file_system_context =
      BrowserContext::GetStoragePartition(
          profile(),
          render_view_host()->GetSiteInstance())->GetFileSystemContext();
  fileapi::FileSystemURL file_system_url(
      file_system_context->CrackURL(GURL(url)));

  scoped_refptr<quota::QuotaManager> quota_manager =
      BrowserContext::GetStoragePartition(
          profile(),
          render_view_host()->GetSiteInstance())->GetQuotaManager();

  BrowserThread::PostTask(
      BrowserThread::IO,
      FROM_HERE,
      Bind(&quota::QuotaManager::GetUsageAndQuotaForWebApps,
           quota_manager,
           source_url().GetOrigin(),
           fileapi::FileSystemTypeToQuotaStorageType(file_system_url.type()),
           Bind(&SyncFileSystemGetUsageAndQuotaFunction::DidGetUsageAndQuota,
                this)));

  return true;
}

void SyncFileSystemGetUsageAndQuotaFunction::DidGetUsageAndQuota(
      quota::QuotaStatusCode status, int64 usage, int64 quota) {
  // Repost to switch from IO thread to UI thread for SendResponse().
  if (!BrowserThread::CurrentlyOn(BrowserThread::UI)) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
    BrowserThread::PostTask(
        BrowserThread::UI,
        FROM_HERE,
        Bind(&SyncFileSystemGetUsageAndQuotaFunction::DidGetUsageAndQuota, this,
             status, usage, quota));
    return;
  }

  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (status != quota::kQuotaStatusOk) {
    error_ = QuotaStatusCodeToString(status);
    SendResponse(false);
    return;
  }

  api::sync_file_system::StorageInfo info;
  info.usage_bytes = usage;
  info.quota_bytes = quota;
  results_ = api::sync_file_system::GetUsageAndQuota::Results::Create(info);
  SendResponse(true);
}

bool SyncFileSystemSetConflictResolutionPolicyFunction::RunImpl() {
  std::string policy_string;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(0, &policy_string));
  ConflictResolutionPolicy policy = ExtensionEnumToConflictResolutionPolicy(
      api::sync_file_system::ParseConflictResolutionPolicy(policy_string));
  if (policy == sync_file_system::CONFLICT_RESOLUTION_POLICY_UNKNOWN) {
    SetError(base::StringPrintf(kUnsupportedConflictResolutionPolicy,
                                policy_string.c_str()));
    return false;
  }
  sync_file_system::SyncFileSystemService* service = GetSyncFileSystemService(
      profile());
  DCHECK(service);
  SyncStatusCode status = service->SetConflictResolutionPolicy(policy);
  if (status != sync_file_system::SYNC_STATUS_OK) {
    SetError(sync_file_system::SyncStatusCodeToString(status));
    return false;
  }
  return true;
}

bool SyncFileSystemGetConflictResolutionPolicyFunction::RunImpl() {
  sync_file_system::SyncFileSystemService* service = GetSyncFileSystemService(
      profile());
  DCHECK(service);
  api::sync_file_system::ConflictResolutionPolicy policy =
      ConflictResolutionPolicyToExtensionEnum(
          service->GetConflictResolutionPolicy());
  SetResult(Value::CreateStringValue(
          api::sync_file_system::ToString(policy)));
  return true;
}

}  // namespace extensions
