// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DOWNLOAD_DOWNLOAD_SERVICE_H_
#define CHROME_BROWSER_DOWNLOAD_DOWNLOAD_SERVICE_H_

#include "base/basictypes.h"
#include "base/callback_forward.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service.h"

class ChromeDownloadManagerDelegate;
class DownloadHistory;
class DownloadUIController;
class ExtensionDownloadsEventRouter;
class Profile;

namespace content {
class DownloadManager;
}

// Owning class for ChromeDownloadManagerDelegate.
class DownloadService : public BrowserContextKeyedService {
 public:
  explicit DownloadService(Profile* profile);
  virtual ~DownloadService();

  // Get the download manager delegate, creating it if it doesn't already exist.
  ChromeDownloadManagerDelegate* GetDownloadManagerDelegate();

  // Get the interface to the history system. Returns NULL if profile is
  // incognito or if the DownloadManager hasn't been created yet or if there is
  // no HistoryService for profile.
  DownloadHistory* GetDownloadHistory();

#if !defined(OS_ANDROID)
  ExtensionDownloadsEventRouter* GetExtensionEventRouter() {
    return extension_event_router_.get();
  }
#endif

  // Has a download manager been created?
  bool HasCreatedDownloadManager();

  // Number of downloads associated with this instance of the service.
  int DownloadCount() const;

  // Number of downloads associated with all profiles.
  static int DownloadCountAllProfiles();

  // Sets the DownloadManagerDelegate associated with this object and
  // its DownloadManager.  Takes ownership of |delegate|, and destroys
  // the previous delegate.  For testing.
  void SetDownloadManagerDelegateForTesting(
      ChromeDownloadManagerDelegate* delegate);

  // Will be called to release references on other services as part
  // of Profile shutdown.
  virtual void Shutdown() OVERRIDE;

  // Returns false if at least one extension has disabled the shelf, true
  // otherwise.
  bool IsShelfEnabled();

 private:
  bool download_manager_created_;
  Profile* profile_;

  // ChromeDownloadManagerDelegate may be the target of callbacks from
  // the history service/DB thread and must be kept alive for those
  // callbacks.
  scoped_refptr<ChromeDownloadManagerDelegate> manager_delegate_;

  scoped_ptr<DownloadHistory> download_history_;

  // The UI controller is responsible for observing the download manager and
  // notifying the UI of any new downloads. Its lifetime matches that of the
  // associated download manager.
  scoped_ptr<DownloadUIController> download_ui_;

  // On Android, GET downloads are not handled by the DownloadManager.
  // Once we have extensions on android, we probably need the EventRouter
  // in ContentViewDownloadDelegate which knows about both GET and POST
  // downloads.
#if !defined(OS_ANDROID)
  // The ExtensionDownloadsEventRouter dispatches download creation, change, and
  // erase events to extensions. Like ChromeDownloadManagerDelegate, it's a
  // chrome-level concept and its lifetime should match DownloadManager. There
  // should be a separate EDER for on-record and off-record managers.
  // There does not appear to be a separate ExtensionSystem for on-record and
  // off-record profiles, so ExtensionSystem cannot own the EDER.
  scoped_ptr<ExtensionDownloadsEventRouter> extension_event_router_;
#endif

  DISALLOW_COPY_AND_ASSIGN(DownloadService);
};

#endif  // CHROME_BROWSER_DOWNLOAD_DOWNLOAD_SERVICE_H_
