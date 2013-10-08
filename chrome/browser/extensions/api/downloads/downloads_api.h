// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_DOWNLOADS_DOWNLOADS_API_H_
#define CHROME_BROWSER_EXTENSIONS_API_DOWNLOADS_DOWNLOADS_API_H_

#include <set>
#include <string>

#include "base/files/file_path.h"
#include "base/memory/singleton.h"
#include "base/strings/string16.h"
#include "base/values.h"
#include "chrome/browser/download/all_download_item_notifier.h"
#include "chrome/browser/download/download_danger_prompt.h"
#include "chrome/browser/download/download_path_reservation_tracker.h"
#include "chrome/browser/extensions/event_router.h"
#include "chrome/browser/extensions/extension_function.h"
#include "chrome/browser/extensions/extension_warning_set.h"
#include "chrome/common/extensions/api/downloads.h"
#include "content/public/browser/download_item.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"

class DownloadFileIconExtractor;
class DownloadQuery;

namespace content {
class ResourceContext;
class ResourceDispatcherHost;
}

// Functions in the chrome.downloads namespace facilitate
// controlling downloads from extensions. See the full API doc at
// http://goo.gl/6hO1n

namespace download_extension_errors {

// Errors that can be returned through chrome.runtime.lastError.message.
extern const char kEmptyFile[];
extern const char kFileAlreadyDeleted[];
extern const char kIconNotFound[];
extern const char kInvalidDangerType[];
extern const char kInvalidFilename[];
extern const char kInvalidFilter[];
extern const char kInvalidHeader[];
extern const char kInvalidId[];
extern const char kInvalidOrderBy[];
extern const char kInvalidQueryLimit[];
extern const char kInvalidState[];
extern const char kInvalidURL[];
extern const char kInvisibleContext[];
extern const char kNotComplete[];
extern const char kNotDangerous[];
extern const char kNotInProgress[];
extern const char kNotResumable[];
extern const char kOpenPermission[];
extern const char kShelfDisabled[];
extern const char kShelfPermission[];
extern const char kTooManyListeners[];
extern const char kUnexpectedDeterminer[];

}  // namespace download_extension_errors


class DownloadedByExtension : public base::SupportsUserData::Data {
 public:
  static DownloadedByExtension* Get(content::DownloadItem* item);

  DownloadedByExtension(content::DownloadItem* item,
                        const std::string& id,
                        const std::string& name);

  const std::string& id() const { return id_; }
  const std::string& name() const { return name_; }

 private:
  static const char kKey[];

  std::string id_;
  std::string name_;

  DISALLOW_COPY_AND_ASSIGN(DownloadedByExtension);
};

class DownloadsDownloadFunction : public AsyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("downloads.download", DOWNLOADS_DOWNLOAD)
  DownloadsDownloadFunction();
  virtual bool RunImpl() OVERRIDE;

 protected:
  virtual ~DownloadsDownloadFunction();

 private:
  void OnStarted(
      const base::FilePath& creator_suggested_filename,
      extensions::api::downloads::FilenameConflictAction
        creator_conflict_action,
      content::DownloadItem* item,
      net::Error error);

  DISALLOW_COPY_AND_ASSIGN(DownloadsDownloadFunction);
};

class DownloadsSearchFunction : public SyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("downloads.search", DOWNLOADS_SEARCH)
  DownloadsSearchFunction();
  virtual bool RunImpl() OVERRIDE;

 protected:
  virtual ~DownloadsSearchFunction();

 private:
  DISALLOW_COPY_AND_ASSIGN(DownloadsSearchFunction);
};

class DownloadsPauseFunction : public SyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("downloads.pause", DOWNLOADS_PAUSE)
  DownloadsPauseFunction();
  virtual bool RunImpl() OVERRIDE;

 protected:
  virtual ~DownloadsPauseFunction();

 private:
  DISALLOW_COPY_AND_ASSIGN(DownloadsPauseFunction);
};

class DownloadsResumeFunction : public SyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("downloads.resume", DOWNLOADS_RESUME)
  DownloadsResumeFunction();
  virtual bool RunImpl() OVERRIDE;

 protected:
  virtual ~DownloadsResumeFunction();

 private:
  DISALLOW_COPY_AND_ASSIGN(DownloadsResumeFunction);
};

