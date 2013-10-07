// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/pending_extension_manager.h"

#include <algorithm>

#include "base/logging.h"
#include "base/version.h"
#include "chrome/browser/extensions/extension_service.h"
#include "content/public/browser/browser_thread.h"
#include "url/gurl.h"

using content::BrowserThread;

namespace {

// Install predicate used by AddFromExternalUpdateUrl().
bool AlwaysInstall(const extensions::Extension* extension) {
  return true;
}

std::string GetVersionString(const Version& version) {
  return version.IsValid() ? version.GetString() : "invalid";
}

}  // namespace

namespace extensions {

PendingExtensionManager::PendingExtensionManager(
    const ExtensionServiceInterface& service)
    : service_(service) {
}

PendingExtensionManager::~PendingExtensionManager() {}

const PendingExtensionInfo* PendingExtensionManager::GetById(
    const std::string& id) const {
  PendingExtensionList::const_iterator iter;
  for (iter = pending_extension_list_.begin();
       iter != pending_extension_list_.end();
       ++iter) {
    if (id == iter->id())
      return &(*iter);
  }

  return NULL;
}

bool PendingExtensionManager::Remove(const std::string& id) {
  PendingExtensionList::iterator iter;
  for (iter = pending_extension_list_.begin();
       iter != pending_extension_list_.end();
       ++iter) {
    if (id == iter->id()) {
      pending_extension_list_.erase(iter);
      return true;
    }
  }

  return false;
}

bool PendingExtensionManager::IsIdPending(const std::string& id) const {
  PendingExtensionList::const_iterator iter;
  for (iter = pending_extension_list_.begin();
       iter != pending_extension_list_.end();
       ++iter) {
    if (id == iter->id())
      return true;
  }

  return false;
}

bool PendingExtensionManager::HasPendingExtensions() const {
  return !pending_extension_list_.empty();
}

bool PendingExtensionManager::HasPendingExtensionFromSync() const {
  PendingExtensionList::const_iterator iter;
  for (iter = pending_extension_list_.begin();
       iter != pending_extension_list_.end();
       ++iter) {
    if (iter->is_from_sync())
      return true;
  }

  return false;
}

bool PendingExtensionManager::AddFromSync(
    const std::string& id,
    const GURL& update_url,
    PendingExtensionInfo::ShouldAllowInstallPredicate should_allow_install,
    bool install_silently) {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (service_.GetInstalledExtension(id)) {
    LOG(ERROR) << "Trying to add pending extension " << id
               << " which already exists";
    return false;
  }

  // Make sure we don't ever try to install the CWS app, because even though
  // it is listed as a syncable app (because its values need to be synced) it
  // should already be installed on every instance.
  if (id == extension_misc::kWebStoreAppId) {
    NOTREACHED();
    return false;
  }

  const bool kIsFromSync = true;
  const Manifest::Location kSyncLocation = Manifest::INTERNAL;

  return AddExtensionImpl(id, update_url, Version(), should_allow_install,
                          kIsFromSync, install_silently, kSyncLocation);
}

bool PendingExtensionManager::AddFromExtensionImport(
    const std::string& id,
    const GURL& update_url,
    PendingExtensionInfo::ShouldAllowInstallPredicate should_allow_install) {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (service_.GetInstalledExtension(id)) {
    LOG(ERROR) << "Trying to add pending extension " << id
               << " which already exists";
    return false;
  }

  const bool kIsFromSync = false;
  const bool kInstallSilently = true;
  const Manifest::Location kManifestLocation = Manifest::INTERNAL;

  return AddExtensionImpl(id, update_url, Version(), should_allow_install,
                          kIsFromSync, kInstallSilently, kManifestLocation);
}

bool PendingExtensionManager::AddFromExternalUpdateUrl(
    const std::string& id,
    const GURL& update_url,
    Manifest::Location location) {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  const bool kIsFromSync = false;
  const bool kInstallSilently = true;

  const Extension* extension = service_.GetInstalledExtension(id);
  if (extension &&
      location == Manifest::GetHigherPriorityLocation(location,
                                                       extension->location())) {
    // If the new location has higher priority than the location of an existing
    // extension, let the update process overwrite the existing extension.
  } else {
    if (service_.IsExternalExtensionUninstalled(id))
      return false;

    if (extension) {
      LOG(DFATAL) << "Trying to add extension " << id
                  << " by external update, but it is already installed.";
      return false;
    }
  }

  return AddExtensionImpl(id, update_url, Version(), &AlwaysInstall,
                          kIsFromSync, kInstallSilently,
                          location);
}


bool PendingExtensionManager::AddFromExternalFile(
    const std::string& id,
    Manifest::Location install_source,
    const Version& version) {
  // TODO(skerner): AddFromSync() checks to see if the extension is
  // installed, but this method assumes that the caller already
  // made sure it is not installed.  Make all AddFrom*() methods
  // consistent.
  GURL kUpdateUrl = GURL();
  bool kIsFromSync = false;
  bool kInstallSilently = true;

  return AddExtensionImpl(
      id,
      kUpdateUrl,
      version,
      &AlwaysInstall,
      kIsFromSync,
      kInstallSilently,
      install_source);
}

void PendingExtensionManager::GetPendingIdsForUpdateCheck(
    std::list<std::string>* out_ids_for_update_check) const {
  PendingExtensionList::const_iterator iter;
  for (iter = pending_extension_list_.begin();
       iter != pending_extension_list_.end();
       ++iter) {
    Manifest::Location install_source = iter->install_source();

    // Some install sources read a CRX from the filesystem.  They can
    // not be fetched from an update URL, so don't include them in the
    // set of ids.
    if (install_source == Manifest::EXTERNAL_PREF ||
        install_source == Manifest::EXTERNAL_REGISTRY)
      continue;

    out_ids_for_update_check->push_back(iter->id());
  }
}

bool PendingExtensionManager::AddExtensionImpl(
    const std::string& id,
    const GURL& update_url,
    const Version& version,
    PendingExtensionInfo::ShouldAllowInstallPredicate should_allow_install,
    bool is_from_sync,
    bool install_silently,
    Manifest::Location install_source) {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  PendingExtensionInfo info(id,
                            update_url,
                            version,
                            should_allow_install,
                            is_from_sync,
                            install_silently,
                            install_source);

  if (const PendingExtensionInfo* pending = GetById(id)) {
    // Bugs in this code will manifest as sporadic incorrect extension
    // locations in situations where multiple install sources run at the
    // same time. For example, on first login to a chrome os machine, an
    // extension may be requested by sync and the default extension set.
    // The following logging will help diagnose such issues.
    VLOG(1) << "Extension id " << id
            << " was entered for update more than once."
            << "  old location: " << pending->install_source()
            << "  new location: " << install_source
            << "  old version: " << GetVersionString(pending->version())
            << "  new version: " << GetVersionString(version);

    // Never override an existing extension with an older version. Only
    // extensions from local CRX files have a known version; extensions from an
    // update URL will get the latest version.

    // If |pending| has the same or higher precedence than |info| then don't
    // install |info| over |pending|.
    if (pending->CompareTo(info) >= 0)
      return false;

    VLOG(1) << "Overwrite existing record.";

    std::replace(pending_extension_list_.begin(),
                 pending_extension_list_.end(),
                 *pending,
                 info);
  } else {
    pending_extension_list_.push_back(info);
  }

  return true;
}

void PendingExtensionManager::AddForTesting(
    const PendingExtensionInfo& pending_extension_info) {
  pending_extension_list_.push_back(pending_extension_info);
}

}  // namespace extensions
