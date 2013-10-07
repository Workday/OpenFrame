// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_WEBSTORE_INSTALLER_H_
#define CHROME_BROWSER_EXTENSIONS_WEBSTORE_INSTALLER_H_

#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/supports_user_data.h"
#include "base/values.h"
#include "chrome/browser/extensions/extension_install_prompt.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/download_item.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "net/base/net_errors.h"
#include "ui/gfx/image/image_skia.h"
#include "url/gurl.h"

class Profile;

namespace base {
class FilePath;
}

namespace content {
class NavigationController;
}

namespace extensions {

class Manifest;

// Downloads and installs extensions from the web store.
class WebstoreInstaller :public content::NotificationObserver,
                         public content::DownloadItem::Observer,
                         public base::RefCountedThreadSafe<
  WebstoreInstaller, content::BrowserThread::DeleteOnUIThread> {
 public:
  enum Flag {
    FLAG_NONE = 0,

    // Inline installs trigger slightly different behavior (install source
    // is different, download referrers are the item's page in the gallery).
    FLAG_INLINE_INSTALL = 1 << 0
  };

  enum FailureReason {
    FAILURE_REASON_CANCELLED,
    FAILURE_REASON_OTHER
  };

  class Delegate {
   public:
    virtual void OnExtensionDownloadStarted(const std::string& id,
                                            content::DownloadItem* item);
    virtual void OnExtensionDownloadProgress(const std::string& id,
                                             content::DownloadItem* item);
    virtual void OnExtensionInstallSuccess(const std::string& id) = 0;
    virtual void OnExtensionInstallFailure(const std::string& id,
                                           const std::string& error,
                                           FailureReason reason) = 0;

   protected:
    virtual ~Delegate() {}
  };

  // Contains information about what parts of the extension install process can
  // be skipped or modified. If one of these is present, it means that a CRX
  // download was initiated by WebstoreInstaller. The Approval instance should
  // be checked further for additional details.
  struct Approval : public base::SupportsUserData::Data {
    static scoped_ptr<Approval> CreateWithInstallPrompt(Profile* profile);
    static scoped_ptr<Approval> CreateWithNoInstallPrompt(
        Profile* profile,
        const std::string& extension_id,
        scoped_ptr<base::DictionaryValue> parsed_manifest);

    virtual ~Approval();

    // The extension id that was approved for installation.
    std::string extension_id;

    // The profile the extension should be installed into.
    Profile* profile;

    // The expected manifest, before localization.
    scoped_ptr<Manifest> manifest;

    // Whether to use a bubble notification when an app is installed, instead of
    // the default behavior of transitioning to the new tab page.
    bool use_app_installed_bubble;

    // Whether to skip the post install UI like the extension installed bubble.
    bool skip_post_install_ui;

    // Whether to skip the install dialog once the extension has been downloaded
    // and unpacked. One reason this can be true is that in the normal webstore
    // installation, the dialog is shown earlier, before any download is done,
    // so there's no need to show it again.
    bool skip_install_dialog;

    // Whether we should enable the launcher before installing the app.
    bool enable_launcher;

    // Used to show the install dialog.
    ExtensionInstallPrompt::ShowDialogCallback show_dialog_callback;

    // The icon to use to display the extension while it is installing.
    gfx::ImageSkia installing_icon;

   private:
    Approval();
  };

  // Gets the Approval associated with the |download|, or NULL if there's none.
  // Note that the Approval is owned by |download|.
  static const Approval* GetAssociatedApproval(
      const content::DownloadItem& download);

  // Creates a WebstoreInstaller for downloading and installing the extension
  // with the given |id| from the Chrome Web Store. If |delegate| is not NULL,
  // it will be notified when the install succeeds or fails. The installer will
  // use the specified |controller| to download the extension. Only one
  // WebstoreInstaller can use a specific controller at any given time. This
  // also associates the |approval| with this install.
  // Note: the delegate should stay alive until being called back.
  WebstoreInstaller(Profile* profile,
                    Delegate* delegate,
                    content::NavigationController* controller,
                    const std::string& id,
                    scoped_ptr<Approval> approval,
                    int flags);

  // Starts downloading and installing the extension.
  void Start();

  // content::NotificationObserver
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // Removes the reference to the delegate passed in the constructor. Used when
  // the delegate object must be deleted before this object.
  void InvalidateDelegate();

  // Instead of using the default download directory, use |directory| instead.
  // This does *not* transfer ownership of |directory|.
  static void SetDownloadDirectoryForTests(base::FilePath* directory);

 private:
  FRIEND_TEST_ALL_PREFIXES(WebstoreInstallerTest, PlatformParams);
  friend struct content::BrowserThread::DeleteOnThread<
   content::BrowserThread::UI>;
  friend class base::DeleteHelper<WebstoreInstaller>;
  virtual ~WebstoreInstaller();

  // Helper to get install URL.
  static GURL GetWebstoreInstallURL(const std::string& extension_id,
                                    const std::string& install_source);

  // DownloadManager::DownloadUrl callback.
  void OnDownloadStarted(content::DownloadItem* item, net::Error error);

  // DownloadItem::Observer implementation:
  virtual void OnDownloadUpdated(content::DownloadItem* download) OVERRIDE;
  virtual void OnDownloadDestroyed(content::DownloadItem* download) OVERRIDE;

  // Starts downloading the extension to |file_path|.
  void StartDownload(const base::FilePath& file_path);

  // Reports an install |error| to the delegate for the given extension if this
  // managed its installation. This also removes the associated PendingInstall.
  void ReportFailure(const std::string& error, FailureReason reason);

  // Reports a successful install to the delegate for the given extension if
  // this managed its installation. This also removes the associated
  // PendingInstall.
  void ReportSuccess();

  content::NotificationRegistrar registrar_;
  Profile* profile_;
  Delegate* delegate_;
  content::NavigationController* controller_;
  std::string id_;
  // The DownloadItem is owned by the DownloadManager and is valid from when
  // OnDownloadStarted is called (with no error) until OnDownloadDestroyed().
  content::DownloadItem* download_item_;
  scoped_ptr<Approval> approval_;
  GURL download_url_;
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_WEBSTORE_INSTALLER_H_