class DownloadsCancelFunction : public SyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("downloads.cancel", DOWNLOADS_CANCEL)
  DownloadsCancelFunction();
  virtual bool RunImpl() OVERRIDE;

 protected:
  virtual ~DownloadsCancelFunction();

 private:
  DISALLOW_COPY_AND_ASSIGN(DownloadsCancelFunction);
};

class DownloadsEraseFunction : public SyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("downloads.erase", DOWNLOADS_ERASE)
  DownloadsEraseFunction();
  virtual bool RunImpl() OVERRIDE;

 protected:
  virtual ~DownloadsEraseFunction();

 private:
  DISALLOW_COPY_AND_ASSIGN(DownloadsEraseFunction);
};

class DownloadsRemoveFileFunction : public AsyncExtensionFunction,
                                    public content::DownloadItem::Observer {
 public:
  DECLARE_EXTENSION_FUNCTION("downloads.removeFile", DOWNLOADS_REMOVEFILE)
  DownloadsRemoveFileFunction();
  virtual bool RunImpl() OVERRIDE;

 protected:
  virtual ~DownloadsRemoveFileFunction();

 private:
  virtual void OnDownloadUpdated(content::DownloadItem* item) OVERRIDE;
  virtual void OnDownloadDestroyed(content::DownloadItem* item) OVERRIDE;

  content::DownloadItem* item_;

  DISALLOW_COPY_AND_ASSIGN(DownloadsRemoveFileFunction);
};

class DownloadsAcceptDangerFunction : public AsyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("downloads.acceptDanger", DOWNLOADS_ACCEPTDANGER)
  DownloadsAcceptDangerFunction();
  virtual bool RunImpl() OVERRIDE;

 protected:
  virtual ~DownloadsAcceptDangerFunction();
  void DangerPromptCallback(int download_id,
                            DownloadDangerPrompt::Action action);

 private:
  DISALLOW_COPY_AND_ASSIGN(DownloadsAcceptDangerFunction);
};

class DownloadsShowFunction : public AsyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("downloads.show", DOWNLOADS_SHOW)
  DownloadsShowFunction();
  virtual bool RunImpl() OVERRIDE;

 protected:
  virtual ~DownloadsShowFunction();

 private:
  DISALLOW_COPY_AND_ASSIGN(DownloadsShowFunction);
};

class DownloadsShowDefaultFolderFunction : public AsyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION(
      "downloads.showDefaultFolder", DOWNLOADS_SHOWDEFAULTFOLDER)
  DownloadsShowDefaultFolderFunction();
  virtual bool RunImpl() OVERRIDE;

 protected:
  virtual ~DownloadsShowDefaultFolderFunction();

 private:
  DISALLOW_COPY_AND_ASSIGN(DownloadsShowDefaultFolderFunction);
};

class DownloadsOpenFunction : public SyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("downloads.open", DOWNLOADS_OPEN)
  DownloadsOpenFunction();
  virtual bool RunImpl() OVERRIDE;

 protected:
  virtual ~DownloadsOpenFunction();

 private:
  DISALLOW_COPY_AND_ASSIGN(DownloadsOpenFunction);
};

class DownloadsSetShelfEnabledFunction : public SyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("downloads.setShelfEnabled",
                             DOWNLOADS_SETSHELFENABLED)
  DownloadsSetShelfEnabledFunction();
  virtual bool RunImpl() OVERRIDE;

 protected:
  virtual ~DownloadsSetShelfEnabledFunction();

 private:
  DISALLOW_COPY_AND_ASSIGN(DownloadsSetShelfEnabledFunction);
};

class DownloadsDragFunction : public AsyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("downloads.drag", DOWNLOADS_DRAG)
  DownloadsDragFunction();
  virtual bool RunImpl() OVERRIDE;

 protected:
  virtual ~DownloadsDragFunction();

 private:
  DISALLOW_COPY_AND_ASSIGN(DownloadsDragFunction);
};

