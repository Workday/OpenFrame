// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_SYNC_FILE_SYSTEM_INTERNALS_SYNC_FILE_SYSTEM_INTERNALS_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_SYNC_FILE_SYSTEM_INTERNALS_SYNC_FILE_SYSTEM_INTERNALS_HANDLER_H_

#include "base/compiler_specific.h"
#include "chrome/browser/sync_file_system/sync_event_observer.h"
#include "content/public/browser/web_ui_message_handler.h"

class Profile;

namespace syncfs_internals {

// This class handles message from WebUI page of chrome://syncfs-internals/
// for the Sync Service tab. It corresponds to browser/resources/
// sync_file_system_internals/sync_service.html. All methods in this class
// should be called on UI thread.
class SyncFileSystemInternalsHandler
    : public content::WebUIMessageHandler,
      public sync_file_system::SyncEventObserver {
 public:
  explicit SyncFileSystemInternalsHandler(Profile* profile);
  virtual ~SyncFileSystemInternalsHandler();

  // content::WebUIMessageHandler implementation.
  virtual void RegisterMessages() OVERRIDE;

  // sync_file_system::SyncEventObserver interface implementation.
  virtual void OnSyncStateUpdated(
      const GURL& app_origin,
      sync_file_system::SyncServiceState state,
      const std::string& description) OVERRIDE;
  virtual void OnFileSynced(
      const fileapi::FileSystemURL& url,
      sync_file_system::SyncFileStatus status,
      sync_file_system::SyncAction action,
      sync_file_system::SyncDirection direction) OVERRIDE;

 private:
  void GetServiceStatus(const base::ListValue* args);
  void GetNotificationSource(const base::ListValue* args);
  void GetLog(const base::ListValue* args);

  Profile* profile_;

  DISALLOW_COPY_AND_ASSIGN(SyncFileSystemInternalsHandler);
};

}  // namespace syncfs_internals

#endif  // CHROME_BROWSER_UI_WEBUI_SYNC_FILE_SYSTEM_INTERNALS_SYNC_FILE_SYSTEM_INTERNALS_HANDLER_H_
