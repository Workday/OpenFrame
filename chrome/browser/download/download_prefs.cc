// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/download/download_prefs.h"

#include <string>
#include <vector>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/prefs/pref_service.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/download/chrome_download_manager_delegate.h"
#include "chrome/browser/download/download_extensions.h"
#include "chrome/browser/download/download_service.h"
#include "chrome/browser/download/download_service_factory.h"
#include "chrome/browser/download/download_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/pref_names.h"
#include "components/user_prefs/pref_registry_syncable.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/save_page_type.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/drive/drive_integration_service.h"
#include "chrome/browser/chromeos/drive/file_system_util.h"
#endif

using content::BrowserContext;
using content::BrowserThread;
using content::DownloadManager;

DownloadPrefs::DownloadPrefs(Profile* profile) : profile_(profile) {
  PrefService* prefs = profile->GetPrefs();

  // If the download path is dangerous we forcefully reset it. But if we do
  // so we set a flag to make sure we only do it once, to avoid fighting
  // the user if he really wants it on an unsafe place such as the desktop.
  if (!prefs->GetBoolean(prefs::kDownloadDirUpgraded)) {
    base::FilePath current_download_dir = prefs->GetFilePath(
        prefs::kDownloadDefaultDirectory);
    if (download_util::DownloadPathIsDangerous(current_download_dir)) {
      prefs->SetFilePath(prefs::kDownloadDefaultDirectory,
                         download_util::GetDefaultDownloadDirectory());
    }
    prefs->SetBoolean(prefs::kDownloadDirUpgraded, true);
  }

  prompt_for_download_.Init(prefs::kPromptForDownload, prefs);
  download_path_.Init(prefs::kDownloadDefaultDirectory, prefs);
  save_file_path_.Init(prefs::kSaveFileDefaultDirectory, prefs);
  save_file_type_.Init(prefs::kSaveFileType, prefs);

  // We store any file extension that should be opened automatically at
  // download completion in this pref.
  std::string extensions_to_open =
      prefs->GetString(prefs::kDownloadExtensionsToOpen);
  std::vector<std::string> extensions;
  base::SplitString(extensions_to_open, ':', &extensions);

  for (size_t i = 0; i < extensions.size(); ++i) {
#if defined(OS_POSIX)
    base::FilePath path(extensions[i]);
#elif defined(OS_WIN)
    base::FilePath path(UTF8ToWide(extensions[i]));
#endif
    if (!extensions[i].empty() &&
        download_util::GetFileDangerLevel(path) == download_util::NOT_DANGEROUS)
      auto_open_.insert(path.value());
  }
}

DownloadPrefs::~DownloadPrefs() {
}