class DownloadsGetFileIconFunction : public AsyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("downloads.getFileIcon", DOWNLOADS_GETFILEICON)
  DownloadsGetFileIconFunction();
  virtual bool RunImpl() OVERRIDE;
  void SetIconExtractorForTesting(DownloadFileIconExtractor* extractor);

 protected:
  virtual ~DownloadsGetFileIconFunction();

 private:
  void OnIconURLExtracted(const std::string& url);
  base::FilePath path_;
  scoped_ptr<DownloadFileIconExtractor> icon_extractor_;
  DISALLOW_COPY_AND_ASSIGN(DownloadsGetFileIconFunction);
};

// Observes a single DownloadManager and many DownloadItems and dispatches
// onCreated and onErased events.
class ExtensionDownloadsEventRouter : public extensions::EventRouter::Observer,
                                      public content::NotificationObserver,
                                      public AllDownloadItemNotifier::Observer {
 public:
  typedef base::Callback<void(
      const base::FilePath& changed_filename,
      DownloadPathReservationTracker::FilenameConflictAction)>
    FilenameChangedCallback;

  // The logic for how to handle conflicting filename suggestions from multiple
  // extensions is split out here for testing.
  static void DetermineFilenameInternal(
      const base::FilePath& filename,
      extensions::api::downloads::FilenameConflictAction conflict_action,
      const std::string& suggesting_extension_id,
      const base::Time& suggesting_install_time,
      const std::string& incumbent_extension_id,
      const base::Time& incumbent_install_time,
      std::string* winner_extension_id,
      base::FilePath* determined_filename,
      extensions::api::downloads::FilenameConflictAction*
        determined_conflict_action,
      extensions::ExtensionWarningSet* warnings);

  // A downloads.onDeterminingFilename listener has returned. If the extension
  // wishes to override the download's filename, then |filename| will be
  // non-empty. |filename| will be interpreted as a relative path, appended to
  // the default downloads directory. If the extension wishes to overwrite any
  // existing files, then |overwrite| will be true. Returns true on success,
  // false otherwise.
  static bool DetermineFilename(
      Profile* profile,
      bool include_incognito,
      const std::string& ext_id,
      int download_id,
      const base::FilePath& filename,
      extensions::api::downloads::FilenameConflictAction conflict_action,
      std::string* error);

  explicit ExtensionDownloadsEventRouter(
      Profile* profile, content::DownloadManager* manager);
  virtual ~ExtensionDownloadsEventRouter();

  void SetShelfEnabled(const extensions::Extension* extension, bool enabled);
  bool IsShelfEnabled() const;

  // Called by ChromeDownloadManagerDelegate during the filename determination
  // process, allows extensions to change the item's target filename. If no
  // extension wants to change the target filename, then |no_change| will be
  // called and the filename determination process will continue as normal. If
  // an extension wants to change the target filename, then |change| will be
  // called with the new filename and a flag indicating whether the new file
  // should overwrite any old files of the same name.
  void OnDeterminingFilename(
      content::DownloadItem* item,
      const base::FilePath& suggested_path,
      const base::Closure& no_change,
      const FilenameChangedCallback& change);

  // AllDownloadItemNotifier::Observer
  virtual void OnDownloadCreated(
      content::DownloadManager* manager,
      content::DownloadItem* download_item) OVERRIDE;
  virtual void OnDownloadUpdated(
      content::DownloadManager* manager,
      content::DownloadItem* download_item) OVERRIDE;
  virtual void OnDownloadRemoved(
      content::DownloadManager* manager,
      content::DownloadItem* download_item) OVERRIDE;

  // extensions::EventRouter::Observer
  virtual void OnListenerRemoved(
      const extensions::EventListenerInfo& details) OVERRIDE;

  // Used for testing.
  struct DownloadsNotificationSource {
    std::string event_name;
    Profile* profile;
  };

 private:
  void DispatchEvent(
      const char* event_name,
      bool include_incognito,
      const extensions::Event::WillDispatchCallback& will_dispatch_callback,
      base::Value* json_arg);

  // content::NotificationObserver
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  Profile* profile_;
  AllDownloadItemNotifier notifier_;
  std::set<const extensions::Extension*> shelf_disabling_extensions_;
  content::NotificationRegistrar registrar_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionDownloadsEventRouter);
};

#endif  // CHROME_BROWSER_EXTENSIONS_API_DOWNLOADS_DOWNLOADS_API_H_
