// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_info_map.h"

#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_set.h"
#include "chrome/common/extensions/incognito_handler.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/browser_thread.h"
#include "extensions/common/constants.h"

using content::BrowserThread;
using extensions::Extension;

namespace {

void CheckOnValidThread() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
}

}  // namespace

struct ExtensionInfoMap::ExtraData {
  // When the extension was installed.
  base::Time install_time;

  // True if the user has allowed this extension to run in incognito mode.
  bool incognito_enabled;

  ExtraData();
  ~ExtraData();
};

ExtensionInfoMap::ExtraData::ExtraData() : incognito_enabled(false) {}

ExtensionInfoMap::ExtraData::~ExtraData() {}

ExtensionInfoMap::ExtensionInfoMap() : signin_process_id_(-1) {}

const extensions::ProcessMap& ExtensionInfoMap::process_map() const {
  return process_map_;
}

void ExtensionInfoMap::AddExtension(const Extension* extension,
                                    base::Time install_time,
                                    bool incognito_enabled) {
  CheckOnValidThread();
  extensions_.Insert(extension);
  disabled_extensions_.Remove(extension->id());

  extra_data_[extension->id()].install_time = install_time;
  extra_data_[extension->id()].incognito_enabled = incognito_enabled;
}

void ExtensionInfoMap::RemoveExtension(const std::string& extension_id,
    const extension_misc::UnloadedExtensionReason reason) {
  CheckOnValidThread();
  const Extension* extension = extensions_.GetByID(extension_id);
  extra_data_.erase(extension_id);  // we don't care about disabled extra data
  bool was_uninstalled = (reason != extension_misc::UNLOAD_REASON_DISABLE &&
                          reason != extension_misc::UNLOAD_REASON_TERMINATE);
  if (extension) {
    if (!was_uninstalled)
      disabled_extensions_.Insert(extension);
    extensions_.Remove(extension_id);
  } else if (was_uninstalled) {
    // If the extension was uninstalled, make sure it's removed from the map of
    // disabled extensions.
    disabled_extensions_.Remove(extension_id);
  } else {
    // NOTE: This can currently happen if we receive multiple unload
    // notifications, e.g. setting incognito-enabled state for a
    // disabled extension (e.g., via sync).  See
    // http://code.google.com/p/chromium/issues/detail?id=50582 .
    NOTREACHED() << extension_id;
  }
}

base::Time ExtensionInfoMap::GetInstallTime(
    const std::string& extension_id) const {
  ExtraDataMap::const_iterator iter = extra_data_.find(extension_id);
  if (iter != extra_data_.end())
    return iter->second.install_time;
  return base::Time();
}

bool ExtensionInfoMap::IsIncognitoEnabled(
    const std::string& extension_id) const {
  // Keep in sync with duplicate in extension_process_manager.cc.
  ExtraDataMap::const_iterator iter = extra_data_.find(extension_id);
  if (iter != extra_data_.end())
    return iter->second.incognito_enabled;
  return false;
}

bool ExtensionInfoMap::CanCrossIncognito(const Extension* extension) const {
  // This is duplicated from ExtensionService :(.
  return IsIncognitoEnabled(extension->id()) &&
      !extensions::IncognitoInfo::IsSplitMode(extension);
}

void ExtensionInfoMap::RegisterExtensionProcess(const std::string& extension_id,
                                                int process_id,
                                                int site_instance_id) {
  if (!process_map_.Insert(extension_id, process_id, site_instance_id)) {
    NOTREACHED() << "Duplicate extension process registration for: "
                 << extension_id << "," << process_id << ".";
  }
}

void ExtensionInfoMap::UnregisterExtensionProcess(
    const std::string& extension_id,
    int process_id,
    int site_instance_id) {
  if (!process_map_.Remove(extension_id, process_id, site_instance_id)) {
    NOTREACHED() << "Unknown extension process registration for: "
                 << extension_id << "," << process_id << ".";
  }
}

void ExtensionInfoMap::UnregisterAllExtensionsInProcess(int process_id) {
  process_map_.RemoveAllFromProcess(process_id);
}

void ExtensionInfoMap::GetExtensionsWithAPIPermissionForSecurityOrigin(
    const GURL& origin,
    int process_id,
    extensions::APIPermission::ID permission,
    ExtensionSet* extensions) const {
  DCHECK(extensions);

  if (origin.SchemeIs(extensions::kExtensionScheme)) {
    const std::string& id = origin.host();
    const Extension* extension = extensions_.GetByID(id);
    if (extension && extension->HasAPIPermission(permission) &&
        process_map_.Contains(id, process_id)) {
      extensions->Insert(extension);
    }
    return;
  }

  ExtensionSet::const_iterator i = extensions_.begin();
  for (; i != extensions_.end(); ++i) {
    if ((*i)->web_extent().MatchesSecurityOrigin(origin) &&
        process_map_.Contains((*i)->id(), process_id) &&
        (*i)->HasAPIPermission(permission)) {
      extensions->Insert(*i);
    }
  }
}

bool ExtensionInfoMap::SecurityOriginHasAPIPermission(
    const GURL& origin, int process_id,
    extensions::APIPermission::ID permission) const {
  ExtensionSet extensions;
  GetExtensionsWithAPIPermissionForSecurityOrigin(
      origin, process_id, permission, &extensions);
  return !extensions.is_empty();
}

ExtensionsQuotaService* ExtensionInfoMap::GetQuotaService() {
  CheckOnValidThread();
  if (!quota_service_)
    quota_service_.reset(new ExtensionsQuotaService());
  return quota_service_.get();
}

void ExtensionInfoMap::SetSigninProcess(int process_id) {
  signin_process_id_ = process_id;
}

bool ExtensionInfoMap::IsSigninProcess(int process_id) const {
  return process_id == signin_process_id_;
}

ExtensionInfoMap::~ExtensionInfoMap() {
  if (quota_service_) {
    BrowserThread::DeleteSoon(BrowserThread::IO, FROM_HERE,
                              quota_service_.release());
  }
}