// static
void DownloadPrefs::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterBooleanPref(
      prefs::kPromptForDownload,
      false,
      user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
  registry->RegisterStringPref(
      prefs::kDownloadExtensionsToOpen,
      std::string(),
      user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
  registry->RegisterBooleanPref(
      prefs::kDownloadDirUpgraded,
      false,
      user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
  registry->RegisterIntegerPref(
      prefs::kSaveFileType,
      content::SAVE_PAGE_TYPE_AS_COMPLETE_HTML,
      user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);

  // The default download path is userprofile\download.
  const base::FilePath& default_download_path =
      download_util::GetDefaultDownloadDirectory();
  registry->RegisterFilePathPref(
      prefs::kDownloadDefaultDirectory,
      default_download_path,
      user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
  registry->RegisterFilePathPref(
      prefs::kSaveFileDefaultDirectory,
      default_download_path,
      user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);

#if defined(OS_CHROMEOS)
  // Ensure that the download directory specified in the preferences exists.
  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      base::Bind(base::IgnoreResult(&file_util::CreateDirectory),
                 default_download_path));
#endif  // defined(OS_CHROMEOS)
}

// static
DownloadPrefs* DownloadPrefs::FromDownloadManager(
    DownloadManager* download_manager) {
  ChromeDownloadManagerDelegate* delegate =
      static_cast<ChromeDownloadManagerDelegate*>(
          download_manager->GetDelegate());
  return delegate->download_prefs();
}

// static
DownloadPrefs* DownloadPrefs::FromBrowserContext(
    content::BrowserContext* context) {
  return FromDownloadManager(BrowserContext::GetDownloadManager(context));
}

base::FilePath DownloadPrefs::DownloadPath() const {
#if defined(OS_CHROMEOS)
  // If the download path is under /drive, and DriveIntegrationService isn't
  // available (which it isn't for incognito mode, for instance), use the
  // default download directory (/Downloads).
  if (drive::util::IsUnderDriveMountPoint(*download_path_) &&
      !drive::DriveIntegrationServiceFactory::GetForProfile(profile_))
    return download_util::GetDefaultDownloadDirectory();
#endif
  return *download_path_;
}

void DownloadPrefs::SetDownloadPath(const base::FilePath& path) {
  download_path_.SetValue(path);
  SetSaveFilePath(path);
}

base::FilePath DownloadPrefs::SaveFilePath() const {
  return *save_file_path_;
}

void DownloadPrefs::SetSaveFilePath(const base::FilePath& path) {
  save_file_path_.SetValue(path);
}

void DownloadPrefs::SetSaveFileType(int type) {
  save_file_type_.SetValue(type);
}

bool DownloadPrefs::PromptForDownload() const {
  // If the DownloadDirectory policy is set, then |prompt_for_download_| should
  // always be false.
  DCHECK(!download_path_.IsManaged() || !prompt_for_download_.GetValue());
  return *prompt_for_download_;
}

bool DownloadPrefs::IsDownloadPathManaged() const {
  return download_path_.IsManaged();
}

bool DownloadPrefs::IsAutoOpenUsed() const {
  return !auto_open_.empty();
}

bool DownloadPrefs::IsAutoOpenEnabledBasedOnExtension(
    const base::FilePath& path) const {
  base::FilePath::StringType extension = path.Extension();
  if (extension.empty())
    return false;
  DCHECK(extension[0] == base::FilePath::kExtensionSeparator);
  extension.erase(0, 1);
  return auto_open_.find(extension) != auto_open_.end();
}

bool DownloadPrefs::EnableAutoOpenBasedOnExtension(
    const base::FilePath& file_name) {
  base::FilePath::StringType extension = file_name.Extension();
  if (extension.empty())
    return false;
  DCHECK(extension[0] == base::FilePath::kExtensionSeparator);
  extension.erase(0, 1);

  auto_open_.insert(extension);
  SaveAutoOpenState();
  return true;
}

void DownloadPrefs::DisableAutoOpenBasedOnExtension(
    const base::FilePath& file_name) {
  base::FilePath::StringType extension = file_name.Extension();
  if (extension.empty())
    return;
  DCHECK(extension[0] == base::FilePath::kExtensionSeparator);
  extension.erase(0, 1);
  auto_open_.erase(extension);
  SaveAutoOpenState();
}

void DownloadPrefs::ResetAutoOpen() {
  auto_open_.clear();
  SaveAutoOpenState();
}

void DownloadPrefs::SaveAutoOpenState() {
  std::string extensions;
  for (AutoOpenSet::iterator it = auto_open_.begin();
       it != auto_open_.end(); ++it) {
#if defined(OS_POSIX)
    std::string this_extension = *it;
#elif defined(OS_WIN)
    // TODO(phajdan.jr): Why we're using Sys conversion here, but not in ctor?
    std::string this_extension = base::SysWideToUTF8(*it);
#endif
    extensions += this_extension + ":";
  }
  if (!extensions.empty())
    extensions.erase(extensions.size() - 1);

  profile_->GetPrefs()->SetString(prefs::kDownloadExtensionsToOpen, extensions);
}

bool DownloadPrefs::AutoOpenCompareFunctor::operator()(
    const base::FilePath::StringType& a,
    const base::FilePath::StringType& b) const {
  return base::FilePath::CompareLessIgnoreCase(a, b);
}
