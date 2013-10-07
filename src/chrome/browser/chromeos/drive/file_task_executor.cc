// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/drive/file_task_executor.h"

#include <string>
#include <vector>

#include "chrome/browser/chromeos/drive/drive.pb.h"
#include "chrome/browser/chromeos/drive/drive_integration_service.h"
#include "chrome/browser/chromeos/drive/file_system_interface.h"
#include "chrome/browser/drive/drive_service_interface.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "content/public/browser/browser_thread.h"
#include "webkit/browser/fileapi/file_system_url.h"

using fileapi::FileSystemURL;

namespace drive {

FileTaskExecutor::FileTaskExecutor(Profile* profile,
                                   const std::string& app_id)
  : profile_(profile),
    app_id_(app_id),
    current_index_(0),
    weak_ptr_factory_(this) {
}

FileTaskExecutor::~FileTaskExecutor() {
}

void FileTaskExecutor::Execute(
    const std::vector<FileSystemURL>& file_urls,
    const file_manager::file_tasks::FileTaskFinishedCallback& done) {
  std::vector<base::FilePath> paths;
  for (size_t i = 0; i < file_urls.size(); ++i) {
    base::FilePath path = util::ExtractDrivePathFromFileSystemUrl(file_urls[i]);
    if (path.empty()) {
      Done(false);
      return;
    }
    paths.push_back(path);
  }

  DriveIntegrationService* integration_service =
      DriveIntegrationServiceFactory::GetForProfile(profile_);
  DCHECK_EQ(current_index_, 0);
  if (!integration_service || !integration_service->file_system()) {
    Done(false);
    return;
  }
  FileSystemInterface* file_system = integration_service->file_system();

  done_ = done;
  // Reset the index, so we know when we're done.
  current_index_ = paths.size();

  for (size_t i = 0; i < paths.size(); ++i) {
    file_system->GetResourceEntryByPath(
        paths[i],
        base::Bind(&FileTaskExecutor::OnFileEntryFetched,
                   weak_ptr_factory_.GetWeakPtr()));
  }
}

void FileTaskExecutor::OnFileEntryFetched(FileError error,
                                          scoped_ptr<ResourceEntry> entry) {
  DriveIntegrationService* integration_service =
      DriveIntegrationServiceFactory::GetForProfile(profile_);

  // Here, we are only interested in files.
  if (entry.get() && !entry->has_file_specific_info())
    error = FILE_ERROR_NOT_FOUND;

  if (!integration_service || error != FILE_ERROR_OK) {
    Done(false);
    return;
  }

  DriveServiceInterface* drive_service =
      integration_service->drive_service();

  // Send off a request for the drive service to authorize the apps for the
  // current document entry for this document so we can get the
  // open-with-<app_id> urls from the document entry.
  drive_service->AuthorizeApp(entry->resource_id(),
                              app_id_,
                              base::Bind(&FileTaskExecutor::OnAppAuthorized,
                                         weak_ptr_factory_.GetWeakPtr(),
                                         entry->resource_id()));
}

void FileTaskExecutor::OnAppAuthorized(const std::string& resource_id,
                                       google_apis::GDataErrorCode error,
                                       const GURL& open_link) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));

  DriveIntegrationService* integration_service =
      DriveIntegrationServiceFactory::GetForProfile(profile_);

  if (!integration_service || error != google_apis::HTTP_SUCCESS) {
    Done(false);
    return;
  }

  if (open_link.is_empty()) {
    Done(false);
    return;
  }

  Browser* browser = chrome::FindOrCreateTabbedBrowser(
      profile_ ? profile_ : ProfileManager::GetDefaultProfileOrOffTheRecord(),
      chrome::HOST_DESKTOP_TYPE_ASH);

  chrome::AddSelectedTabWithURL(browser, open_link,
                                content::PAGE_TRANSITION_LINK);
  // If the current browser is not tabbed then the new tab will be created
  // in a different browser. Make sure it is visible.
  browser->window()->Show();

  // We're done with this file.  If this is the last one, then we're done.
  current_index_--;
  DCHECK_GE(current_index_, 0);
  if (current_index_ == 0)
    Done(true);
}

void FileTaskExecutor::Done(bool success) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  if (!done_.is_null())
    done_.Run(success);
  delete this;
}

}  // namespace drive
