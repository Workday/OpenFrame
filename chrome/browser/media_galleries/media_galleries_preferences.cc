// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media_galleries/media_galleries_preferences.h"

#include "base/i18n/time_formatting.h"
#include "base/path_service.h"
#include "base/prefs/pref_service.h"
#include "base/stl_util.h"
#include "base/strings/string16.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/extensions/api/media_galleries_private/media_galleries_private_api.h"
#include "chrome/browser/extensions/extension_prefs.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/media_galleries/fileapi/itunes_finder.h"
#include "chrome/browser/media_galleries/fileapi/picasa/picasa_finder.h"
#include "chrome/browser/media_galleries/media_file_system_registry.h"
#include "chrome/browser/prefs/scoped_user_pref_update.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/storage_monitor/media_storage_util.h"
#include "chrome/browser/storage_monitor/storage_monitor.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/permissions/api_permission.h"
#include "chrome/common/extensions/permissions/media_galleries_permission.h"
#include "chrome/common/extensions/permissions/permissions_data.h"
#include "chrome/common/pref_names.h"
#include "components/user_prefs/pref_registry_syncable.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/text/bytes_formatting.h"

using base::DictionaryValue;
using base::ListValue;
using extensions::ExtensionPrefs;

namespace chrome {

namespace {

// Pref key for the list of media gallery permissions.
const char kMediaGalleriesPermissions[] = "media_galleries_permissions";
// Pref key for Media Gallery ID.
const char kMediaGalleryIdKey[] = "id";
// Pref key for Media Gallery Permission Value.
const char kMediaGalleryHasPermissionKey[] = "has_permission";

const char kMediaGalleriesDeviceIdKey[] = "deviceId";
const char kMediaGalleriesDisplayNameKey[] = "displayName";
const char kMediaGalleriesPathKey[] = "path";
const char kMediaGalleriesPrefIdKey[] = "prefId";
const char kMediaGalleriesTypeKey[] = "type";
const char kMediaGalleriesVolumeLabelKey[] = "volumeLabel";
const char kMediaGalleriesVendorNameKey[] = "vendorName";
const char kMediaGalleriesModelNameKey[] = "modelName";
const char kMediaGalleriesSizeKey[] = "totalSize";
const char kMediaGalleriesLastAttachTimeKey[] = "lastAttachTime";
const char kMediaGalleriesPrefsVersionKey[] = "preferencesVersion";

const char kMediaGalleriesTypeAutoDetectedValue[] = "autoDetected";
const char kMediaGalleriesTypeUserAddedValue[] = "userAdded";
const char kMediaGalleriesTypeBlackListedValue[] = "blackListed";

const char kITunesGalleryName[] = "iTunes";
const char kPicasaGalleryName[] = "Picasa";

bool GetPrefId(const DictionaryValue& dict, MediaGalleryPrefId* value) {
  std::string string_id;
  if (!dict.GetString(kMediaGalleriesPrefIdKey, &string_id) ||
      !base::StringToUint64(string_id, value)) {
    return false;
  }

  return true;
}

bool GetType(const DictionaryValue& dict, MediaGalleryPrefInfo::Type* type) {
  std::string string_type;
  if (!dict.GetString(kMediaGalleriesTypeKey, &string_type))
    return false;

  if (string_type == kMediaGalleriesTypeAutoDetectedValue) {
    *type = MediaGalleryPrefInfo::kAutoDetected;
    return true;
  }
  if (string_type == kMediaGalleriesTypeUserAddedValue) {
    *type = MediaGalleryPrefInfo::kUserAdded;
    return true;
  }
  if (string_type == kMediaGalleriesTypeBlackListedValue) {
    *type = MediaGalleryPrefInfo::kBlackListed;
    return true;
  }

  return false;
}

bool PopulateGalleryPrefInfoFromDictionary(
    const DictionaryValue& dict, MediaGalleryPrefInfo* out_gallery_info) {
  MediaGalleryPrefId pref_id;
  string16 display_name;
  std::string device_id;
  base::FilePath::StringType path;
  MediaGalleryPrefInfo::Type type = MediaGalleryPrefInfo::kAutoDetected;
  string16 volume_label;
  string16 vendor_name;
  string16 model_name;
  double total_size_in_bytes = 0.0;
  double last_attach_time = 0.0;
  bool volume_metadata_valid = false;
  int prefs_version = 0;

  if (!GetPrefId(dict, &pref_id) ||
      !dict.GetString(kMediaGalleriesDeviceIdKey, &device_id) ||
      !dict.GetString(kMediaGalleriesPathKey, &path) ||
      !GetType(dict, &type)) {
    return false;
  }

  dict.GetString(kMediaGalleriesDisplayNameKey, &display_name);
  dict.GetInteger(kMediaGalleriesPrefsVersionKey, &prefs_version);

  if (dict.GetString(kMediaGalleriesVolumeLabelKey, &volume_label) &&
      dict.GetString(kMediaGalleriesVendorNameKey, &vendor_name) &&
      dict.GetString(kMediaGalleriesModelNameKey, &model_name) &&
      dict.GetDouble(kMediaGalleriesSizeKey, &total_size_in_bytes) &&
      dict.GetDouble(kMediaGalleriesLastAttachTimeKey, &last_attach_time)) {
    volume_metadata_valid = true;
  }

  out_gallery_info->pref_id = pref_id;
  out_gallery_info->display_name = display_name;
  out_gallery_info->device_id = device_id;
  out_gallery_info->path = base::FilePath(path);
  out_gallery_info->type = type;
  out_gallery_info->volume_label = volume_label;
  out_gallery_info->vendor_name = vendor_name;
  out_gallery_info->model_name = model_name;
  out_gallery_info->total_size_in_bytes = total_size_in_bytes;
  out_gallery_info->last_attach_time =
      base::Time::FromInternalValue(last_attach_time);
  out_gallery_info->volume_metadata_valid = volume_metadata_valid;
  out_gallery_info->prefs_version = prefs_version;

  return true;
}

DictionaryValue* CreateGalleryPrefInfoDictionary(
    const MediaGalleryPrefInfo& gallery) {
  DictionaryValue* dict = new DictionaryValue();
  dict->SetString(kMediaGalleriesPrefIdKey,
                  base::Uint64ToString(gallery.pref_id));
  if (!gallery.volume_metadata_valid)
    dict->SetString(kMediaGalleriesDisplayNameKey, gallery.display_name);
  dict->SetString(kMediaGalleriesDeviceIdKey, gallery.device_id);
  dict->SetString(kMediaGalleriesPathKey, gallery.path.value());

  const char* type = NULL;
  switch (gallery.type) {
    case MediaGalleryPrefInfo::kAutoDetected:
      type = kMediaGalleriesTypeAutoDetectedValue;
      break;
    case MediaGalleryPrefInfo::kUserAdded:
      type = kMediaGalleriesTypeUserAddedValue;
      break;
    case MediaGalleryPrefInfo::kBlackListed:
      type = kMediaGalleriesTypeBlackListedValue;
      break;
    default:
      NOTREACHED();
      break;
  }
  dict->SetString(kMediaGalleriesTypeKey, type);

  if (gallery.volume_metadata_valid) {
    dict->SetString(kMediaGalleriesVolumeLabelKey, gallery.volume_label);
    dict->SetString(kMediaGalleriesVendorNameKey, gallery.vendor_name);
    dict->SetString(kMediaGalleriesModelNameKey, gallery.model_name);
    dict->SetDouble(kMediaGalleriesSizeKey, gallery.total_size_in_bytes);
    dict->SetDouble(kMediaGalleriesLastAttachTimeKey,
                    gallery.last_attach_time.ToInternalValue());
  }

  // Version 0 of the prefs format was that the display_name was always
  // used to show the user-visible name of the gallery. Version 1 means
  // that there is an optional display_name, and when it is present, it
  // overrides the name that would be built from the volume metadata, path,
  // or whatever other data. So if we see a display_name with version 0, it
  // means it may be overwritten simply by getting new volume metadata.
  // A display_name with version 1 should not be overwritten.
  dict->SetInteger(kMediaGalleriesPrefsVersionKey, gallery.prefs_version);

  return dict;
}

bool HasAutoDetectedGalleryPermission(const extensions::Extension& extension) {
  extensions::MediaGalleriesPermission::CheckParam param(
      extensions::MediaGalleriesPermission::kAllAutoDetectedPermission);
  return extensions::PermissionsData::CheckAPIPermissionWithParam(
      &extension, extensions::APIPermission::kMediaGalleries, &param);
}

// Retrieves the MediaGalleryPermission from the given dictionary; DCHECKs on
// failure.
bool GetMediaGalleryPermissionFromDictionary(
    const DictionaryValue* dict,
    MediaGalleryPermission* out_permission) {
  std::string string_id;
  if (dict->GetString(kMediaGalleryIdKey, &string_id) &&
      base::StringToUint64(string_id, &out_permission->pref_id) &&
      dict->GetBoolean(kMediaGalleryHasPermissionKey,
                       &out_permission->has_permission)) {
    return true;
  }
  NOTREACHED();
  return false;
}

string16 GetDisplayNameForDevice(uint64 storage_size_in_bytes,
                                 const string16& name) {
  DCHECK(!name.empty());
  return (storage_size_in_bytes == 0) ?
      name : ui::FormatBytes(storage_size_in_bytes) + ASCIIToUTF16(" ") + name;
}

// For a device with |device_name| and a relative path |sub_folder|, construct
// a display name. If |sub_folder| is empty, then just return |device_name|.
string16 GetDisplayNameForSubFolder(const string16& device_name,
                                    const base::FilePath& sub_folder) {
  if (sub_folder.empty())
    return device_name;
  return (sub_folder.BaseName().LossyDisplayName() +
          ASCIIToUTF16(" - ") +
          device_name);
}

string16 GetFullProductName(const string16& vendor_name,
                            const string16& model_name) {
  if (vendor_name.empty() && model_name.empty())
    return string16();

  string16 product_name;
  if (vendor_name.empty())
    product_name = model_name;
  else if (model_name.empty())
    product_name = vendor_name;
  else if (!vendor_name.empty() && !model_name.empty())
    product_name = vendor_name + UTF8ToUTF16(", ") + model_name;

  return product_name;
}

}  // namespace

MediaGalleryPrefInfo::MediaGalleryPrefInfo()
    : pref_id(kInvalidMediaGalleryPrefId),
      type(kInvalidType),
      total_size_in_bytes(0),
      volume_metadata_valid(false),
      prefs_version(0) {
}

MediaGalleryPrefInfo::~MediaGalleryPrefInfo() {}

base::FilePath MediaGalleryPrefInfo::AbsolutePath() const {
  base::FilePath base_path = MediaStorageUtil::FindDevicePathById(device_id);
  DCHECK(!path.IsAbsolute());
  return base_path.empty() ? base_path : base_path.Append(path);
}

string16 MediaGalleryPrefInfo::GetGalleryDisplayName() const {
  if (!StorageInfo::IsRemovableDevice(device_id)) {
    // For fixed storage, the name is the directory name, or, in the case
    // of a root directory, the root directory name.
    // TODO(gbillock): Using only the BaseName can lead to ambiguity. The
    // tooltip resolves it. Is that enough?
    base::FilePath path = AbsolutePath();
    if (!display_name.empty())
      return display_name;
    if (path == path.DirName())
      return path.LossyDisplayName();
    return path.BaseName().LossyDisplayName();
  }

  string16 name = display_name;
  if (name.empty())
    name = volume_label;
  if (name.empty())
    name = GetFullProductName(vendor_name, model_name);
  if (name.empty())
    name = l10n_util::GetStringUTF16(IDS_MEDIA_GALLERIES_UNLABELED_DEVICE);

  name = GetDisplayNameForDevice(total_size_in_bytes, name);

  if (!path.empty())
    name = GetDisplayNameForSubFolder(name, path);

  return name;
}

string16 MediaGalleryPrefInfo::GetGalleryTooltip() const {
  return AbsolutePath().LossyDisplayName();
}

string16 MediaGalleryPrefInfo::GetGalleryAdditionalDetails() const {
  string16 attached;
  if (StorageInfo::IsRemovableDevice(device_id)) {
    if (MediaStorageUtil::IsRemovableStorageAttached(device_id)) {
      attached = l10n_util::GetStringUTF16(
          IDS_MEDIA_GALLERIES_DIALOG_DEVICE_ATTACHED);
    } else if (!last_attach_time.is_null()) {
      attached = l10n_util::GetStringFUTF16(
          IDS_MEDIA_GALLERIES_LAST_ATTACHED,
          base::TimeFormatShortDateNumeric(last_attach_time));
    } else {
      attached = l10n_util::GetStringUTF16(
          IDS_MEDIA_GALLERIES_DIALOG_DEVICE_NOT_ATTACHED);
    }
  }

  return attached;
}

bool MediaGalleryPrefInfo::IsGalleryAvailable() const {
  return !StorageInfo::IsRemovableDevice(device_id) ||
         MediaStorageUtil::IsRemovableStorageAttached(device_id);
}

MediaGalleriesPreferences::GalleryChangeObserver::~GalleryChangeObserver() {}

MediaGalleriesPreferences::MediaGalleriesPreferences(Profile* profile)
    : weak_factory_(this),
      profile_(profile),
      extension_prefs_for_testing_(NULL) {
  AddDefaultGalleriesIfFreshProfile();

  // Look for optional default galleries every time.
  itunes::ITunesFinder::FindITunesLibrary(
      base::Bind(&MediaGalleriesPreferences::OnITunesDeviceID,
                 weak_factory_.GetWeakPtr()));

  // TODO(tommycli): Turn on when Picasa code is ready.
#if 0
  picasa::PicasaFinder::FindPicasaDatabaseOnUIThread(
      base::Bind(&MediaGalleriesPreferences::OnPicasaDeviceID,
                 weak_factory_.GetWeakPtr()));
#endif

  InitFromPrefs(false /*no notification*/);

  StorageMonitor::GetInstance()->AddObserver(this);
}

MediaGalleriesPreferences::~MediaGalleriesPreferences() {
  if (StorageMonitor::GetInstance())
    StorageMonitor::GetInstance()->RemoveObserver(this);
}

void MediaGalleriesPreferences::AddDefaultGalleriesIfFreshProfile() {
  // Only add defaults the first time.
  if (APIHasBeenUsed(profile_))
    return;

  // Fresh profile case.
  const int kDirectoryKeys[] = {
    DIR_USER_MUSIC,
    DIR_USER_PICTURES,
    DIR_USER_VIDEOS,
  };

  for (size_t i = 0; i < arraysize(kDirectoryKeys); ++i) {
    base::FilePath path;
    if (!PathService::Get(kDirectoryKeys[i], &path))
      continue;

    base::FilePath relative_path;
    StorageInfo info;
    if (MediaStorageUtil::GetDeviceInfoFromPath(path, &info, &relative_path)) {
      AddGalleryInternal(info.device_id(), info.name(), relative_path, false,
                         info.storage_label(), info.vendor_name(),
                         info.model_name(), info.total_size_in_bytes(),
                         base::Time(), true, 2);
    }
  }
}

bool MediaGalleriesPreferences::UpdateDeviceIDForSingletonType(
    const std::string& device_id) {
  StorageInfo::Type singleton_type;
  if (!StorageInfo::CrackDeviceId(device_id, &singleton_type, NULL))
    return false;

  PrefService* prefs = profile_->GetPrefs();
  scoped_ptr<ListPrefUpdate> update(new ListPrefUpdate(
      prefs, prefs::kMediaGalleriesRememberedGalleries));
  ListValue* list = update->Get();
  for (ListValue::iterator iter = list->begin(); iter != list->end(); ++iter) {
    // All of these calls should succeed, but preferences file can be corrupt.
    DictionaryValue* dict;
    if (!(*iter)->GetAsDictionary(&dict))
      continue;
    std::string this_device_id;
    if (!dict->GetString(kMediaGalleriesDeviceIdKey, &this_device_id))
      continue;
    if (this_device_id == device_id)
      return true;  // No update is necessary.
    StorageInfo::Type device_type;
    if (!StorageInfo::CrackDeviceId(this_device_id, &device_type, NULL))
      continue;

    if (device_type == singleton_type) {
      dict->SetString(kMediaGalleriesDeviceIdKey, device_id);
      update.reset();  // commits the update.
      InitFromPrefs(true /* notify observers */);
      return true;
    }
  }
  return false;
}

void MediaGalleriesPreferences::OnITunesDeviceID(const std::string& device_id) {
  if (device_id.empty())
    return;
  if (!UpdateDeviceIDForSingletonType(device_id)) {
    AddGalleryInternal(device_id, ASCIIToUTF16(kITunesGalleryName),
                       base::FilePath(), false /*not user added*/,
                       string16(), string16(), string16(), 0,
                       base::Time(), false, 2);
  }
}

void MediaGalleriesPreferences::OnPicasaDeviceID(const std::string& device_id) {
  DCHECK(!device_id.empty());
  if (!UpdateDeviceIDForSingletonType(device_id)) {
    AddGalleryInternal(device_id, ASCIIToUTF16(kPicasaGalleryName),
                       base::FilePath(), false /*not user added*/,
                       string16(), string16(), string16(), 0,
                       base::Time(), false, 2);
  }
}

void MediaGalleriesPreferences::InitFromPrefs(bool notify_observers) {
  known_galleries_.clear();
  device_map_.clear();

  PrefService* prefs = profile_->GetPrefs();
  const ListValue* list = prefs->GetList(
      prefs::kMediaGalleriesRememberedGalleries);
  if (list) {
    for (ListValue::const_iterator it = list->begin();
         it != list->end(); ++it) {
      const DictionaryValue* dict = NULL;
      if (!(*it)->GetAsDictionary(&dict))
        continue;

      MediaGalleryPrefInfo gallery_info;
      if (!PopulateGalleryPrefInfoFromDictionary(*dict, &gallery_info))
        continue;

      known_galleries_[gallery_info.pref_id] = gallery_info;
      device_map_[gallery_info.device_id].insert(gallery_info.pref_id);
    }
  }
  if (notify_observers)
    NotifyChangeObservers(std::string(), kInvalidMediaGalleryPrefId, false);
}

void MediaGalleriesPreferences::NotifyChangeObservers(
    const std::string& extension_id,
    MediaGalleryPrefId pref_id,
    bool has_permission) {
  FOR_EACH_OBSERVER(GalleryChangeObserver,
                    gallery_change_observers_,
                    OnGalleryChanged(this, extension_id, pref_id,
                                     has_permission));
}

void MediaGalleriesPreferences::AddGalleryChangeObserver(
    GalleryChangeObserver* observer) {
  gallery_change_observers_.AddObserver(observer);
}

void MediaGalleriesPreferences::RemoveGalleryChangeObserver(
    GalleryChangeObserver* observer) {
  gallery_change_observers_.RemoveObserver(observer);
}

void MediaGalleriesPreferences::OnRemovableStorageAttached(
    const StorageInfo& info) {
  if (!StorageInfo::IsMediaDevice(info.device_id()))
    return;

  AddGallery(info.device_id(), base::FilePath(),
             false /*not user added*/,
             info.storage_label(),
             info.vendor_name(),
             info.model_name(),
             info.total_size_in_bytes(),
             base::Time::Now());
}

bool MediaGalleriesPreferences::LookUpGalleryByPath(
    const base::FilePath& path,
    MediaGalleryPrefInfo* gallery_info) const {
  StorageInfo info;
  base::FilePath relative_path;
  if (!MediaStorageUtil::GetDeviceInfoFromPath(path, &info, &relative_path)) {
    if (gallery_info)
      *gallery_info = MediaGalleryPrefInfo();
    return false;
  }

  relative_path = relative_path.NormalizePathSeparators();
  MediaGalleryPrefIdSet galleries_on_device =
      LookUpGalleriesByDeviceId(info.device_id());
  for (MediaGalleryPrefIdSet::const_iterator it = galleries_on_device.begin();
       it != galleries_on_device.end();
       ++it) {
    const MediaGalleryPrefInfo& gallery = known_galleries_.find(*it)->second;
    if (gallery.path != relative_path)
      continue;

    if (gallery_info)
      *gallery_info = gallery;
    return true;
  }

  // This method is called by controller::FilesSelected when the user
  // adds a new gallery. Control reaches here when the selected gallery is
  // on a volume we know about, but have no gallery already for. Returns
  // hypothetical data to the caller about what the prefs will look like
  // if the gallery is added.
  // TODO(gbillock): split this out into another function so it doesn't
  // conflate LookUp.
  if (gallery_info) {
    gallery_info->pref_id = kInvalidMediaGalleryPrefId;
    gallery_info->device_id = info.device_id();
    gallery_info->path = relative_path;
    gallery_info->type = MediaGalleryPrefInfo::kUserAdded;
    gallery_info->volume_label = info.storage_label();
    gallery_info->vendor_name = info.vendor_name();
    gallery_info->model_name = info.model_name();
    gallery_info->total_size_in_bytes = info.total_size_in_bytes();
    gallery_info->last_attach_time = base::Time::Now();
    gallery_info->volume_metadata_valid = true;
    gallery_info->prefs_version = 2;
  }
  return false;
}

MediaGalleryPrefIdSet MediaGalleriesPreferences::LookUpGalleriesByDeviceId(
    const std::string& device_id) const {
  DeviceIdPrefIdsMap::const_iterator found = device_map_.find(device_id);
  if (found == device_map_.end())
    return MediaGalleryPrefIdSet();
  return found->second;
}

base::FilePath MediaGalleriesPreferences::LookUpGalleryPathForExtension(
    MediaGalleryPrefId gallery_id,
    const extensions::Extension* extension,
    bool include_unpermitted_galleries) {
  DCHECK(extension);
  if (!include_unpermitted_galleries &&
      !ContainsKey(GalleriesForExtension(*extension), gallery_id))
    return base::FilePath();

  MediaGalleriesPrefInfoMap::const_iterator it =
      known_galleries_.find(gallery_id);
  if (it == known_galleries_.end())
    return base::FilePath();
  return MediaStorageUtil::FindDevicePathById(it->second.device_id);
}

MediaGalleryPrefId MediaGalleriesPreferences::AddGallery(
    const std::string& device_id,
    const base::FilePath& relative_path, bool user_added,
    const string16& volume_label, const string16& vendor_name,
    const string16& model_name, uint64 total_size_in_bytes,
    base::Time last_attach_time) {
  return AddGalleryInternal(device_id, string16(), relative_path, user_added,
                            volume_label, vendor_name, model_name,
                            total_size_in_bytes, last_attach_time, true, 2);
}

MediaGalleryPrefId MediaGalleriesPreferences::AddGalleryInternal(
    const std::string& device_id, const string16& display_name,
    const base::FilePath& relative_path, bool user_added,
    const string16& volume_label, const string16& vendor_name,
    const string16& model_name, uint64 total_size_in_bytes,
    base::Time last_attach_time,
    bool volume_metadata_valid,
    int prefs_version) {
  base::FilePath normalized_relative_path =
      relative_path.NormalizePathSeparators();
  MediaGalleryPrefIdSet galleries_on_device =
    LookUpGalleriesByDeviceId(device_id);
  for (MediaGalleryPrefIdSet::const_iterator it = galleries_on_device.begin();
       it != galleries_on_device.end();
       ++it) {
    const MediaGalleryPrefInfo& existing = known_galleries_.find(*it)->second;
    if (existing.path != normalized_relative_path)
      continue;

    bool update_gallery_type =
        user_added && (existing.type == MediaGalleryPrefInfo::kBlackListed);
    // Status quo: In M27 and M28, galleries added manually use version 0,
    // and galleries added automatically (including default galleries) use
    // version 1. The name override is used by default galleries as well
    // as all device attach events.
    // We want to upgrade the name if the existing version is < 2. Leave it
    // alone if the existing display name is set with version == 2 and the
    // proposed new name is empty.
    bool update_gallery_name = existing.display_name != display_name;
    if (existing.prefs_version == 2 && !existing.display_name.empty() &&
        display_name.empty()) {
      update_gallery_name = false;
    }
    bool update_gallery_metadata = volume_metadata_valid &&
        ((existing.volume_label != volume_label) ||
         (existing.vendor_name != vendor_name) ||
         (existing.model_name != model_name) ||
         (existing.total_size_in_bytes != total_size_in_bytes) ||
         (existing.last_attach_time != last_attach_time));

    if (!update_gallery_name && !update_gallery_type &&
        !update_gallery_metadata)
      return *it;

    PrefService* prefs = profile_->GetPrefs();
    scoped_ptr<ListPrefUpdate> update(
        new ListPrefUpdate(prefs, prefs::kMediaGalleriesRememberedGalleries));
    ListValue* list = update->Get();

    for (ListValue::const_iterator list_iter = list->begin();
         list_iter != list->end();
         ++list_iter) {
      DictionaryValue* dict;
      MediaGalleryPrefId iter_id;
      if ((*list_iter)->GetAsDictionary(&dict) &&
          GetPrefId(*dict, &iter_id) &&
          *it == iter_id) {
        if (update_gallery_type) {
          dict->SetString(kMediaGalleriesTypeKey,
                          kMediaGalleriesTypeAutoDetectedValue);
        }
        if (update_gallery_name)
          dict->SetString(kMediaGalleriesDisplayNameKey, display_name);
        if (update_gallery_metadata) {
          dict->SetString(kMediaGalleriesVolumeLabelKey, volume_label);
          dict->SetString(kMediaGalleriesVendorNameKey, vendor_name);
          dict->SetString(kMediaGalleriesModelNameKey, model_name);
          dict->SetDouble(kMediaGalleriesSizeKey, total_size_in_bytes);
          dict->SetDouble(kMediaGalleriesLastAttachTimeKey,
                          last_attach_time.ToInternalValue());
        }
        dict->SetInteger(kMediaGalleriesPrefsVersionKey, prefs_version);
        break;
      }
    }

    // Commits the prefs update.
    update.reset();

    if (update_gallery_name || update_gallery_metadata || update_gallery_type)
      InitFromPrefs(true /* notify observers */);
    return *it;
  }

  PrefService* prefs = profile_->GetPrefs();

  MediaGalleryPrefInfo gallery_info;
  gallery_info.pref_id = prefs->GetUint64(prefs::kMediaGalleriesUniqueId);
  prefs->SetUint64(prefs::kMediaGalleriesUniqueId, gallery_info.pref_id + 1);
  gallery_info.display_name = display_name;
  gallery_info.device_id = device_id;
  gallery_info.path = normalized_relative_path;
  gallery_info.type = MediaGalleryPrefInfo::kAutoDetected;
  if (user_added)
    gallery_info.type = MediaGalleryPrefInfo::kUserAdded;
  if (volume_metadata_valid) {
    gallery_info.volume_label = volume_label;
    gallery_info.vendor_name = vendor_name;
    gallery_info.model_name = model_name;
    gallery_info.total_size_in_bytes = total_size_in_bytes;
    gallery_info.last_attach_time = last_attach_time;
  }
  gallery_info.volume_metadata_valid = volume_metadata_valid;
  gallery_info.prefs_version = prefs_version;

  {
    ListPrefUpdate update(prefs, prefs::kMediaGalleriesRememberedGalleries);
    ListValue* list = update.Get();
    list->Append(CreateGalleryPrefInfoDictionary(gallery_info));
  }
  InitFromPrefs(true /* notify observers */);

  return gallery_info.pref_id;
}

MediaGalleryPrefId MediaGalleriesPreferences::AddGalleryByPath(
    const base::FilePath& path) {
  MediaGalleryPrefInfo gallery_info;
  if (LookUpGalleryByPath(path, &gallery_info) &&
      gallery_info.type != MediaGalleryPrefInfo::kBlackListed) {
    return gallery_info.pref_id;
  }
  return AddGalleryInternal(gallery_info.device_id,
                            gallery_info.display_name,
                            gallery_info.path,
                            true /*user added*/,
                            gallery_info.volume_label,
                            gallery_info.vendor_name,
                            gallery_info.model_name,
                            gallery_info.total_size_in_bytes,
                            gallery_info.last_attach_time,
                            gallery_info.volume_metadata_valid,
                            gallery_info.prefs_version);
}

void MediaGalleriesPreferences::ForgetGalleryById(MediaGalleryPrefId pref_id) {
  PrefService* prefs = profile_->GetPrefs();
  scoped_ptr<ListPrefUpdate> update(new ListPrefUpdate(
      prefs, prefs::kMediaGalleriesRememberedGalleries));
  ListValue* list = update->Get();

  if (!ContainsKey(known_galleries_, pref_id))
    return;

  for (ListValue::iterator iter = list->begin(); iter != list->end(); ++iter) {
    DictionaryValue* dict;
    MediaGalleryPrefId iter_id;
    if ((*iter)->GetAsDictionary(&dict) && GetPrefId(*dict, &iter_id) &&
        pref_id == iter_id) {
      RemoveGalleryPermissionsFromPrefs(pref_id);
      MediaGalleryPrefInfo::Type type;
      if (GetType(*dict, &type) &&
          type == MediaGalleryPrefInfo::kAutoDetected) {
        dict->SetString(kMediaGalleriesTypeKey,
                        kMediaGalleriesTypeBlackListedValue);
      } else {
        list->Erase(iter, NULL);
      }
      update.reset(NULL);  // commits the update.

      InitFromPrefs(true /* notify observers */);
      return;
    }
  }
}

MediaGalleryPrefIdSet MediaGalleriesPreferences::GalleriesForExtension(
    const extensions::Extension& extension) const {
  MediaGalleryPrefIdSet result;

  if (HasAutoDetectedGalleryPermission(extension)) {
    for (MediaGalleriesPrefInfoMap::const_iterator it =
             known_galleries_.begin(); it != known_galleries_.end(); ++it) {
      if (it->second.type == MediaGalleryPrefInfo::kAutoDetected)
        result.insert(it->second.pref_id);
    }
  }

  std::vector<MediaGalleryPermission> stored_permissions =
      GetGalleryPermissionsFromPrefs(extension.id());
  for (std::vector<MediaGalleryPermission>::const_iterator it =
           stored_permissions.begin(); it != stored_permissions.end(); ++it) {
    if (!it->has_permission) {
      result.erase(it->pref_id);
    } else {
      MediaGalleriesPrefInfoMap::const_iterator gallery =
          known_galleries_.find(it->pref_id);
      DCHECK(gallery != known_galleries_.end());
      if (gallery->second.type != MediaGalleryPrefInfo::kBlackListed) {
        result.insert(it->pref_id);
      } else {
        NOTREACHED() << gallery->second.device_id;
      }
    }
  }
  return result;
}

void MediaGalleriesPreferences::SetGalleryPermissionForExtension(
    const extensions::Extension& extension,
    MediaGalleryPrefId pref_id,
    bool has_permission) {
  // The gallery may not exist anymore if the user opened a second config
  // surface concurrently and removed it. Drop the permission update if so.
  MediaGalleriesPrefInfoMap::const_iterator gallery_info =
      known_galleries_.find(pref_id);
  if (gallery_info == known_galleries_.end())
    return;

  bool all_permission = HasAutoDetectedGalleryPermission(extension);
  if (has_permission && all_permission) {
    if (gallery_info->second.type == MediaGalleryPrefInfo::kAutoDetected) {
      UnsetGalleryPermissionInPrefs(extension.id(), pref_id);
      NotifyChangeObservers(extension.id(), pref_id, true);
      return;
    }
  }

  if (!has_permission && !all_permission) {
    UnsetGalleryPermissionInPrefs(extension.id(), pref_id);
  } else {
    SetGalleryPermissionInPrefs(extension.id(), pref_id, has_permission);
  }
  NotifyChangeObservers(extension.id(), pref_id, has_permission);
}

void MediaGalleriesPreferences::Shutdown() {
  weak_factory_.InvalidateWeakPtrs();
  profile_ = NULL;
}

// static
bool MediaGalleriesPreferences::APIHasBeenUsed(Profile* profile) {
  MediaGalleryPrefId current_id =
      profile->GetPrefs()->GetUint64(prefs::kMediaGalleriesUniqueId);
  return current_id != kInvalidMediaGalleryPrefId + 1;
}

// static
void MediaGalleriesPreferences::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterListPref(prefs::kMediaGalleriesRememberedGalleries,
                             user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
  registry->RegisterUint64Pref(
      prefs::kMediaGalleriesUniqueId,
      kInvalidMediaGalleryPrefId + 1,
      user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
}

void MediaGalleriesPreferences::SetGalleryPermissionInPrefs(
    const std::string& extension_id,
    MediaGalleryPrefId gallery_id,
    bool has_access) {
  ExtensionPrefs::ScopedListUpdate update(GetExtensionPrefs(),
                                          extension_id,
                                          kMediaGalleriesPermissions);
  ListValue* permissions = update.Get();
  if (!permissions) {
    permissions = update.Create();
  } else {
    // If the gallery is already in the list, update the permission...
    for (ListValue::iterator iter = permissions->begin();
         iter != permissions->end(); ++iter) {
      DictionaryValue* dict = NULL;
      if (!(*iter)->GetAsDictionary(&dict))
        continue;
      MediaGalleryPermission perm;
      if (!GetMediaGalleryPermissionFromDictionary(dict, &perm))
        continue;
      if (perm.pref_id == gallery_id) {
        dict->SetBoolean(kMediaGalleryHasPermissionKey, has_access);
        return;
      }
    }
  }
  // ...Otherwise, add a new entry for the gallery.
  DictionaryValue* dict = new DictionaryValue;
  dict->SetString(kMediaGalleryIdKey, base::Uint64ToString(gallery_id));
  dict->SetBoolean(kMediaGalleryHasPermissionKey, has_access);
  permissions->Append(dict);
}

void MediaGalleriesPreferences::UnsetGalleryPermissionInPrefs(
    const std::string& extension_id,
    MediaGalleryPrefId gallery_id) {
  ExtensionPrefs::ScopedListUpdate update(GetExtensionPrefs(),
                                          extension_id,
                                          kMediaGalleriesPermissions);
  ListValue* permissions = update.Get();
  if (!permissions)
    return;

  for (ListValue::iterator iter = permissions->begin();
       iter != permissions->end(); ++iter) {
    const DictionaryValue* dict = NULL;
    if (!(*iter)->GetAsDictionary(&dict))
      continue;
    MediaGalleryPermission perm;
    if (!GetMediaGalleryPermissionFromDictionary(dict, &perm))
      continue;
    if (perm.pref_id == gallery_id) {
      permissions->Erase(iter, NULL);
      return;
    }
  }
}

std::vector<MediaGalleryPermission>
MediaGalleriesPreferences::GetGalleryPermissionsFromPrefs(
    const std::string& extension_id) const {
  std::vector<MediaGalleryPermission> result;
  const ListValue* permissions;
  if (!GetExtensionPrefs()->ReadPrefAsList(extension_id,
                                           kMediaGalleriesPermissions,
                                           &permissions)) {
    return result;
  }

  for (ListValue::const_iterator iter = permissions->begin();
       iter != permissions->end(); ++iter) {
    DictionaryValue* dict = NULL;
    if (!(*iter)->GetAsDictionary(&dict))
      continue;
    MediaGalleryPermission perm;
    if (!GetMediaGalleryPermissionFromDictionary(dict, &perm))
      continue;
    result.push_back(perm);
  }

  return result;
}

void MediaGalleriesPreferences::RemoveGalleryPermissionsFromPrefs(
    MediaGalleryPrefId gallery_id) {
  ExtensionPrefs* prefs = GetExtensionPrefs();
  const DictionaryValue* extensions =
      prefs->pref_service()->GetDictionary(ExtensionPrefs::kExtensionsPref);
  if (!extensions)
    return;

  for (DictionaryValue::Iterator iter(*extensions); !iter.IsAtEnd();
       iter.Advance()) {
    if (!extensions::Extension::IdIsValid(iter.key())) {
      NOTREACHED();
      continue;
    }
    UnsetGalleryPermissionInPrefs(iter.key(), gallery_id);
  }
}

ExtensionPrefs* MediaGalleriesPreferences::GetExtensionPrefs() const {
  if (extension_prefs_for_testing_)
    return extension_prefs_for_testing_;
  return extensions::ExtensionPrefs::Get(profile_);
}

void MediaGalleriesPreferences::SetExtensionPrefsForTesting(
    extensions::ExtensionPrefs* extension_prefs) {
  extension_prefs_for_testing_ = extension_prefs;
}

}  // namespace chrome
