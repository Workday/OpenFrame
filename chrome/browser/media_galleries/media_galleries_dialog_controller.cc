// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media_galleries/media_galleries_dialog_controller.h"

#include "base/path_service.h"
#include "base/stl_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/media_galleries/media_file_system_registry.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/storage_monitor/storage_info.h"
#include "chrome/browser/storage_monitor/storage_monitor.h"
#include "chrome/browser/ui/chrome_select_file_policy.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/permissions/media_galleries_permission.h"
#include "chrome/common/extensions/permissions/permissions_data.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/text/bytes_formatting.h"

using extensions::APIPermission;
using extensions::Extension;

namespace chrome {

namespace {

// Comparator for sorting GalleryPermissionsVector -- sorts
// allowed galleries low, and then sorts by absolute path.
bool GalleriesVectorComparator(
    const MediaGalleriesDialogController::GalleryPermission& a,
    const MediaGalleriesDialogController::GalleryPermission& b) {
  if (a.allowed && !b.allowed)
    return true;
  if (!a.allowed && b.allowed)
    return false;

  return a.pref_info.AbsolutePath() < b.pref_info.AbsolutePath();
}

}  // namespace

MediaGalleriesDialogController::MediaGalleriesDialogController(
    content::WebContents* web_contents,
    const Extension& extension,
    const base::Closure& on_finish)
      : web_contents_(web_contents),
        extension_(&extension),
        on_finish_(on_finish) {
  // Passing unretained pointer is safe, since the dialog controller
  // is self-deleting, and so won't be deleted until it can be shown
  // and then closed.
  StorageMonitor::GetInstance()->EnsureInitialized(base::Bind(
      &MediaGalleriesDialogController::OnStorageMonitorInitialized,
      base::Unretained(this)));
}

void MediaGalleriesDialogController::OnStorageMonitorInitialized() {
  MediaFileSystemRegistry* registry =
      g_browser_process->media_file_system_registry();
  preferences_ = registry->GetPreferences(
      Profile::FromBrowserContext(web_contents_->GetBrowserContext()));
  InitializePermissions();

  dialog_.reset(MediaGalleriesDialog::Create(this));

  StorageMonitor::GetInstance()->AddObserver(this);

  preferences_->AddGalleryChangeObserver(this);
}

MediaGalleriesDialogController::MediaGalleriesDialogController(
    const extensions::Extension& extension)
    : web_contents_(NULL),
      extension_(&extension),
      preferences_(NULL) {}

MediaGalleriesDialogController::~MediaGalleriesDialogController() {
  if (chrome::StorageMonitor::GetInstance())
    StorageMonitor::GetInstance()->RemoveObserver(this);

  if (select_folder_dialog_.get())
    select_folder_dialog_->ListenerDestroyed();
}

string16 MediaGalleriesDialogController::GetHeader() const {
  return l10n_util::GetStringUTF16(IDS_MEDIA_GALLERIES_DIALOG_HEADER);
}

string16 MediaGalleriesDialogController::GetSubtext() const {
  extensions::MediaGalleriesPermission::CheckParam read_param(
      extensions::MediaGalleriesPermission::kReadPermission);
  extensions::MediaGalleriesPermission::CheckParam copy_to_param(
      extensions::MediaGalleriesPermission::kCopyToPermission);
  bool has_read_permission =
      extensions::PermissionsData::CheckAPIPermissionWithParam(
          extension_, APIPermission::kMediaGalleries, &read_param);
  bool has_copy_to_permission =
      extensions::PermissionsData::CheckAPIPermissionWithParam(
          extension_, APIPermission::kMediaGalleries, &copy_to_param);

  int id;
  if (has_read_permission && has_copy_to_permission)
    id = IDS_MEDIA_GALLERIES_DIALOG_SUBTEXT_READ_WRITE;
  else if (has_copy_to_permission)
    id = IDS_MEDIA_GALLERIES_DIALOG_SUBTEXT_WRITE_ONLY;
  else
    id = IDS_MEDIA_GALLERIES_DIALOG_SUBTEXT_READ_ONLY;

  return l10n_util::GetStringFUTF16(id, UTF8ToUTF16(extension_->name()));
}

string16 MediaGalleriesDialogController::GetUnattachedLocationsHeader() const {
  return l10n_util::GetStringUTF16(IDS_MEDIA_GALLERIES_UNATTACHED_LOCATIONS);
}

// TODO(gbillock): Call this something a bit more connected to the
// messaging in the dialog.
bool MediaGalleriesDialogController::HasPermittedGalleries() const {
  for (KnownGalleryPermissions::const_iterator iter = known_galleries_.begin();
       iter != known_galleries_.end(); ++iter) {
    if (iter->second.allowed)
      return true;
  }

  // Do this? Views did.
  if (new_galleries_.size() > 0)
    return true;

  return false;
}

// Note: sorts by display criterion: GalleriesVectorComparator.
void MediaGalleriesDialogController::FillPermissions(
    bool attached,
    MediaGalleriesDialogController::GalleryPermissionsVector* permissions)
    const {
  for (KnownGalleryPermissions::const_iterator iter = known_galleries_.begin();
       iter != known_galleries_.end(); ++iter) {
    if ((attached && iter->second.pref_info.IsGalleryAvailable()) ||
        (!attached && !iter->second.pref_info.IsGalleryAvailable())) {
      permissions->push_back(iter->second);
    }
  }
  for (GalleryPermissionsVector::const_iterator iter = new_galleries_.begin();
       iter != new_galleries_.end(); ++iter) {
    if ((attached && iter->pref_info.IsGalleryAvailable()) ||
        (!attached && !iter->pref_info.IsGalleryAvailable())) {
      permissions->push_back(*iter);
    }
  }

  std::sort(permissions->begin(), permissions->end(),
            GalleriesVectorComparator);
}

MediaGalleriesDialogController::GalleryPermissionsVector
MediaGalleriesDialogController::AttachedPermissions() const {
  GalleryPermissionsVector attached;
  FillPermissions(true, &attached);
  return attached;
}

MediaGalleriesDialogController::GalleryPermissionsVector
MediaGalleriesDialogController::UnattachedPermissions() const {
  GalleryPermissionsVector unattached;
  FillPermissions(false, &unattached);
  return unattached;
}

void MediaGalleriesDialogController::OnAddFolderClicked() {
  base::FilePath user_data_dir;
  PathService::Get(chrome::DIR_USER_DATA, &user_data_dir);
  select_folder_dialog_ =
      ui::SelectFileDialog::Create(this, new ChromeSelectFilePolicy(NULL));
  select_folder_dialog_->SelectFile(
      ui::SelectFileDialog::SELECT_FOLDER,
      l10n_util::GetStringUTF16(IDS_MEDIA_GALLERIES_DIALOG_ADD_GALLERY_TITLE),
      user_data_dir,
      NULL,
      0,
      base::FilePath::StringType(),
      web_contents_->GetView()->GetTopLevelNativeWindow(),
      NULL);
}

void MediaGalleriesDialogController::DidToggleGalleryId(
    MediaGalleryPrefId gallery_id,
    bool enabled) {
  // Check known galleries.
  KnownGalleryPermissions::iterator iter =
      known_galleries_.find(gallery_id);
  if (iter != known_galleries_.end()) {
    if (iter->second.allowed == enabled)
      return;

    iter->second.allowed = enabled;
    if (ContainsKey(toggled_galleries_, gallery_id))
      toggled_galleries_.erase(gallery_id);
    else
      toggled_galleries_.insert(gallery_id);
    return;
  }

  // Check new galleries.
  for (GalleryPermissionsVector::iterator iter = new_galleries_.begin();
       iter != new_galleries_.end(); ++iter) {
    if (iter->pref_info.pref_id == gallery_id) {
      iter->allowed = enabled;
      return;
    }
  }

  // Don't sort -- the dialog is open, and we don't want to adjust any
  // positions for future updates to the dialog contents until they are
  // redrawn.
}

void MediaGalleriesDialogController::DialogFinished(bool accepted) {
  // The dialog has finished, so there is no need to watch for more updates
  // from |preferences_|. Do this here and not in the dtor since this is the
  // only non-test code path that deletes |this|. The test ctor never adds
  // this observer in the first place.
  preferences_->RemoveGalleryChangeObserver(this);

  if (accepted)
    SavePermissions();

  on_finish_.Run();
  delete this;
}

content::WebContents* MediaGalleriesDialogController::web_contents() {
  return web_contents_;
}

void MediaGalleriesDialogController::FileSelected(const base::FilePath& path,
                                                  int /*index*/,
                                                  void* /*params*/) {
  // Try to find it in the prefs.
  MediaGalleryPrefInfo gallery;
  bool gallery_exists = preferences_->LookUpGalleryByPath(path, &gallery);
  if (gallery_exists && gallery.type != MediaGalleryPrefInfo::kBlackListed) {
    // The prefs are in sync with |known_galleries_|, so it should exist in
    // |known_galleries_| as well. User selecting a known gallery effectively
    // just sets the gallery to permitted.
    KnownGalleryPermissions::const_iterator iter =
        known_galleries_.find(gallery.pref_id);
    DCHECK(iter != known_galleries_.end());
    dialog_->UpdateGallery(iter->second.pref_info, true);
    return;
  }

  // Try to find it in |new_galleries_| (user added same folder twice).
  for (GalleryPermissionsVector::iterator iter = new_galleries_.begin();
       iter != new_galleries_.end(); ++iter) {
    if (iter->pref_info.path == gallery.path &&
        iter->pref_info.device_id == gallery.device_id) {
      iter->allowed = true;
      dialog_->UpdateGallery(iter->pref_info, true);
      return;
    }
  }

  // Lastly, add a new gallery to |new_galleries_|.
  new_galleries_.push_back(GalleryPermission(gallery, true));
  dialog_->UpdateGallery(new_galleries_.back().pref_info, true);
}

void MediaGalleriesDialogController::OnRemovableStorageAttached(
    const StorageInfo& info) {
  UpdateGalleriesOnDeviceEvent(info.device_id());
}

void MediaGalleriesDialogController::OnRemovableStorageDetached(
    const StorageInfo& info) {
  UpdateGalleriesOnDeviceEvent(info.device_id());
}

void MediaGalleriesDialogController::OnGalleryChanged(
    MediaGalleriesPreferences* pref, const std::string& extension_id,
    MediaGalleryPrefId /* pref_id */, bool /* has_permission */) {
  DCHECK_EQ(preferences_, pref);
  if (extension_id.empty() || extension_id == extension_->id())
    UpdateGalleriesOnPreferencesEvent();
}

void MediaGalleriesDialogController::InitializePermissions() {
  const MediaGalleriesPrefInfoMap& galleries = preferences_->known_galleries();
  for (MediaGalleriesPrefInfoMap::const_iterator iter = galleries.begin();
       iter != galleries.end();
       ++iter) {
    const MediaGalleryPrefInfo& gallery = iter->second;
    if (gallery.type == MediaGalleryPrefInfo::kBlackListed)
      continue;

    known_galleries_[iter->first] = GalleryPermission(gallery, false);
  }

  MediaGalleryPrefIdSet permitted =
      preferences_->GalleriesForExtension(*extension_);

  for (MediaGalleryPrefIdSet::iterator iter = permitted.begin();
       iter != permitted.end(); ++iter) {
    if (ContainsKey(toggled_galleries_, *iter))
      continue;
    DCHECK(ContainsKey(known_galleries_, *iter));
    known_galleries_[*iter].allowed = true;
  }
}

void MediaGalleriesDialogController::SavePermissions() {
  for (KnownGalleryPermissions::const_iterator iter = known_galleries_.begin();
       iter != known_galleries_.end(); ++iter) {
    preferences_->SetGalleryPermissionForExtension(
        *extension_, iter->first, iter->second.allowed);
  }

  for (GalleryPermissionsVector::const_iterator iter = new_galleries_.begin();
       iter != new_galleries_.end(); ++iter) {
    // If the user added a gallery then unchecked it, forget about it.
    if (!iter->allowed)
      continue;

    // TODO(gbillock): Should be adding volume metadata during FileSelected.
    const MediaGalleryPrefInfo& gallery = iter->pref_info;
    MediaGalleryPrefId id = preferences_->AddGallery(
        gallery.device_id, gallery.path, true,
        gallery.volume_label, gallery.vendor_name, gallery.model_name,
        gallery.total_size_in_bytes, gallery.last_attach_time);
    preferences_->SetGalleryPermissionForExtension(*extension_, id, true);
  }
}

void MediaGalleriesDialogController::UpdateGalleriesOnPreferencesEvent() {
  // Merge in the permissions from |preferences_|. Afterwards,
  // |known_galleries_| may contain galleries that no longer belong there,
  // but the code below will put |known_galleries_| back in a consistent state.
  InitializePermissions();

  // If a gallery no longer belongs in |known_galleries_|, forget it in the
  // model/view.
  // If a gallery still belong in |known_galleries_|, check for a duplicate
  // entry in |new_galleries_|, merge its permission and remove it. Then update
  // the view.
  const MediaGalleriesPrefInfoMap& pref_galleries =
      preferences_->known_galleries();
  MediaGalleryPrefIdSet galleries_to_forget;
  for (KnownGalleryPermissions::iterator it = known_galleries_.begin();
       it != known_galleries_.end();
       ++it) {
    const MediaGalleryPrefId& gallery_id = it->first;
    GalleryPermission& gallery = it->second;
    MediaGalleriesPrefInfoMap::const_iterator pref_it =
        pref_galleries.find(gallery_id);
    // Check for lingering entry that should be removed.
    if (pref_it == pref_galleries.end() ||
        pref_it->second.type == MediaGalleryPrefInfo::kBlackListed) {
      galleries_to_forget.insert(gallery_id);
      dialog_->ForgetGallery(gallery.pref_info.pref_id);
      continue;
    }

    // Look for duplicate entries in |new_galleries_|.
    for (GalleryPermissionsVector::iterator new_it = new_galleries_.begin();
         new_it != new_galleries_.end();
         ++new_it) {
      if (new_it->pref_info.path == gallery.pref_info.path &&
          new_it->pref_info.device_id == gallery.pref_info.device_id) {
        // Found duplicate entry. Get the existing permission from it and then
        // remove it.
        gallery.allowed = new_it->allowed;
        dialog_->ForgetGallery(new_it->pref_info.pref_id);
        new_galleries_.erase(new_it);
        break;
      }
    }
    dialog_->UpdateGallery(gallery.pref_info, gallery.allowed);
  }

  // Remove the galleries to forget from |known_galleries_|. Doing it in the
  // above loop would invalidate the iterator there.
  for (MediaGalleryPrefIdSet::const_iterator it = galleries_to_forget.begin();
       it != galleries_to_forget.end();
       ++it) {
    known_galleries_.erase(*it);
  }
}

void MediaGalleriesDialogController::UpdateGalleriesOnDeviceEvent(
    const std::string& device_id) {
  for (KnownGalleryPermissions::iterator iter = known_galleries_.begin();
       iter != known_galleries_.end(); ++iter) {
    if (iter->second.pref_info.device_id == device_id)
      dialog_->UpdateGallery(iter->second.pref_info, iter->second.allowed);
  }

  for (GalleryPermissionsVector::iterator iter = new_galleries_.begin();
       iter != new_galleries_.end(); ++iter) {
    if (iter->pref_info.device_id == device_id)
      dialog_->UpdateGallery(iter->pref_info, iter->allowed);
  }
}

// MediaGalleries dialog -------------------------------------------------------

MediaGalleriesDialog::~MediaGalleriesDialog() {}

}  // namespace chrome
