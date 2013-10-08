// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/google_apis/drive_api_parser.h"

#include <algorithm>

#include "base/basictypes.h"
#include "base/files/file_path.h"
#include "base/json/json_value_converter.h"
#include "base/memory/scoped_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "chrome/browser/google_apis/gdata_wapi_parser.h"
#include "chrome/browser/google_apis/time_util.h"

using base::Value;
using base::DictionaryValue;
using base::ListValue;

namespace google_apis {

namespace {

bool CreateFileResourceFromValue(const base::Value* value,
                                 scoped_ptr<FileResource>* file) {
  *file = FileResource::CreateFrom(*value);
  return !!*file;
}

// Converts |url_string| to |result|.  Always returns true to be used
// for JSONValueConverter::RegisterCustomField method.
// TODO(mukai): make it return false in case of invalid |url_string|.
bool GetGURLFromString(const base::StringPiece& url_string, GURL* result) {
  *result = GURL(url_string.as_string());
  return true;
}

// Converts |value| to |result|. The key of |value| is app_id, and its value
// is URL to open the resource on the web app.
bool GetOpenWithLinksFromDictionaryValue(
    const base::Value* value,
    std::vector<FileResource::OpenWithLink>* result) {
  DCHECK(value);
  DCHECK(result);

  const base::DictionaryValue* dictionary_value;
  if (!value->GetAsDictionary(&dictionary_value))
    return false;

  result->reserve(dictionary_value->size());
  for (DictionaryValue::Iterator iter(*dictionary_value);
       !iter.IsAtEnd(); iter.Advance()) {
    std::string string_value;
    if (!iter.value().GetAsString(&string_value))
      return false;

    FileResource::OpenWithLink open_with_link;
    open_with_link.app_id = iter.key();
    open_with_link.open_url = GURL(string_value);
    result->push_back(open_with_link);
  }

  return true;
}

// Drive v2 API JSON names.

// Definition order follows the order of documentation in
// https://developers.google.com/drive/v2/reference/

// Common
const char kKind[] = "kind";
const char kId[] = "id";
const char kETag[] = "etag";
const char kSelfLink[] = "selfLink";
const char kItems[] = "items";
const char kLargestChangeId[] = "largestChangeId";

// About Resource
// https://developers.google.com/drive/v2/reference/about
const char kAboutKind[] = "drive#about";
const char kQuotaBytesTotal[] = "quotaBytesTotal";
const char kQuotaBytesUsed[] = "quotaBytesUsed";
const char kRootFolderId[] = "rootFolderId";

// App Icon
// https://developers.google.com/drive/v2/reference/apps
const char kCategory[] = "category";
const char kSize[] = "size";
const char kIconUrl[] = "iconUrl";

// Apps Resource
// https://developers.google.com/drive/v2/reference/apps
const char kAppKind[] = "drive#app";
const char kName[] = "name";
const char kObjectType[] = "objectType";
const char kSupportsCreate[] = "supportsCreate";
const char kSupportsImport[] = "supportsImport";
const char kInstalled[] = "installed";
const char kAuthorized[] = "authorized";
const char kProductUrl[] = "productUrl";
const char kPrimaryMimeTypes[] = "primaryMimeTypes";
const char kSecondaryMimeTypes[] = "secondaryMimeTypes";
const char kPrimaryFileExtensions[] = "primaryFileExtensions";
const char kSecondaryFileExtensions[] = "secondaryFileExtensions";
const char kIcons[] = "icons";

// Apps List
// https://developers.google.com/drive/v2/reference/apps/list
const char kAppListKind[] = "drive#appList";

// Parent Resource
// https://developers.google.com/drive/v2/reference/parents
const char kParentReferenceKind[] = "drive#parentReference";
const char kParentLink[] = "parentLink";
const char kIsRoot[] = "isRoot";

// File Resource
// https://developers.google.com/drive/v2/reference/files
const char kFileKind[] = "drive#file";
const char kTitle[] = "title";
const char kMimeType[] = "mimeType";
const char kCreatedDate[] = "createdDate";
const char kModifiedDate[] = "modifiedDate";
const char kModifiedByMeDate[] = "modifiedByMeDate";
const char kLastViewedByMeDate[] = "lastViewedByMeDate";
const char kSharedWithMeDate[] = "sharedWithMeDate";
const char kDownloadUrl[] = "downloadUrl";
const char kFileExtension[] = "fileExtension";
const char kMd5Checksum[] = "md5Checksum";
const char kFileSize[] = "fileSize";
const char kAlternateLink[] = "alternateLink";
const char kEmbedLink[] = "embedLink";
const char kParents[] = "parents";
const char kThumbnailLink[] = "thumbnailLink";
const char kWebContentLink[] = "webContentLink";
const char kOpenWithLinks[] = "openWithLinks";
const char kLabels[] = "labels";
// These 5 flags are defined under |labels|.
const char kLabelStarred[] = "starred";
const char kLabelHidden[] = "hidden";
const char kLabelTrashed[] = "trashed";
const char kLabelRestricted[] = "restricted";
const char kLabelViewed[] = "viewed";

const char kDriveFolderMimeType[] = "application/vnd.google-apps.folder";

// Files List
// https://developers.google.com/drive/v2/reference/files/list
const char kFileListKind[] = "drive#fileList";
const char kNextPageToken[] = "nextPageToken";
const char kNextLink[] = "nextLink";

// Change Resource
// https://developers.google.com/drive/v2/reference/changes
const char kChangeKind[] = "drive#change";
const char kFileId[] = "fileId";
const char kDeleted[] = "deleted";
const char kFile[] = "file";

// Changes List
// https://developers.google.com/drive/v2/reference/changes/list
const char kChangeListKind[] = "drive#changeList";

// Google Apps MIME types:
const char kGoogleDocumentMimeType[] = "application/vnd.google-apps.document";
const char kGoogleDrawingMimeType[] = "application/vnd.google-apps.drawing";
const char kGoogleFormMimeType[] = "application/vnd.google-apps.form";
const char kGooglePresentationMimeType[] =
    "application/vnd.google-apps.presentation";
const char kGoogleScriptMimeType[] = "application/vnd.google-apps.script";
const char kGoogleSiteMimeType[] = "application/vnd.google-apps.site";
const char kGoogleSpreadsheetMimeType[] =
    "application/vnd.google-apps.spreadsheet";
const char kGoogleTableMimeType[] = "application/vnd.google-apps.table";

// Maps category name to enum IconCategory.
struct AppIconCategoryMap {
  DriveAppIcon::IconCategory category;
  const char* category_name;
};

const AppIconCategoryMap kAppIconCategoryMap[] = {
  { DriveAppIcon::DOCUMENT, "document" },
  { DriveAppIcon::APPLICATION, "application" },
  { DriveAppIcon::SHARED_DOCUMENT, "documentShared" },
};

// Checks if the JSON is expected kind.  In Drive API, JSON data structure has
// |kind| property which denotes the type of the structure (e.g. "drive#file").
bool IsResourceKindExpected(const base::Value& value,
                            const std::string& expected_kind) {
  const base::DictionaryValue* as_dict = NULL;
  std::string kind;
  return value.GetAsDictionary(&as_dict) &&
      as_dict->HasKey(kKind) &&
      as_dict->GetString(kKind, &kind) &&
      kind == expected_kind;
}

ScopedVector<std::string> CopyScopedVectorString(
    const ScopedVector<std::string>& source) {
  ScopedVector<std::string> result;
  result.reserve(source.size());
  for (size_t i = 0; i < source.size(); ++i) {
    result.push_back(new std::string(*source[i]));
  }
  return result.Pass();
}

}  // namespace

////////////////////////////////////////////////////////////////////////////////
// AboutResource implementation

AboutResource::AboutResource()
    : largest_change_id_(0),
      quota_bytes_total_(0),
      quota_bytes_used_(0) {}

AboutResource::~AboutResource() {}

// static
scoped_ptr<AboutResource> AboutResource::CreateFrom(const base::Value& value) {
  scoped_ptr<AboutResource> resource(new AboutResource());
  if (!IsResourceKindExpected(value, kAboutKind) || !resource->Parse(value)) {
    LOG(ERROR) << "Unable to create: Invalid About resource JSON!";
    return scoped_ptr<AboutResource>();
  }
  return resource.Pass();
}

// static
scoped_ptr<AboutResource> AboutResource::CreateFromAccountMetadata(
    const AccountMetadata& account_metadata,
    const std::string& root_resource_id) {
  scoped_ptr<AboutResource> resource(new AboutResource);
  resource->set_largest_change_id(account_metadata.largest_changestamp());
  resource->set_quota_bytes_total(account_metadata.quota_bytes_total());
  resource->set_quota_bytes_used(account_metadata.quota_bytes_used());
  resource->set_root_folder_id(root_resource_id);
  return resource.Pass();
}

// static
void AboutResource::RegisterJSONConverter(
    base::JSONValueConverter<AboutResource>* converter) {
  converter->RegisterCustomField<int64>(kLargestChangeId,
                                        &AboutResource::largest_change_id_,
                                        &base::StringToInt64);
  converter->RegisterCustomField<int64>(kQuotaBytesTotal,
                                        &AboutResource::quota_bytes_total_,
                                        &base::StringToInt64);
  converter->RegisterCustomField<int64>(kQuotaBytesUsed,
                                        &AboutResource::quota_bytes_used_,
                                        &base::StringToInt64);
  converter->RegisterStringField(kRootFolderId,
                                 &AboutResource::root_folder_id_);
}

bool AboutResource::Parse(const base::Value& value) {
  base::JSONValueConverter<AboutResource> converter;
  if (!converter.Convert(value, this)) {
    LOG(ERROR) << "Unable to parse: Invalid About resource JSON!";
    return false;
  }
  return true;
}

////////////////////////////////////////////////////////////////////////////////
// DriveAppIcon implementation

DriveAppIcon::DriveAppIcon() : category_(UNKNOWN), icon_side_length_(0) {}

DriveAppIcon::~DriveAppIcon() {}

// static
void DriveAppIcon::RegisterJSONConverter(
    base::JSONValueConverter<DriveAppIcon>* converter) {
  converter->RegisterCustomField<IconCategory>(
      kCategory,
      &DriveAppIcon::category_,
      &DriveAppIcon::GetIconCategory);
  converter->RegisterIntField(kSize, &DriveAppIcon::icon_side_length_);
  converter->RegisterCustomField<GURL>(kIconUrl,
                                       &DriveAppIcon::icon_url_,
                                       GetGURLFromString);
}

// static
scoped_ptr<DriveAppIcon> DriveAppIcon::CreateFrom(const base::Value& value) {
  scoped_ptr<DriveAppIcon> resource(new DriveAppIcon());
  if (!resource->Parse(value)) {
    LOG(ERROR) << "Unable to create: Invalid DriveAppIcon JSON!";
    return scoped_ptr<DriveAppIcon>();
  }
  return resource.Pass();
}

// static
scoped_ptr<DriveAppIcon> DriveAppIcon::CreateFromAppIcon(
    const AppIcon& app_icon) {
  scoped_ptr<DriveAppIcon> resource(new DriveAppIcon);
  switch (app_icon.category()) {
    case AppIcon::ICON_UNKNOWN:
      resource->set_category(DriveAppIcon::UNKNOWN);
      break;
    case AppIcon::ICON_DOCUMENT:
      resource->set_category(DriveAppIcon::DOCUMENT);
      break;
    case AppIcon::ICON_APPLICATION:
      resource->set_category(DriveAppIcon::APPLICATION);
      break;
    case AppIcon::ICON_SHARED_DOCUMENT:
      resource->set_category(DriveAppIcon::SHARED_DOCUMENT);
      break;
    default:
      NOTREACHED();
  }

  resource->set_icon_side_length(app_icon.icon_side_length());
  resource->set_icon_url(app_icon.GetIconURL());
  return resource.Pass();
}

bool DriveAppIcon::Parse(const base::Value& value) {
  base::JSONValueConverter<DriveAppIcon> converter;
  if (!converter.Convert(value, this)) {
    LOG(ERROR) << "Unable to parse: Invalid DriveAppIcon";
    return false;
  }
  return true;
}

// static
bool DriveAppIcon::GetIconCategory(const base::StringPiece& category,
                                   DriveAppIcon::IconCategory* result) {
  for (size_t i = 0; i < arraysize(kAppIconCategoryMap); i++) {
    if (category == kAppIconCategoryMap[i].category_name) {
      *result = kAppIconCategoryMap[i].category;
      return true;
    }
  }
  DVLOG(1) << "Unknown icon category " << category;
  return false;
}

////////////////////////////////////////////////////////////////////////////////
// AppResource implementation

AppResource::AppResource()
    : supports_create_(false),
      supports_import_(false),
      installed_(false),
      authorized_(false) {
}

AppResource::~AppResource() {}

// static
void AppResource::RegisterJSONConverter(
    base::JSONValueConverter<AppResource>* converter) {
  converter->RegisterStringField(kId, &AppResource::application_id_);
  converter->RegisterStringField(kName, &AppResource::name_);
  converter->RegisterStringField(kObjectType, &AppResource::object_type_);
  converter->RegisterBoolField(kSupportsCreate, &AppResource::supports_create_);
  converter->RegisterBoolField(kSupportsImport, &AppResource::supports_import_);
  converter->RegisterBoolField(kInstalled, &AppResource::installed_);
  converter->RegisterBoolField(kAuthorized, &AppResource::authorized_);
  converter->RegisterCustomField<GURL>(kProductUrl,
                                       &AppResource::product_url_,
                                       GetGURLFromString);
  converter->RegisterRepeatedString(kPrimaryMimeTypes,
                                    &AppResource::primary_mimetypes_);
  converter->RegisterRepeatedString(kSecondaryMimeTypes,
                                    &AppResource::secondary_mimetypes_);
  converter->RegisterRepeatedString(kPrimaryFileExtensions,
                                    &AppResource::primary_file_extensions_);
  converter->RegisterRepeatedString(kSecondaryFileExtensions,
                                    &AppResource::secondary_file_extensions_);
  converter->RegisterRepeatedMessage(kIcons, &AppResource::icons_);
}

// static
scoped_ptr<AppResource> AppResource::CreateFrom(const base::Value& value) {
  scoped_ptr<AppResource> resource(new AppResource());
  if (!IsResourceKindExpected(value, kAppKind) || !resource->Parse(value)) {
    LOG(ERROR) << "Unable to create: Invalid AppResource JSON!";
    return scoped_ptr<AppResource>();
  }
  return resource.Pass();
}

// static
scoped_ptr<AppResource> AppResource::CreateFromInstalledApp(
    const InstalledApp& installed_app) {
  scoped_ptr<AppResource> resource(new AppResource);
  resource->set_application_id(installed_app.app_id());
  resource->set_name(installed_app.app_name());
  resource->set_object_type(installed_app.object_type());
  resource->set_supports_create(installed_app.supports_create());
  resource->set_product_url(installed_app.GetProductUrl());

  {
    ScopedVector<std::string> primary_mimetypes(
        CopyScopedVectorString(installed_app.primary_mimetypes()));
    resource->set_primary_mimetypes(&primary_mimetypes);
  }
  {
    ScopedVector<std::string> secondary_mimetypes(
        CopyScopedVectorString(installed_app.secondary_mimetypes()));
    resource->set_secondary_mimetypes(&secondary_mimetypes);
  }
  {
    ScopedVector<std::string> primary_file_extensions(
        CopyScopedVectorString(installed_app.primary_extensions()));
    resource->set_primary_file_extensions(&primary_file_extensions);
  }
  {
    ScopedVector<std::string> secondary_file_extensions(
        CopyScopedVectorString(installed_app.secondary_extensions()));
    resource->set_secondary_file_extensions(&secondary_file_extensions);
  }

  {
    const ScopedVector<AppIcon>& app_icons = installed_app.app_icons();
    ScopedVector<DriveAppIcon> icons;
    icons.reserve(app_icons.size());
    for (size_t i = 0; i < app_icons.size(); ++i) {
      icons.push_back(DriveAppIcon::CreateFromAppIcon(*app_icons[i]).release());
    }
    resource->set_icons(&icons);
  }

  // supports_import, installed and authorized are not supported in
  // InstalledApp.

  return resource.Pass();
}


bool AppResource::Parse(const base::Value& value) {
  base::JSONValueConverter<AppResource> converter;
  if (!converter.Convert(value, this)) {
    LOG(ERROR) << "Unable to parse: Invalid AppResource";
    return false;
  }
  return true;
}

////////////////////////////////////////////////////////////////////////////////
// AppList implementation

AppList::AppList() {}

AppList::~AppList() {}

// static
void AppList::RegisterJSONConverter(
    base::JSONValueConverter<AppList>* converter) {
  converter->RegisterStringField(kETag, &AppList::etag_);
  converter->RegisterRepeatedMessage<AppResource>(kItems,
                                                   &AppList::items_);
}

// static
scoped_ptr<AppList> AppList::CreateFrom(const base::Value& value) {
  scoped_ptr<AppList> resource(new AppList());
  if (!IsResourceKindExpected(value, kAppListKind) || !resource->Parse(value)) {
    LOG(ERROR) << "Unable to create: Invalid AppList JSON!";
    return scoped_ptr<AppList>();
  }
  return resource.Pass();
}

// static
scoped_ptr<AppList> AppList::CreateFromAccountMetadata(
    const AccountMetadata& account_metadata) {
  scoped_ptr<AppList> resource(new AppList);

  const ScopedVector<InstalledApp>& installed_apps =
      account_metadata.installed_apps();
  ScopedVector<AppResource> app_resources;
  app_resources.reserve(installed_apps.size());
  for (size_t i = 0; i < installed_apps.size(); ++i) {
    app_resources.push_back(
        AppResource::CreateFromInstalledApp(*installed_apps[i]).release());
  }
  resource->set_items(&app_resources);

  // etag is not supported in AccountMetadata.

  return resource.Pass();
}

bool AppList::Parse(const base::Value& value) {
  base::JSONValueConverter<AppList> converter;
  if (!converter.Convert(value, this)) {
    LOG(ERROR) << "Unable to parse: Invalid AppList";
    return false;
  }
  return true;
}

////////////////////////////////////////////////////////////////////////////////
// ParentReference implementation

ParentReference::ParentReference() : is_root_(false) {}

ParentReference::~ParentReference() {}

// static
void ParentReference::RegisterJSONConverter(
    base::JSONValueConverter<ParentReference>* converter) {
  converter->RegisterStringField(kId, &ParentReference::file_id_);
  converter->RegisterCustomField<GURL>(kParentLink,
                                       &ParentReference::parent_link_,
                                       GetGURLFromString);
  converter->RegisterBoolField(kIsRoot, &ParentReference::is_root_);
}

// static
scoped_ptr<ParentReference>
ParentReference::CreateFrom(const base::Value& value) {
  scoped_ptr<ParentReference> reference(new ParentReference());
  if (!IsResourceKindExpected(value, kParentReferenceKind) ||
      !reference->Parse(value)) {
    LOG(ERROR) << "Unable to create: Invalid ParentRefernce JSON!";
    return scoped_ptr<ParentReference>();
  }
  return reference.Pass();
}

bool ParentReference::Parse(const base::Value& value) {
  base::JSONValueConverter<ParentReference> converter;
  if (!converter.Convert(value, this)) {
    LOG(ERROR) << "Unable to parse: Invalid ParentReference";
    return false;
  }
  return true;
}

////////////////////////////////////////////////////////////////////////////////
// FileResource implementation

FileResource::FileResource() : file_size_(0) {}

FileResource::~FileResource() {}

// static
void FileResource::RegisterJSONConverter(
    base::JSONValueConverter<FileResource>* converter) {
  converter->RegisterStringField(kId, &FileResource::file_id_);
  converter->RegisterStringField(kETag, &FileResource::etag_);
  converter->RegisterCustomField<GURL>(kSelfLink,
                                       &FileResource::self_link_,
                                       GetGURLFromString);
  converter->RegisterStringField(kTitle, &FileResource::title_);
  converter->RegisterStringField(kMimeType, &FileResource::mime_type_);
  converter->RegisterNestedField(kLabels, &FileResource::labels_);
  converter->RegisterCustomField<base::Time>(
      kCreatedDate,
      &FileResource::created_date_,
      &util::GetTimeFromString);
  converter->RegisterCustomField<base::Time>(
      kModifiedDate,
      &FileResource::modified_date_,
      &util::GetTimeFromString);
  converter->RegisterCustomField<base::Time>(
      kModifiedByMeDate,
      &FileResource::modified_by_me_date_,
      &util::GetTimeFromString);
  converter->RegisterCustomField<base::Time>(
      kLastViewedByMeDate,
      &FileResource::last_viewed_by_me_date_,
      &util::GetTimeFromString);
  converter->RegisterCustomField<base::Time>(
      kSharedWithMeDate,
      &FileResource::shared_with_me_date_,
      &util::GetTimeFromString);
  converter->RegisterCustomField<GURL>(kDownloadUrl,
                                       &FileResource::download_url_,
                                       GetGURLFromString);
  converter->RegisterStringField(kFileExtension,
                                 &FileResource::file_extension_);
  converter->RegisterStringField(kMd5Checksum, &FileResource::md5_checksum_);
  converter->RegisterCustomField<int64>(kFileSize,
                                        &FileResource::file_size_,
                                        &base::StringToInt64);
  converter->RegisterCustomField<GURL>(kAlternateLink,
                                       &FileResource::alternate_link_,
                                       GetGURLFromString);
  converter->RegisterCustomField<GURL>(kEmbedLink,
                                       &FileResource::embed_link_,
                                       GetGURLFromString);
  converter->RegisterRepeatedMessage<ParentReference>(kParents,
                                                      &FileResource::parents_);
  converter->RegisterCustomField<GURL>(kThumbnailLink,
                                       &FileResource::thumbnail_link_,
                                       GetGURLFromString);
  converter->RegisterCustomField<GURL>(kWebContentLink,
                                       &FileResource::web_content_link_,
                                       GetGURLFromString);
  converter->RegisterCustomValueField<std::vector<OpenWithLink> >(
      kOpenWithLinks,
      &FileResource::open_with_links_,
      GetOpenWithLinksFromDictionaryValue);
}

// static
scoped_ptr<FileResource> FileResource::CreateFrom(const base::Value& value) {
  scoped_ptr<FileResource> resource(new FileResource());
  if (!IsResourceKindExpected(value, kFileKind) || !resource->Parse(value)) {
    LOG(ERROR) << "Unable to create: Invalid FileResource JSON!";
    return scoped_ptr<FileResource>();
  }
  return resource.Pass();
}

bool FileResource::IsDirectory() const {
  return mime_type_ == kDriveFolderMimeType;
}

DriveEntryKind FileResource::GetKind() const {
  if (mime_type() == kGoogleDocumentMimeType)
    return ENTRY_KIND_DOCUMENT;
  if (mime_type() == kGoogleSpreadsheetMimeType)
    return ENTRY_KIND_SPREADSHEET;
  if (mime_type() == kGooglePresentationMimeType)
    return ENTRY_KIND_PRESENTATION;
  if (mime_type() == kGoogleDrawingMimeType)
    return ENTRY_KIND_DRAWING;
  if (mime_type() == kGoogleTableMimeType)
    return ENTRY_KIND_TABLE;
  if (mime_type() == kDriveFolderMimeType)
    return ENTRY_KIND_FOLDER;
  if (mime_type() == "application/pdf")
    return ENTRY_KIND_PDF;
  return ENTRY_KIND_FILE;
}

bool FileResource::Parse(const base::Value& value) {
  base::JSONValueConverter<FileResource> converter;
  if (!converter.Convert(value, this)) {
    LOG(ERROR) << "Unable to parse: Invalid FileResource";
    return false;
  }
  return true;
}

////////////////////////////////////////////////////////////////////////////////
// FileList implementation

FileList::FileList() {}

FileList::~FileList() {}

// static
void FileList::RegisterJSONConverter(
    base::JSONValueConverter<FileList>* converter) {
  converter->RegisterStringField(kETag, &FileList::etag_);
  converter->RegisterStringField(kNextPageToken, &FileList::next_page_token_);
  converter->RegisterCustomField<GURL>(kNextLink,
                                       &FileList::next_link_,
                                       GetGURLFromString);
  converter->RegisterRepeatedMessage<FileResource>(kItems,
                                                   &FileList::items_);
}

// static
bool FileList::HasFileListKind(const base::Value& value) {
  return IsResourceKindExpected(value, kFileListKind);
}

// static
scoped_ptr<FileList> FileList::CreateFrom(const base::Value& value) {
  scoped_ptr<FileList> resource(new FileList());
  if (!HasFileListKind(value) || !resource->Parse(value)) {
    LOG(ERROR) << "Unable to create: Invalid FileList JSON!";
    return scoped_ptr<FileList>();
  }
  return resource.Pass();
}

bool FileList::Parse(const base::Value& value) {
  base::JSONValueConverter<FileList> converter;
  if (!converter.Convert(value, this)) {
    LOG(ERROR) << "Unable to parse: Invalid FileList";
    return false;
  }
  return true;
}

////////////////////////////////////////////////////////////////////////////////
// ChangeResource implementation

ChangeResource::ChangeResource() : change_id_(0), deleted_(false) {}

ChangeResource::~ChangeResource() {}

// static
void ChangeResource::RegisterJSONConverter(
    base::JSONValueConverter<ChangeResource>* converter) {
  converter->RegisterCustomField<int64>(kId,
                                        &ChangeResource::change_id_,
                                        &base::StringToInt64);
  converter->RegisterStringField(kFileId, &ChangeResource::file_id_);
  converter->RegisterBoolField(kDeleted, &ChangeResource::deleted_);
  converter->RegisterCustomValueField(kFile, &ChangeResource::file_,
                                      &CreateFileResourceFromValue);
}

// static
scoped_ptr<ChangeResource>
ChangeResource::CreateFrom(const base::Value& value) {
  scoped_ptr<ChangeResource> resource(new ChangeResource());
  if (!IsResourceKindExpected(value, kChangeKind) || !resource->Parse(value)) {
    LOG(ERROR) << "Unable to create: Invalid ChangeResource JSON!";
    return scoped_ptr<ChangeResource>();
  }
  return resource.Pass();
}

bool ChangeResource::Parse(const base::Value& value) {
  base::JSONValueConverter<ChangeResource> converter;
  if (!converter.Convert(value, this)) {
    LOG(ERROR) << "Unable to parse: Invalid ChangeResource";
    return false;
  }
  return true;
}

////////////////////////////////////////////////////////////////////////////////
// ChangeList implementation

ChangeList::ChangeList() : largest_change_id_(0) {}

ChangeList::~ChangeList() {}

// static
void ChangeList::RegisterJSONConverter(
    base::JSONValueConverter<ChangeList>* converter) {
  converter->RegisterStringField(kETag, &ChangeList::etag_);
  converter->RegisterStringField(kNextPageToken, &ChangeList::next_page_token_);
  converter->RegisterCustomField<GURL>(kNextLink,
                                       &ChangeList::next_link_,
                                       GetGURLFromString);
  converter->RegisterCustomField<int64>(kLargestChangeId,
                                        &ChangeList::largest_change_id_,
                                        &base::StringToInt64);
  converter->RegisterRepeatedMessage<ChangeResource>(kItems,
                                                     &ChangeList::items_);
}

// static
bool ChangeList::HasChangeListKind(const base::Value& value) {
  return IsResourceKindExpected(value, kChangeListKind);
}

// static
scoped_ptr<ChangeList> ChangeList::CreateFrom(const base::Value& value) {
  scoped_ptr<ChangeList> resource(new ChangeList());
  if (!HasChangeListKind(value) || !resource->Parse(value)) {
    LOG(ERROR) << "Unable to create: Invalid ChangeList JSON!";
    return scoped_ptr<ChangeList>();
  }
  return resource.Pass();
}

bool ChangeList::Parse(const base::Value& value) {
  base::JSONValueConverter<ChangeList> converter;
  if (!converter.Convert(value, this)) {
    LOG(ERROR) << "Unable to parse: Invalid ChangeList";
    return false;
  }
  return true;
}


////////////////////////////////////////////////////////////////////////////////
// FileLabels implementation

FileLabels::FileLabels()
    : starred_(false),
      hidden_(false),
      trashed_(false),
      restricted_(false),
      viewed_(false) {}

FileLabels::~FileLabels() {}

// static
void FileLabels::RegisterJSONConverter(
    base::JSONValueConverter<FileLabels>* converter) {
  converter->RegisterBoolField(kLabelStarred, &FileLabels::starred_);
  converter->RegisterBoolField(kLabelHidden, &FileLabels::hidden_);
  converter->RegisterBoolField(kLabelTrashed, &FileLabels::trashed_);
  converter->RegisterBoolField(kLabelRestricted, &FileLabels::restricted_);
  converter->RegisterBoolField(kLabelViewed, &FileLabels::viewed_);
}

// static
scoped_ptr<FileLabels> FileLabels::CreateFrom(const base::Value& value) {
  scoped_ptr<FileLabels> resource(new FileLabels());
  if (!resource->Parse(value)) {
    LOG(ERROR) << "Unable to create: Invalid FileLabels JSON!";
    return scoped_ptr<FileLabels>();
  }
  return resource.Pass();
}

bool FileLabels::Parse(const base::Value& value) {
  base::JSONValueConverter<FileLabels> converter;
  if (!converter.Convert(value, this)) {
    LOG(ERROR) << "Unable to parse: Invalid FileLabels";
    return false;
  }
  return true;
}

}  // namespace google_apis
