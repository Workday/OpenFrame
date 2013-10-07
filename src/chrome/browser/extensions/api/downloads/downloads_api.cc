// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/downloads/downloads_api.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <set>
#include <string>

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/callback.h"
#include "base/file_util.h"
#include "base/files/file_path.h"
#include "base/json/json_writer.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/memory/weak_ptr.h"
#include "base/metrics/histogram.h"
#include "base/stl_util.h"
#include "base/strings/string16.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/download/download_danger_prompt.h"
#include "chrome/browser/download/download_file_icon_extractor.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/download/download_query.h"
#include "chrome/browser/download/download_service.h"
#include "chrome/browser/download/download_service_factory.h"
#include "chrome/browser/download/download_shelf.h"
#include "chrome/browser/download/download_stats.h"
#include "chrome/browser/download/download_util.h"
#include "chrome/browser/extensions/event_names.h"
#include "chrome/browser/extensions/event_router.h"
#include "chrome/browser/extensions/extension_function_dispatcher.h"
#include "chrome/browser/extensions/extension_info_map.h"
#include "chrome/browser/extensions/extension_prefs.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/extensions/extension_warning_service.h"
#include "chrome/browser/extensions/extension_warning_set.h"
#include "chrome/browser/icon_loader.h"
#include "chrome/browser/icon_manager.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/renderer_host/chrome_render_message_filter.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/common/cancelable_task_tracker.h"
#include "chrome/common/extensions/api/downloads.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/permissions/permissions_data.h"
#include "content/public/browser/download_interrupt_reasons.h"
#include "content/public/browser/download_item.h"
#include "content/public/browser/download_save_info.h"
#include "content/public/browser/download_url_parameters.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/resource_context.h"
#include "content/public/browser/resource_dispatcher_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "net/base/load_flags.h"
#include "net/base/net_util.h"
#include "net/http/http_util.h"
#include "net/url_request/url_request.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/webui/web_ui_util.h"

namespace events = extensions::event_names;

using content::BrowserContext;
using content::BrowserThread;
using content::DownloadItem;
using content::DownloadManager;

namespace download_extension_errors {

const char kEmptyFile[] = "Filename not yet determined";
const char kFileAlreadyDeleted[] = "Download file already deleted";
const char kIconNotFound[] = "Icon not found";
const char kInvalidDangerType[] = "Invalid danger type";
const char kInvalidFilename[] = "Invalid filename";
const char kInvalidFilter[] = "Invalid query filter";
const char kInvalidHeader[] = "Invalid request header";
const char kInvalidId[] = "Invalid downloadId";
const char kInvalidOrderBy[] = "Invalid orderBy field";
const char kInvalidQueryLimit[] = "Invalid query limit";
const char kInvalidState[] = "Invalid state";
const char kInvalidURL[] = "Invalid URL";
const char kInvisibleContext[] = "Javascript execution context is not visible "
  "(tab, window, popup bubble)";
const char kNotComplete[] = "Download must be complete";
const char kNotDangerous[] = "Download must be dangerous";
const char kNotInProgress[] = "Download must be in progress";
const char kNotResumable[] = "DownloadItem.canResume must be true";
const char kOpenPermission[] = "The \"downloads.open\" permission is required";
const char kShelfDisabled[] = "Another extension has disabled the shelf";
const char kShelfPermission[] = "downloads.setShelfEnabled requires the "
  "\"downloads.shelf\" permission";
const char kTooManyListeners[] = "Each extension may have at most one "
  "onDeterminingFilename listener between all of its renderer execution "
  "contexts.";
const char kUnexpectedDeterminer[] = "Unexpected determineFilename call";

}  // namespace download_extension_errors

namespace errors = download_extension_errors;

namespace {

// Default icon size for getFileIcon() in pixels.
const int  kDefaultIconSize = 32;

// Parameter keys
const char kByExtensionIdKey[] = "byExtensionId";
const char kByExtensionNameKey[] = "byExtensionName";
const char kBytesReceivedKey[] = "bytesReceived";
const char kCanResumeKey[] = "canResume";
const char kDangerAccepted[] = "accepted";
const char kDangerContent[] = "content";
const char kDangerFile[] = "file";
const char kDangerHost[] = "host";
const char kDangerKey[] = "danger";
const char kDangerSafe[] = "safe";
const char kDangerUncommon[] = "uncommon";
const char kDangerUnwanted[] = "unwanted";
const char kDangerUrl[] = "url";
const char kEndTimeKey[] = "endTime";
const char kEndedAfterKey[] = "endedAfter";
const char kEndedBeforeKey[] = "endedBefore";
const char kErrorKey[] = "error";
const char kEstimatedEndTimeKey[] = "estimatedEndTime";
const char kExistsKey[] = "exists";
const char kFileSizeKey[] = "fileSize";
const char kFilenameKey[] = "filename";
const char kFilenameRegexKey[] = "filenameRegex";
const char kIdKey[] = "id";
const char kIncognitoKey[] = "incognito";
const char kMimeKey[] = "mime";
const char kPausedKey[] = "paused";
const char kQueryKey[] = "query";
const char kReferrerUrlKey[] = "referrer";
const char kStartTimeKey[] = "startTime";
const char kStartedAfterKey[] = "startedAfter";
const char kStartedBeforeKey[] = "startedBefore";
const char kStateComplete[] = "complete";
const char kStateInProgress[] = "in_progress";
const char kStateInterrupted[] = "interrupted";
const char kStateKey[] = "state";
const char kTotalBytesGreaterKey[] = "totalBytesGreater";
const char kTotalBytesKey[] = "totalBytes";
const char kTotalBytesLessKey[] = "totalBytesLess";
const char kUrlKey[] = "url";
const char kUrlRegexKey[] = "urlRegex";

// Note: Any change to the danger type strings, should be accompanied by a
// corresponding change to downloads.json.
const char* kDangerStrings[] = {
  kDangerSafe,
  kDangerFile,
  kDangerUrl,
  kDangerContent,
  kDangerSafe,
  kDangerUncommon,
  kDangerAccepted,
  kDangerHost,
  kDangerUnwanted
};
COMPILE_ASSERT(arraysize(kDangerStrings) == content::DOWNLOAD_DANGER_TYPE_MAX,
               download_danger_type_enum_changed);

// Note: Any change to the state strings, should be accompanied by a
// corresponding change to downloads.json.
const char* kStateStrings[] = {
  kStateInProgress,
  kStateComplete,
  kStateInterrupted,
  kStateInterrupted,
};
COMPILE_ASSERT(arraysize(kStateStrings) == DownloadItem::MAX_DOWNLOAD_STATE,
               download_item_state_enum_changed);

const char* DangerString(content::DownloadDangerType danger) {
  DCHECK(danger >= 0);
  DCHECK(danger < static_cast<content::DownloadDangerType>(
      arraysize(kDangerStrings)));
  if (danger < 0 || danger >= static_cast<content::DownloadDangerType>(
      arraysize(kDangerStrings)))
    return "";
  return kDangerStrings[danger];
}

content::DownloadDangerType DangerEnumFromString(const std::string& danger) {
  for (size_t i = 0; i < arraysize(kDangerStrings); ++i) {
    if (danger == kDangerStrings[i])
      return static_cast<content::DownloadDangerType>(i);
  }
  return content::DOWNLOAD_DANGER_TYPE_MAX;
}

const char* StateString(DownloadItem::DownloadState state) {
  DCHECK(state >= 0);
  DCHECK(state < static_cast<DownloadItem::DownloadState>(
      arraysize(kStateStrings)));
  if (state < 0 || state >= static_cast<DownloadItem::DownloadState>(
      arraysize(kStateStrings)))
    return "";
  return kStateStrings[state];
}

DownloadItem::DownloadState StateEnumFromString(const std::string& state) {
  for (size_t i = 0; i < arraysize(kStateStrings); ++i) {
    if ((kStateStrings[i] != NULL) && (state == kStateStrings[i]))
      return static_cast<DownloadItem::DownloadState>(i);
  }
  return DownloadItem::MAX_DOWNLOAD_STATE;
}

std::string TimeToISO8601(const base::Time& t) {
  base::Time::Exploded exploded;
  t.UTCExplode(&exploded);
  return base::StringPrintf(
      "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", exploded.year, exploded.month,
      exploded.day_of_month, exploded.hour, exploded.minute, exploded.second,
      exploded.millisecond);
}

scoped_ptr<base::DictionaryValue> DownloadItemToJSON(
    DownloadItem* download_item,
    Profile* profile) {
  base::DictionaryValue* json = new base::DictionaryValue();
  json->SetBoolean(kExistsKey, !download_item->GetFileExternallyRemoved());
  json->SetInteger(kIdKey, download_item->GetId());
  const GURL& url = download_item->GetOriginalUrl();
  json->SetString(kUrlKey, (url.is_valid() ? url.spec() : std::string()));
  const GURL& referrer = download_item->GetReferrerUrl();
  json->SetString(kReferrerUrlKey, (referrer.is_valid() ? referrer.spec()
                                                        : std::string()));
  json->SetString(kFilenameKey,
                  download_item->GetTargetFilePath().LossyDisplayName());
  json->SetString(kDangerKey, DangerString(download_item->GetDangerType()));
  json->SetString(kStateKey, StateString(download_item->GetState()));
  json->SetBoolean(kCanResumeKey, download_item->CanResume());
  json->SetBoolean(kPausedKey, download_item->IsPaused());
  json->SetString(kMimeKey, download_item->GetMimeType());
  json->SetString(kStartTimeKey, TimeToISO8601(download_item->GetStartTime()));
  json->SetInteger(kBytesReceivedKey, download_item->GetReceivedBytes());
  json->SetInteger(kTotalBytesKey, download_item->GetTotalBytes());
  json->SetBoolean(kIncognitoKey, profile->IsOffTheRecord());
  if (download_item->GetState() == DownloadItem::INTERRUPTED) {
    json->SetString(kErrorKey, content::InterruptReasonDebugString(
        download_item->GetLastReason()));
  } else if (download_item->GetState() == DownloadItem::CANCELLED) {
    json->SetString(kErrorKey, content::InterruptReasonDebugString(
        content::DOWNLOAD_INTERRUPT_REASON_USER_CANCELED));
  }
  if (!download_item->GetEndTime().is_null())
    json->SetString(kEndTimeKey, TimeToISO8601(download_item->GetEndTime()));
  base::TimeDelta time_remaining;
  if (download_item->TimeRemaining(&time_remaining)) {
    base::Time now = base::Time::Now();
    json->SetString(kEstimatedEndTimeKey, TimeToISO8601(now + time_remaining));
  }
  DownloadedByExtension* by_ext = DownloadedByExtension::Get(download_item);
  if (by_ext) {
    json->SetString(kByExtensionIdKey, by_ext->id());
    json->SetString(kByExtensionNameKey, by_ext->name());
    // Lookup the extension's current name() in case the user changed their
    // language. This won't work if the extension was uninstalled, so the name
    // might be the wrong language.
    bool include_disabled = true;
    const extensions::Extension* extension = extensions::ExtensionSystem::Get(
        profile)->extension_service()->GetExtensionById(
            by_ext->id(), include_disabled);
    if (extension)
      json->SetString(kByExtensionNameKey, extension->name());
  }
  // TODO(benjhayden): Implement fileSize.
  json->SetInteger(kFileSizeKey, download_item->GetTotalBytes());
  return scoped_ptr<base::DictionaryValue>(json);
}

class DownloadFileIconExtractorImpl : public DownloadFileIconExtractor {
 public:
  DownloadFileIconExtractorImpl() {}

  virtual ~DownloadFileIconExtractorImpl() {}

  virtual bool ExtractIconURLForPath(const base::FilePath& path,
                                     IconLoader::IconSize icon_size,
                                     IconURLCallback callback) OVERRIDE;
 private:
  void OnIconLoadComplete(gfx::Image* icon);

  CancelableTaskTracker cancelable_task_tracker_;
  IconURLCallback callback_;
};

bool DownloadFileIconExtractorImpl::ExtractIconURLForPath(
    const base::FilePath& path,
    IconLoader::IconSize icon_size,
    IconURLCallback callback) {
  callback_ = callback;
  IconManager* im = g_browser_process->icon_manager();
  // The contents of the file at |path| may have changed since a previous
  // request, in which case the associated icon may also have changed.
  // Therefore, always call LoadIcon instead of attempting a LookupIcon.
  im->LoadIcon(path,
               icon_size,
               base::Bind(&DownloadFileIconExtractorImpl::OnIconLoadComplete,
                          base::Unretained(this)),
               &cancelable_task_tracker_);
  return true;
}

void DownloadFileIconExtractorImpl::OnIconLoadComplete(gfx::Image* icon) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  std::string url;
  if (icon)
    url = webui::GetBitmapDataUrl(icon->AsBitmap());
  callback_.Run(url);
}

IconLoader::IconSize IconLoaderSizeFromPixelSize(int pixel_size) {
  switch (pixel_size) {
    case 16: return IconLoader::SMALL;
    case 32: return IconLoader::NORMAL;
    default:
      NOTREACHED();
      return IconLoader::NORMAL;
  }
}

typedef base::hash_map<std::string, DownloadQuery::FilterType> FilterTypeMap;

void InitFilterTypeMap(FilterTypeMap& filter_types) {
  filter_types[kBytesReceivedKey] = DownloadQuery::FILTER_BYTES_RECEIVED;
  filter_types[kExistsKey] = DownloadQuery::FILTER_EXISTS;
  filter_types[kFilenameKey] = DownloadQuery::FILTER_FILENAME;
  filter_types[kFilenameRegexKey] = DownloadQuery::FILTER_FILENAME_REGEX;
  filter_types[kMimeKey] = DownloadQuery::FILTER_MIME;
  filter_types[kPausedKey] = DownloadQuery::FILTER_PAUSED;
  filter_types[kQueryKey] = DownloadQuery::FILTER_QUERY;
  filter_types[kEndedAfterKey] = DownloadQuery::FILTER_ENDED_AFTER;
  filter_types[kEndedBeforeKey] = DownloadQuery::FILTER_ENDED_BEFORE;
  filter_types[kEndTimeKey] = DownloadQuery::FILTER_END_TIME;
  filter_types[kStartedAfterKey] = DownloadQuery::FILTER_STARTED_AFTER;
  filter_types[kStartedBeforeKey] = DownloadQuery::FILTER_STARTED_BEFORE;
  filter_types[kStartTimeKey] = DownloadQuery::FILTER_START_TIME;
  filter_types[kTotalBytesKey] = DownloadQuery::FILTER_TOTAL_BYTES;
  filter_types[kTotalBytesGreaterKey] =
    DownloadQuery::FILTER_TOTAL_BYTES_GREATER;
  filter_types[kTotalBytesLessKey] = DownloadQuery::FILTER_TOTAL_BYTES_LESS;
  filter_types[kUrlKey] = DownloadQuery::FILTER_URL;
  filter_types[kUrlRegexKey] = DownloadQuery::FILTER_URL_REGEX;
}

typedef base::hash_map<std::string, DownloadQuery::SortType> SortTypeMap;

void InitSortTypeMap(SortTypeMap& sorter_types) {
  sorter_types[kBytesReceivedKey] = DownloadQuery::SORT_BYTES_RECEIVED;
  sorter_types[kDangerKey] = DownloadQuery::SORT_DANGER;
  sorter_types[kEndTimeKey] = DownloadQuery::SORT_END_TIME;
  sorter_types[kExistsKey] = DownloadQuery::SORT_EXISTS;
  sorter_types[kFilenameKey] = DownloadQuery::SORT_FILENAME;
  sorter_types[kMimeKey] = DownloadQuery::SORT_MIME;
  sorter_types[kPausedKey] = DownloadQuery::SORT_PAUSED;
  sorter_types[kStartTimeKey] = DownloadQuery::SORT_START_TIME;
  sorter_types[kStateKey] = DownloadQuery::SORT_STATE;
  sorter_types[kTotalBytesKey] = DownloadQuery::SORT_TOTAL_BYTES;
  sorter_types[kUrlKey] = DownloadQuery::SORT_URL;
}

bool IsNotTemporaryDownloadFilter(const DownloadItem& download_item) {
  return !download_item.IsTemporary();
}

// Set |manager| to the on-record DownloadManager, and |incognito_manager| to
// the off-record DownloadManager if one exists and is requested via
// |include_incognito|. This should work regardless of whether |profile| is
// original or incognito.
void GetManagers(
    Profile* profile,
    bool include_incognito,
    DownloadManager** manager,
    DownloadManager** incognito_manager) {
  *manager = BrowserContext::GetDownloadManager(profile->GetOriginalProfile());
  if (profile->HasOffTheRecordProfile() &&
      (include_incognito ||
       profile->IsOffTheRecord())) {
    *incognito_manager = BrowserContext::GetDownloadManager(
        profile->GetOffTheRecordProfile());
  } else {
    *incognito_manager = NULL;
  }
}

DownloadItem* GetDownload(Profile* profile, bool include_incognito, int id) {
  DownloadManager* manager = NULL;
  DownloadManager* incognito_manager = NULL;
  GetManagers(profile, include_incognito, &manager, &incognito_manager);
  DownloadItem* download_item = manager->GetDownload(id);
  if (!download_item && incognito_manager)
    download_item = incognito_manager->GetDownload(id);
  return download_item;
}

enum DownloadsFunctionName {
  DOWNLOADS_FUNCTION_DOWNLOAD = 0,
  DOWNLOADS_FUNCTION_SEARCH = 1,
  DOWNLOADS_FUNCTION_PAUSE = 2,
  DOWNLOADS_FUNCTION_RESUME = 3,
  DOWNLOADS_FUNCTION_CANCEL = 4,
  DOWNLOADS_FUNCTION_ERASE = 5,
  // 6 unused
  DOWNLOADS_FUNCTION_ACCEPT_DANGER = 7,
  DOWNLOADS_FUNCTION_SHOW = 8,
  DOWNLOADS_FUNCTION_DRAG = 9,
  DOWNLOADS_FUNCTION_GET_FILE_ICON = 10,
  DOWNLOADS_FUNCTION_OPEN = 11,
  DOWNLOADS_FUNCTION_REMOVE_FILE = 12,
  DOWNLOADS_FUNCTION_SHOW_DEFAULT_FOLDER = 13,
  DOWNLOADS_FUNCTION_SET_SHELF_ENABLED = 14,
  // Insert new values here, not at the beginning.
  DOWNLOADS_FUNCTION_LAST
};

void RecordApiFunctions(DownloadsFunctionName function) {
  UMA_HISTOGRAM_ENUMERATION("Download.ApiFunctions",
                            function,
                            DOWNLOADS_FUNCTION_LAST);
}

void CompileDownloadQueryOrderBy(
    const std::vector<std::string>& order_by_strs,
    std::string* error,
    DownloadQuery* query) {
  // TODO(benjhayden): Consider switching from LazyInstance to explicit string
  // comparisons.
  static base::LazyInstance<SortTypeMap> sorter_types =
    LAZY_INSTANCE_INITIALIZER;
  if (sorter_types.Get().size() == 0)
    InitSortTypeMap(sorter_types.Get());

  for (std::vector<std::string>::const_iterator iter = order_by_strs.begin();
       iter != order_by_strs.end(); ++iter) {
    std::string term_str = *iter;
    if (term_str.empty())
      continue;
    DownloadQuery::SortDirection direction = DownloadQuery::ASCENDING;
    if (term_str[0] == '-') {
      direction = DownloadQuery::DESCENDING;
      term_str = term_str.substr(1);
    }
    SortTypeMap::const_iterator sorter_type =
        sorter_types.Get().find(term_str);
    if (sorter_type == sorter_types.Get().end()) {
      *error = errors::kInvalidOrderBy;
      return;
    }
    query->AddSorter(sorter_type->second, direction);
  }
}

void RunDownloadQuery(
    const extensions::api::downloads::DownloadQuery& query_in,
    DownloadManager* manager,
    DownloadManager* incognito_manager,
    std::string* error,
    DownloadQuery::DownloadVector* results) {
  // TODO(benjhayden): Consider switching from LazyInstance to explicit string
  // comparisons.
  static base::LazyInstance<FilterTypeMap> filter_types =
    LAZY_INSTANCE_INITIALIZER;
  if (filter_types.Get().size() == 0)
    InitFilterTypeMap(filter_types.Get());

  DownloadQuery query_out;

  size_t limit = 1000;
  if (query_in.limit.get()) {
    if (*query_in.limit.get() < 0) {
      *error = errors::kInvalidQueryLimit;
      return;
    }
    limit = *query_in.limit.get();
  }
  if (limit > 0) {
    query_out.Limit(limit);
  }

  std::string state_string =
      extensions::api::downloads::ToString(query_in.state);
  if (!state_string.empty()) {
    DownloadItem::DownloadState state = StateEnumFromString(state_string);
    if (state == DownloadItem::MAX_DOWNLOAD_STATE) {
      *error = errors::kInvalidState;
      return;
    }
    query_out.AddFilter(state);
  }
  std::string danger_string =
      extensions::api::downloads::ToString(query_in.danger);
  if (!danger_string.empty()) {
    content::DownloadDangerType danger_type = DangerEnumFromString(
        danger_string);
    if (danger_type == content::DOWNLOAD_DANGER_TYPE_MAX) {
      *error = errors::kInvalidDangerType;
      return;
    }
    query_out.AddFilter(danger_type);
  }
  if (query_in.order_by.get()) {
    CompileDownloadQueryOrderBy(*query_in.order_by.get(), error, &query_out);
    if (!error->empty())
      return;
  }

  scoped_ptr<base::DictionaryValue> query_in_value(query_in.ToValue().Pass());
  for (base::DictionaryValue::Iterator query_json_field(*query_in_value.get());
       !query_json_field.IsAtEnd(); query_json_field.Advance()) {
    FilterTypeMap::const_iterator filter_type =
        filter_types.Get().find(query_json_field.key());
    if (filter_type != filter_types.Get().end()) {
      if (!query_out.AddFilter(filter_type->second, query_json_field.value())) {
        *error = errors::kInvalidFilter;
        return;
      }
    }
  }

  DownloadQuery::DownloadVector all_items;
  if (query_in.id.get()) {
    DownloadItem* download_item = manager->GetDownload(*query_in.id.get());
    if (!download_item && incognito_manager)
      download_item = incognito_manager->GetDownload(*query_in.id.get());
    if (download_item)
      all_items.push_back(download_item);
  } else {
    manager->GetAllDownloads(&all_items);
    if (incognito_manager)
      incognito_manager->GetAllDownloads(&all_items);
  }
  query_out.AddFilter(base::Bind(&IsNotTemporaryDownloadFilter));
  query_out.Search(all_items.begin(), all_items.end(), results);
}

DownloadPathReservationTracker::FilenameConflictAction ConvertConflictAction(
    extensions::api::downloads::FilenameConflictAction action) {
  switch (action) {
    case extensions::api::downloads::FILENAME_CONFLICT_ACTION_NONE:
    case extensions::api::downloads::FILENAME_CONFLICT_ACTION_UNIQUIFY:
      return DownloadPathReservationTracker::UNIQUIFY;
    case extensions::api::downloads::FILENAME_CONFLICT_ACTION_OVERWRITE:
      return DownloadPathReservationTracker::OVERWRITE;
    case extensions::api::downloads::FILENAME_CONFLICT_ACTION_PROMPT:
      return DownloadPathReservationTracker::PROMPT;
  }
  NOTREACHED();
  return DownloadPathReservationTracker::UNIQUIFY;
}

class ExtensionDownloadsEventRouterData : public base::SupportsUserData::Data {
 public:
  static ExtensionDownloadsEventRouterData* Get(DownloadItem* download_item) {
    base::SupportsUserData::Data* data = download_item->GetUserData(kKey);
    return (data == NULL) ? NULL :
        static_cast<ExtensionDownloadsEventRouterData*>(data);
  }

  static void Remove(DownloadItem* download_item) {
    download_item->RemoveUserData(kKey);
  }

  explicit ExtensionDownloadsEventRouterData(
      DownloadItem* download_item,
      scoped_ptr<base::DictionaryValue> json_item)
      : updated_(0),
        changed_fired_(0),
        json_(json_item.Pass()),
        creator_conflict_action_(
            extensions::api::downloads::FILENAME_CONFLICT_ACTION_UNIQUIFY),
        determined_conflict_action_(
            extensions::api::downloads::FILENAME_CONFLICT_ACTION_UNIQUIFY) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    download_item->SetUserData(kKey, this);
  }

  virtual ~ExtensionDownloadsEventRouterData() {
    if (updated_ > 0) {
      UMA_HISTOGRAM_PERCENTAGE("Download.OnChanged",
                               (changed_fired_ * 100 / updated_));
    }
  }

  const base::DictionaryValue& json() const { return *json_.get(); }
  void set_json(scoped_ptr<base::DictionaryValue> json_item) {
    json_ = json_item.Pass();
  }

  void OnItemUpdated() { ++updated_; }
  void OnChangedFired() { ++changed_fired_; }

  void set_filename_change_callbacks(
      const base::Closure& no_change,
      const ExtensionDownloadsEventRouter::FilenameChangedCallback& change) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    filename_no_change_ = no_change;
    filename_change_ = change;
    determined_filename_ = creator_suggested_filename_;
    determined_conflict_action_ = creator_conflict_action_;
    // determiner_.install_time should default to 0 so that creator suggestions
    // should be lower priority than any actual onDeterminingFilename listeners.
  }

  void ClearPendingDeterminers() {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    determined_filename_.clear();
    determined_conflict_action_ =
      extensions::api::downloads::FILENAME_CONFLICT_ACTION_UNIQUIFY;
    determiner_ = DeterminerInfo();
    filename_no_change_ = base::Closure();
    filename_change_ = ExtensionDownloadsEventRouter::FilenameChangedCallback();
    weak_ptr_factory_.reset();
    determiners_.clear();
  }

  void DeterminerRemoved(const std::string& extension_id) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    for (DeterminerInfoVector::iterator iter = determiners_.begin();
         iter != determiners_.end();) {
      if (iter->extension_id == extension_id) {
        iter = determiners_.erase(iter);
      } else {
        ++iter;
      }
    }
    // If we just removed the last unreported determiner, then we need to call a
    // callback.
    CheckAllDeterminersCalled();
  }

  void AddPendingDeterminer(const std::string& extension_id,
                            const base::Time& installed) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    for (size_t index = 0; index < determiners_.size(); ++index) {
      if (determiners_[index].extension_id == extension_id) {
        DCHECK(false) << extension_id;
        return;
      }
    }
    determiners_.push_back(DeterminerInfo(extension_id, installed));
  }

  bool DeterminerAlreadyReported(const std::string& extension_id) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    for (size_t index = 0; index < determiners_.size(); ++index) {
      if (determiners_[index].extension_id == extension_id) {
        return determiners_[index].reported;
      }
    }
    return false;
  }

  void CreatorSuggestedFilename(
      const base::FilePath& filename,
      extensions::api::downloads::FilenameConflictAction conflict_action) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    creator_suggested_filename_ = filename;
    creator_conflict_action_ = conflict_action;
  }

  base::FilePath creator_suggested_filename() const {
    return creator_suggested_filename_;
  }

  extensions::api::downloads::FilenameConflictAction
  creator_conflict_action() const {
    return creator_conflict_action_;
  }

  void ResetCreatorSuggestion() {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    creator_suggested_filename_.clear();
    creator_conflict_action_ =
      extensions::api::downloads::FILENAME_CONFLICT_ACTION_UNIQUIFY;
  }

  // Returns false if this |extension_id| was not expected or if this
  // |extension_id| has already reported. The caller is responsible for
  // validating |filename|.
  bool DeterminerCallback(
      Profile* profile,
      const std::string& extension_id,
      const base::FilePath& filename,
      extensions::api::downloads::FilenameConflictAction conflict_action) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    bool found_info = false;
    for (size_t index = 0; index < determiners_.size(); ++index) {
      if (determiners_[index].extension_id == extension_id) {
        found_info = true;
        if (determiners_[index].reported)
          return false;
        determiners_[index].reported = true;
        // Do not use filename if another determiner has already overridden the
        // filename and they take precedence. Extensions that were installed
        // later take precedence over previous extensions.
        if (!filename.empty()) {
          extensions::ExtensionWarningSet warnings;
          std::string winner_extension_id;
          ExtensionDownloadsEventRouter::DetermineFilenameInternal(
              filename,
              conflict_action,
              determiners_[index].extension_id,
              determiners_[index].install_time,
              determiner_.extension_id,
              determiner_.install_time,
              &winner_extension_id,
              &determined_filename_,
              &determined_conflict_action_,
              &warnings);
          if (!warnings.empty())
            extensions::ExtensionWarningService::NotifyWarningsOnUI(
                profile, warnings);
          if (winner_extension_id == determiners_[index].extension_id)
            determiner_ = determiners_[index];
        }
        break;
      }
    }
    if (!found_info)
      return false;
    CheckAllDeterminersCalled();
    return true;
  }

 private:
  struct DeterminerInfo {
    DeterminerInfo();
    DeterminerInfo(const std::string& e_id,
                   const base::Time& installed);
    ~DeterminerInfo();

    std::string extension_id;
    base::Time install_time;
    bool reported;
  };
  typedef std::vector<DeterminerInfo> DeterminerInfoVector;

  static const char kKey[];

  // This is safe to call even while not waiting for determiners to call back;
  // in that case, the callbacks will be null so they won't be Run.
  void CheckAllDeterminersCalled() {
    for (DeterminerInfoVector::iterator iter = determiners_.begin();
         iter != determiners_.end(); ++iter) {
      if (!iter->reported)
        return;
    }
    if (determined_filename_.empty()) {
      if (!filename_no_change_.is_null())
        filename_no_change_.Run();
    } else {
      if (!filename_change_.is_null()) {
        filename_change_.Run(determined_filename_, ConvertConflictAction(
            determined_conflict_action_));
      }
    }
    // Don't clear determiners_ immediately in case there's a second listener
    // for one of the extensions, so that DetermineFilename can return
    // kTooManyListeners. After a few seconds, DetermineFilename will return
    // kUnexpectedDeterminer instead of kTooManyListeners so that determiners_
    // doesn't keep hogging memory.
    weak_ptr_factory_.reset(
        new base::WeakPtrFactory<ExtensionDownloadsEventRouterData>(this));
    base::MessageLoopForUI::current()->PostDelayedTask(
        FROM_HERE,
        base::Bind(&ExtensionDownloadsEventRouterData::ClearPendingDeterminers,
                   weak_ptr_factory_->GetWeakPtr()),
        base::TimeDelta::FromSeconds(30));
  }

  int updated_;
  int changed_fired_;
  scoped_ptr<base::DictionaryValue> json_;

  base::Closure filename_no_change_;
  ExtensionDownloadsEventRouter::FilenameChangedCallback filename_change_;

  DeterminerInfoVector determiners_;

  base::FilePath creator_suggested_filename_;
  extensions::api::downloads::FilenameConflictAction
    creator_conflict_action_;
  base::FilePath determined_filename_;
  extensions::api::downloads::FilenameConflictAction
    determined_conflict_action_;
  DeterminerInfo determiner_;

  scoped_ptr<base::WeakPtrFactory<ExtensionDownloadsEventRouterData> >
    weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionDownloadsEventRouterData);
};

ExtensionDownloadsEventRouterData::DeterminerInfo::DeterminerInfo(
    const std::string& e_id,
    const base::Time& installed)
    : extension_id(e_id),
      install_time(installed),
      reported(false) {
}

ExtensionDownloadsEventRouterData::DeterminerInfo::DeterminerInfo()
    : reported(false) {
}

ExtensionDownloadsEventRouterData::DeterminerInfo::~DeterminerInfo() {}

const char ExtensionDownloadsEventRouterData::kKey[] =
  "DownloadItem ExtensionDownloadsEventRouterData";

class ManagerDestructionObserver : public DownloadManager::Observer {
 public:
  static void CheckForHistoryFilesRemoval(DownloadManager* manager) {
    if (!manager)
      return;
    if (!manager_file_existence_last_checked_)
      manager_file_existence_last_checked_ =
        new std::map<DownloadManager*, ManagerDestructionObserver*>();
    if (!(*manager_file_existence_last_checked_)[manager])
      (*manager_file_existence_last_checked_)[manager] =
        new ManagerDestructionObserver(manager);
    (*manager_file_existence_last_checked_)[manager]->
      CheckForHistoryFilesRemovalInternal();
  }

 private:
  static const int kFileExistenceRateLimitSeconds = 10;

  explicit ManagerDestructionObserver(DownloadManager* manager)
      : manager_(manager) {
    manager_->AddObserver(this);
  }

  virtual ~ManagerDestructionObserver() {
    manager_->RemoveObserver(this);
  }

  virtual void ManagerGoingDown(DownloadManager* manager) OVERRIDE {
    manager_file_existence_last_checked_->erase(manager);
    if (manager_file_existence_last_checked_->size() == 0) {
      delete manager_file_existence_last_checked_;
      manager_file_existence_last_checked_ = NULL;
    }
  }

  void CheckForHistoryFilesRemovalInternal() {
    base::Time now(base::Time::Now());
    int delta = now.ToTimeT() - last_checked_.ToTimeT();
    if (delta > kFileExistenceRateLimitSeconds) {
      last_checked_ = now;
      manager_->CheckForHistoryFilesRemoval();
    }
  }

  static std::map<DownloadManager*, ManagerDestructionObserver*>*
    manager_file_existence_last_checked_;

  DownloadManager* manager_;
  base::Time last_checked_;

  DISALLOW_COPY_AND_ASSIGN(ManagerDestructionObserver);
};

std::map<DownloadManager*, ManagerDestructionObserver*>*
  ManagerDestructionObserver::manager_file_existence_last_checked_ = NULL;

void OnDeterminingFilenameWillDispatchCallback(
    bool* any_determiners,
    ExtensionDownloadsEventRouterData* data,
    Profile* profile,
    const extensions::Extension* extension,
    base::ListValue* event_args) {
  *any_determiners = true;
  base::Time installed = extensions::ExtensionSystem::Get(
      profile)->extension_service()->extension_prefs()->
    GetInstallTime(extension->id());
  data->AddPendingDeterminer(extension->id(), installed);
}

bool Fault(bool error,
           const char* message_in,
           std::string* message_out) {
  if (!error)
    return false;
  *message_out = message_in;
  return true;
}

bool InvalidId(DownloadItem* valid_item, std::string* message_out) {
  return Fault(!valid_item, errors::kInvalidId, message_out);
}

bool IsDownloadDeltaField(const std::string& field) {
  return ((field == kUrlKey) ||
          (field == kFilenameKey) ||
          (field == kDangerKey) ||
          (field == kMimeKey) ||
          (field == kStartTimeKey) ||
          (field == kEndTimeKey) ||
          (field == kStateKey) ||
          (field == kCanResumeKey) ||
          (field == kPausedKey) ||
          (field == kErrorKey) ||
          (field == kTotalBytesKey) ||
          (field == kFileSizeKey) ||
          (field == kExistsKey));
}

}  // namespace

const char DownloadedByExtension::kKey[] =
  "DownloadItem DownloadedByExtension";

DownloadedByExtension* DownloadedByExtension::Get(
    content::DownloadItem* item) {
  base::SupportsUserData::Data* data = item->GetUserData(kKey);
  return (data == NULL) ? NULL :
      static_cast<DownloadedByExtension*>(data);
}

DownloadedByExtension::DownloadedByExtension(
    content::DownloadItem* item,
    const std::string& id,
    const std::string& name)
  : id_(id),
    name_(name) {
  item->SetUserData(kKey, this);
}

DownloadsDownloadFunction::DownloadsDownloadFunction() {}

DownloadsDownloadFunction::~DownloadsDownloadFunction() {}

bool DownloadsDownloadFunction::RunImpl() {
  scoped_ptr<extensions::api::downloads::Download::Params> params(
      extensions::api::downloads::Download::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());
  const extensions::api::downloads::DownloadOptions& options = params->options;
  GURL download_url(options.url);
  if (Fault(!download_url.is_valid(), errors::kInvalidURL, &error_))
    return false;

  Profile* current_profile = profile();
  if (include_incognito() && profile()->HasOffTheRecordProfile())
    current_profile = profile()->GetOffTheRecordProfile();

  scoped_ptr<content::DownloadUrlParameters> download_params(
      new content::DownloadUrlParameters(
          download_url,
          render_view_host()->GetProcess()->GetID(),
          render_view_host()->GetRoutingID(),
          current_profile->GetResourceContext()));

  base::FilePath creator_suggested_filename;
  if (options.filename.get()) {
#if defined(OS_WIN)
    // Can't get filename16 from options.ToValue() because that converts it from
    // std::string.
    base::DictionaryValue* options_value = NULL;
    EXTENSION_FUNCTION_VALIDATE(args_->GetDictionary(0, &options_value));
    base::string16 filename16;
    EXTENSION_FUNCTION_VALIDATE(options_value->GetString(
        kFilenameKey, &filename16));
    creator_suggested_filename = base::FilePath(filename16);
#elif defined(OS_POSIX)
    creator_suggested_filename = base::FilePath(*options.filename.get());
#endif
    if (!net::IsSafePortableRelativePath(creator_suggested_filename)) {
      error_ = errors::kInvalidFilename;
      return false;
    }
  }

  if (options.save_as.get())
    download_params->set_prompt(*options.save_as.get());

  if (options.headers.get()) {
    typedef extensions::api::downloads::HeaderNameValuePair HeaderNameValuePair;
    for (std::vector<linked_ptr<HeaderNameValuePair> >::const_iterator iter =
         options.headers->begin();
         iter != options.headers->end();
         ++iter) {
      const HeaderNameValuePair& name_value = **iter;
      if (!net::HttpUtil::IsSafeHeader(name_value.name)) {
        error_ = errors::kInvalidHeader;
        return false;
      }
      download_params->add_request_header(name_value.name, name_value.value);
    }
  }

  std::string method_string =
      extensions::api::downloads::ToString(options.method);
  if (!method_string.empty())
    download_params->set_method(method_string);
  if (options.body.get())
    download_params->set_post_body(*options.body.get());
  download_params->set_callback(base::Bind(
      &DownloadsDownloadFunction::OnStarted, this,
      creator_suggested_filename, options.conflict_action));
  // Prevent login prompts for 401/407 responses.
  download_params->set_load_flags(net::LOAD_DO_NOT_PROMPT_FOR_LOGIN);

  DownloadManager* manager = BrowserContext::GetDownloadManager(
      current_profile);
  manager->DownloadUrl(download_params.Pass());
  RecordDownloadSource(DOWNLOAD_INITIATED_BY_EXTENSION);
  RecordApiFunctions(DOWNLOADS_FUNCTION_DOWNLOAD);
  return true;
}

void DownloadsDownloadFunction::OnStarted(
    const base::FilePath& creator_suggested_filename,
    extensions::api::downloads::FilenameConflictAction creator_conflict_action,
    DownloadItem* item,
    net::Error error) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  VLOG(1) << __FUNCTION__ << " " << item << " " << error;
  if (item) {
    DCHECK_EQ(net::OK, error);
    SetResult(base::Value::CreateIntegerValue(item->GetId()));
    if (!creator_suggested_filename.empty()) {
      ExtensionDownloadsEventRouterData* data =
          ExtensionDownloadsEventRouterData::Get(item);
      if (!data) {
        data = new ExtensionDownloadsEventRouterData(
            item,
            scoped_ptr<base::DictionaryValue>(new base::DictionaryValue()));
      }
      data->CreatorSuggestedFilename(
          creator_suggested_filename, creator_conflict_action);
    }
    new DownloadedByExtension(
        item, GetExtension()->id(), GetExtension()->name());
    item->UpdateObservers();
  } else {
    DCHECK_NE(net::OK, error);
    error_ = net::ErrorToString(error);
  }
  SendResponse(error_.empty());
}

DownloadsSearchFunction::DownloadsSearchFunction() {}

DownloadsSearchFunction::~DownloadsSearchFunction() {}

bool DownloadsSearchFunction::RunImpl() {
  scoped_ptr<extensions::api::downloads::Search::Params> params(
      extensions::api::downloads::Search::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());
  DownloadManager* manager = NULL;
  DownloadManager* incognito_manager = NULL;
  GetManagers(profile(), include_incognito(), &manager, &incognito_manager);
  ManagerDestructionObserver::CheckForHistoryFilesRemoval(manager);
  ManagerDestructionObserver::CheckForHistoryFilesRemoval(incognito_manager);
  DownloadQuery::DownloadVector results;
  RunDownloadQuery(params->query,
                   manager,
                   incognito_manager,
                   &error_,
                   &results);
  if (!error_.empty())
    return false;

  base::ListValue* json_results = new base::ListValue();
  for (DownloadManager::DownloadVector::const_iterator it = results.begin();
       it != results.end(); ++it) {
    DownloadItem* download_item = *it;
    uint32 download_id = download_item->GetId();
    bool off_record = ((incognito_manager != NULL) &&
                       (incognito_manager->GetDownload(download_id) != NULL));
    scoped_ptr<base::DictionaryValue> json_item(DownloadItemToJSON(
        *it, off_record ? profile()->GetOffTheRecordProfile()
                        : profile()->GetOriginalProfile()));
    json_results->Append(json_item.release());
  }
  SetResult(json_results);
  RecordApiFunctions(DOWNLOADS_FUNCTION_SEARCH);
  return true;
}

DownloadsPauseFunction::DownloadsPauseFunction() {}

DownloadsPauseFunction::~DownloadsPauseFunction() {}

bool DownloadsPauseFunction::RunImpl() {
  scoped_ptr<extensions::api::downloads::Pause::Params> params(
      extensions::api::downloads::Pause::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());
  DownloadItem* download_item = GetDownload(
      profile(), include_incognito(), params->download_id);
  if (InvalidId(download_item, &error_) ||
      Fault(download_item->GetState() != DownloadItem::IN_PROGRESS,
            errors::kNotInProgress, &error_))
    return false;
  // If the item is already paused, this is a no-op and the operation will
  // silently succeed.
  download_item->Pause();
  RecordApiFunctions(DOWNLOADS_FUNCTION_PAUSE);
  return true;
}

DownloadsResumeFunction::DownloadsResumeFunction() {}

DownloadsResumeFunction::~DownloadsResumeFunction() {}

bool DownloadsResumeFunction::RunImpl() {
  scoped_ptr<extensions::api::downloads::Resume::Params> params(
      extensions::api::downloads::Resume::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());
  DownloadItem* download_item = GetDownload(
      profile(), include_incognito(), params->download_id);
  if (InvalidId(download_item, &error_) ||
      Fault(download_item->IsPaused() && !download_item->CanResume(),
            errors::kNotResumable, &error_))
    return false;
  // Note that if the item isn't paused, this will be a no-op, and the extension
  // call will seem successful.
  download_item->Resume();
  RecordApiFunctions(DOWNLOADS_FUNCTION_RESUME);
  return true;
}

DownloadsCancelFunction::DownloadsCancelFunction() {}

DownloadsCancelFunction::~DownloadsCancelFunction() {}

bool DownloadsCancelFunction::RunImpl() {
  scoped_ptr<extensions::api::downloads::Resume::Params> params(
      extensions::api::downloads::Resume::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());
  DownloadItem* download_item = GetDownload(
      profile(), include_incognito(), params->download_id);
  if (download_item &&
      (download_item->GetState() == DownloadItem::IN_PROGRESS))
    download_item->Cancel(true);
  // |download_item| can be NULL if the download ID was invalid or if the
  // download is not currently active.  Either way, it's not a failure.
  RecordApiFunctions(DOWNLOADS_FUNCTION_CANCEL);
  return true;
}

DownloadsEraseFunction::DownloadsEraseFunction() {}

DownloadsEraseFunction::~DownloadsEraseFunction() {}

bool DownloadsEraseFunction::RunImpl() {
  scoped_ptr<extensions::api::downloads::Erase::Params> params(
      extensions::api::downloads::Erase::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());
  DownloadManager* manager = NULL;
  DownloadManager* incognito_manager = NULL;
  GetManagers(profile(), include_incognito(), &manager, &incognito_manager);
  DownloadQuery::DownloadVector results;
  RunDownloadQuery(params->query,
                   manager,
                   incognito_manager,
                   &error_,
                   &results);
  if (!error_.empty())
    return false;
  base::ListValue* json_results = new base::ListValue();
  for (DownloadManager::DownloadVector::const_iterator it = results.begin();
       it != results.end(); ++it) {
    json_results->Append(base::Value::CreateIntegerValue((*it)->GetId()));
    (*it)->Remove();
  }
  SetResult(json_results);
  RecordApiFunctions(DOWNLOADS_FUNCTION_ERASE);
  return true;
}

DownloadsRemoveFileFunction::DownloadsRemoveFileFunction() {}

DownloadsRemoveFileFunction::~DownloadsRemoveFileFunction() {
  if (item_) {
    item_->RemoveObserver(this);
    item_ = NULL;
  }
}

bool DownloadsRemoveFileFunction::RunImpl() {
  scoped_ptr<extensions::api::downloads::RemoveFile::Params> params(
      extensions::api::downloads::RemoveFile::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());
  DownloadItem* download_item = GetDownload(
      profile(), include_incognito(), params->download_id);
  if (InvalidId(download_item, &error_) ||
      Fault((download_item->GetState() != DownloadItem::COMPLETE),
            errors::kNotComplete, &error_) ||
      Fault(download_item->GetFileExternallyRemoved(),
            errors::kFileAlreadyDeleted, &error_))
    return false;
  item_ = download_item;
  item_->AddObserver(this);
  RecordApiFunctions(DOWNLOADS_FUNCTION_REMOVE_FILE);
  download_item->DeleteFile();
  return true;
}

void DownloadsRemoveFileFunction::OnDownloadUpdated(DownloadItem* download) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK_EQ(item_, download);
  if (!item_->GetFileExternallyRemoved())
    return;
  item_->RemoveObserver(this);
  item_ = NULL;
  SendResponse(true);
}

void DownloadsRemoveFileFunction::OnDownloadDestroyed(DownloadItem* download) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK_EQ(item_, download);
  item_->RemoveObserver(this);
  item_ = NULL;
  SendResponse(true);
}

DownloadsAcceptDangerFunction::DownloadsAcceptDangerFunction() {}

DownloadsAcceptDangerFunction::~DownloadsAcceptDangerFunction() {}

bool DownloadsAcceptDangerFunction::RunImpl() {
  scoped_ptr<extensions::api::downloads::AcceptDanger::Params> params(
      extensions::api::downloads::AcceptDanger::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());
  DownloadItem* download_item = GetDownload(
      profile(), include_incognito(), params->download_id);
  content::WebContents* web_contents =
      dispatcher()->delegate()->GetVisibleWebContents();
  if (InvalidId(download_item, &error_) ||
      Fault(download_item->GetState() != DownloadItem::IN_PROGRESS,
            errors::kNotInProgress, &error_) ||
      Fault(!download_item->IsDangerous(), errors::kNotDangerous, &error_) ||
      Fault(!web_contents, errors::kInvisibleContext, &error_))
    return false;
  RecordApiFunctions(DOWNLOADS_FUNCTION_ACCEPT_DANGER);
  // DownloadDangerPrompt displays a modal dialog using native widgets that the
  // user must either accept or cancel. It cannot be scripted.
  DownloadDangerPrompt::Create(
      download_item,
      web_contents,
      true,
      base::Bind(&DownloadsAcceptDangerFunction::DangerPromptCallback,
                 this, params->download_id));
  // DownloadDangerPrompt deletes itself
  return true;
}

void DownloadsAcceptDangerFunction::DangerPromptCallback(
    int download_id, DownloadDangerPrompt::Action action) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DownloadItem* download_item = GetDownload(
      profile(), include_incognito(), download_id);
  if (InvalidId(download_item, &error_) ||
      Fault(download_item->GetState() != DownloadItem::IN_PROGRESS,
            errors::kNotInProgress, &error_))
    return;
  switch (action) {
    case DownloadDangerPrompt::ACCEPT:
      download_item->ValidateDangerousDownload();
      break;
    case DownloadDangerPrompt::CANCEL:
      download_item->Remove();
      break;
    case DownloadDangerPrompt::DISMISS:
      break;
  }
  SendResponse(error_.empty());
}

DownloadsShowFunction::DownloadsShowFunction() {}

DownloadsShowFunction::~DownloadsShowFunction() {}

bool DownloadsShowFunction::RunImpl() {
  scoped_ptr<extensions::api::downloads::Show::Params> params(
      extensions::api::downloads::Show::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());
  DownloadItem* download_item = GetDownload(
      profile(), include_incognito(), params->download_id);
  if (InvalidId(download_item, &error_))
    return false;
  download_item->ShowDownloadInShell();
  RecordApiFunctions(DOWNLOADS_FUNCTION_SHOW);
  return true;
}

DownloadsShowDefaultFolderFunction::DownloadsShowDefaultFolderFunction() {}

DownloadsShowDefaultFolderFunction::~DownloadsShowDefaultFolderFunction() {}

bool DownloadsShowDefaultFolderFunction::RunImpl() {
  DownloadManager* manager = NULL;
  DownloadManager* incognito_manager = NULL;
  GetManagers(profile(), include_incognito(), &manager, &incognito_manager);
  platform_util::OpenItem(DownloadPrefs::FromDownloadManager(
      manager)->DownloadPath());
  RecordApiFunctions(DOWNLOADS_FUNCTION_SHOW_DEFAULT_FOLDER);
  return true;
}

DownloadsOpenFunction::DownloadsOpenFunction() {}

DownloadsOpenFunction::~DownloadsOpenFunction() {}

bool DownloadsOpenFunction::RunImpl() {
  scoped_ptr<extensions::api::downloads::Open::Params> params(
      extensions::api::downloads::Open::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());
  DownloadItem* download_item = GetDownload(
      profile(), include_incognito(), params->download_id);
  if (InvalidId(download_item, &error_) ||
      Fault(download_item->GetState() != DownloadItem::COMPLETE,
            errors::kNotComplete, &error_) ||
      Fault(!GetExtension()->HasAPIPermission(
                extensions::APIPermission::kDownloadsOpen),
            errors::kOpenPermission, &error_))
    return false;
  download_item->OpenDownload();
  RecordApiFunctions(DOWNLOADS_FUNCTION_OPEN);
  return true;
}

DownloadsDragFunction::DownloadsDragFunction() {}

DownloadsDragFunction::~DownloadsDragFunction() {}

bool DownloadsDragFunction::RunImpl() {
  scoped_ptr<extensions::api::downloads::Drag::Params> params(
      extensions::api::downloads::Drag::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());
  DownloadItem* download_item = GetDownload(
      profile(), include_incognito(), params->download_id);
  content::WebContents* web_contents =
      dispatcher()->delegate()->GetVisibleWebContents();
  if (InvalidId(download_item, &error_) ||
      Fault(!web_contents, errors::kInvisibleContext, &error_))
    return false;
  RecordApiFunctions(DOWNLOADS_FUNCTION_DRAG);
  gfx::Image* icon = g_browser_process->icon_manager()->LookupIconFromFilepath(
      download_item->GetTargetFilePath(), IconLoader::NORMAL);
  gfx::NativeView view = web_contents->GetView()->GetNativeView();
  {
    // Enable nested tasks during DnD, while |DragDownload()| blocks.
    base::MessageLoop::ScopedNestableTaskAllower allow(
        base::MessageLoop::current());
    download_util::DragDownload(download_item, icon, view);
  }
  return true;
}

DownloadsSetShelfEnabledFunction::DownloadsSetShelfEnabledFunction() {}

DownloadsSetShelfEnabledFunction::~DownloadsSetShelfEnabledFunction() {}

bool DownloadsSetShelfEnabledFunction::RunImpl() {
  scoped_ptr<extensions::api::downloads::SetShelfEnabled::Params> params(
      extensions::api::downloads::SetShelfEnabled::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());
  if (!GetExtension()->HasAPIPermission(
        extensions::APIPermission::kDownloadsShelf)) {
    error_ = download_extension_errors::kShelfPermission;
    return false;
  }

  RecordApiFunctions(DOWNLOADS_FUNCTION_SET_SHELF_ENABLED);
  DownloadManager* manager = NULL;
  DownloadManager* incognito_manager = NULL;
  GetManagers(profile(), include_incognito(), &manager, &incognito_manager);
  DownloadService* service = NULL;
  DownloadService* incognito_service = NULL;
  if (manager) {
    service = DownloadServiceFactory::GetForBrowserContext(
        manager->GetBrowserContext());
    service->GetExtensionEventRouter()->SetShelfEnabled(
        GetExtension(), params->enabled);
  }
  if (incognito_manager) {
    incognito_service = DownloadServiceFactory::GetForBrowserContext(
        incognito_manager->GetBrowserContext());
    incognito_service->GetExtensionEventRouter()->SetShelfEnabled(
        GetExtension(), params->enabled);
  }

  BrowserList* browsers = BrowserList::GetInstance(chrome::GetActiveDesktop());
  if (browsers) {
    for (BrowserList::const_iterator iter = browsers->begin();
        iter != browsers->end(); ++iter) {
      const Browser* browser = *iter;
      DownloadService* current_service =
        DownloadServiceFactory::GetForBrowserContext(browser->profile());
      if (((current_service == service) ||
           (current_service == incognito_service)) &&
          browser->window()->IsDownloadShelfVisible() &&
          !current_service->IsShelfEnabled())
        browser->window()->GetDownloadShelf()->Close(DownloadShelf::AUTOMATIC);
    }
  }

  if (params->enabled &&
      ((manager && !service->IsShelfEnabled()) ||
       (incognito_manager && !incognito_service->IsShelfEnabled()))) {
    error_ = download_extension_errors::kShelfDisabled;
    return false;
  }

  return true;
}

DownloadsGetFileIconFunction::DownloadsGetFileIconFunction()
    : icon_extractor_(new DownloadFileIconExtractorImpl()) {
}

DownloadsGetFileIconFunction::~DownloadsGetFileIconFunction() {}

void DownloadsGetFileIconFunction::SetIconExtractorForTesting(
    DownloadFileIconExtractor* extractor) {
  DCHECK(extractor);
  icon_extractor_.reset(extractor);
}

bool DownloadsGetFileIconFunction::RunImpl() {
  scoped_ptr<extensions::api::downloads::GetFileIcon::Params> params(
      extensions::api::downloads::GetFileIcon::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());
  const extensions::api::downloads::GetFileIconOptions* options =
      params->options.get();
  int icon_size = kDefaultIconSize;
  if (options && options->size.get())
    icon_size = *options->size.get();
  DownloadItem* download_item = GetDownload(
      profile(), include_incognito(), params->download_id);
  if (InvalidId(download_item, &error_) ||
      Fault(download_item->GetTargetFilePath().empty(),
            errors::kEmptyFile, &error_))
    return false;
  // In-progress downloads return the intermediate filename for GetFullPath()
  // which doesn't have the final extension. Therefore a good file icon can't be
  // found, so use GetTargetFilePath() instead.
  DCHECK(icon_extractor_.get());
  DCHECK(icon_size == 16 || icon_size == 32);
  EXTENSION_FUNCTION_VALIDATE(icon_extractor_->ExtractIconURLForPath(
      download_item->GetTargetFilePath(),
      IconLoaderSizeFromPixelSize(icon_size),
      base::Bind(&DownloadsGetFileIconFunction::OnIconURLExtracted, this)));
  return true;
}

void DownloadsGetFileIconFunction::OnIconURLExtracted(const std::string& url) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (Fault(url.empty(), errors::kIconNotFound, &error_)) {
    SendResponse(false);
    return;
  }
  RecordApiFunctions(DOWNLOADS_FUNCTION_GET_FILE_ICON);
  SetResult(base::Value::CreateStringValue(url));
  SendResponse(true);
}

ExtensionDownloadsEventRouter::ExtensionDownloadsEventRouter(
    Profile* profile,
    DownloadManager* manager)
    : profile_(profile),
      notifier_(manager, this) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(profile_);
  registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_UNLOADED,
                 content::Source<Profile>(profile_));
  extensions::EventRouter* router = extensions::ExtensionSystem::Get(profile_)->
      event_router();
  if (router)
    router->RegisterObserver(this, events::kOnDownloadDeterminingFilename);
}

ExtensionDownloadsEventRouter::~ExtensionDownloadsEventRouter() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  extensions::EventRouter* router = extensions::ExtensionSystem::Get(profile_)->
      event_router();
  if (router)
    router->UnregisterObserver(this);
}

void ExtensionDownloadsEventRouter::SetShelfEnabled(
    const extensions::Extension* extension, bool enabled) {
  std::set<const extensions::Extension*>::iterator iter =
    shelf_disabling_extensions_.find(extension);
  if (iter == shelf_disabling_extensions_.end()) {
    if (!enabled)
      shelf_disabling_extensions_.insert(extension);
  } else if (enabled) {
    shelf_disabling_extensions_.erase(extension);
  }
}

bool ExtensionDownloadsEventRouter::IsShelfEnabled() const {
  return shelf_disabling_extensions_.empty();
}

// The method by which extensions hook into the filename determination process
// is based on the method by which the omnibox API allows extensions to hook
// into the omnibox autocompletion process. Extensions that wish to play a part
// in the filename determination process call
// chrome.downloads.onDeterminingFilename.addListener, which adds an
// EventListener object to ExtensionEventRouter::listeners().
//
// When a download's filename is being determined,
// ChromeDownloadManagerDelegate::CheckVisitedReferrerBeforeDone (CVRBD) passes
// 2 callbacks to ExtensionDownloadsEventRouter::OnDeterminingFilename (ODF),
// which stores the callbacks in the item's ExtensionDownloadsEventRouterData
// (EDERD) along with all of the extension IDs that are listening for
// onDeterminingFilename events.  ODF dispatches
// chrome.downloads.onDeterminingFilename.
//
// When the extension's event handler calls |suggestCallback|,
// downloads_custom_bindings.js calls
// DownloadsInternalDetermineFilenameFunction::RunImpl, which calls
// EDER::DetermineFilename, which notifies the item's EDERD.
//
// When the last extension's event handler returns, EDERD calls one of the two
// callbacks that CVRBD passed to ODF, allowing CDMD to complete the filename
// determination process. If multiple extensions wish to override the filename,
// then the extension that was last installed wins.

void ExtensionDownloadsEventRouter::OnDeterminingFilename(
    DownloadItem* item,
    const base::FilePath& suggested_path,
    const base::Closure& no_change,
    const FilenameChangedCallback& change) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  ExtensionDownloadsEventRouterData* data =
      ExtensionDownloadsEventRouterData::Get(item);
  if (!data) {
    no_change.Run();
    return;
  }
  data->ClearPendingDeterminers();
  data->set_filename_change_callbacks(no_change, change);
  bool any_determiners = false;
  base::DictionaryValue* json = DownloadItemToJSON(
      item, profile_).release();
  json->SetString(kFilenameKey, suggested_path.LossyDisplayName());
  DispatchEvent(events::kOnDownloadDeterminingFilename,
                false,
                base::Bind(&OnDeterminingFilenameWillDispatchCallback,
                           &any_determiners,
                           data),
                json);
  if (!any_determiners) {
    data->ClearPendingDeterminers();
    if (!data->creator_suggested_filename().empty()) {
      change.Run(data->creator_suggested_filename(),
                 ConvertConflictAction(data->creator_conflict_action()));
      // If all listeners are removed, don't keep |data| around.
      data->ResetCreatorSuggestion();
    } else {
      no_change.Run();
    }
  }
}

void ExtensionDownloadsEventRouter::DetermineFilenameInternal(
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
    extensions::ExtensionWarningSet* warnings) {
  DCHECK(!filename.empty());
  DCHECK(!suggesting_extension_id.empty());

  if (incumbent_extension_id.empty()) {
    *winner_extension_id = suggesting_extension_id;
    *determined_filename = filename;
    *determined_conflict_action = conflict_action;
    return;
  }

  if (suggesting_install_time < incumbent_install_time) {
    *winner_extension_id = incumbent_extension_id;
    warnings->insert(
        extensions::ExtensionWarning::CreateDownloadFilenameConflictWarning(
            suggesting_extension_id,
            incumbent_extension_id,
            filename,
            *determined_filename));
    return;
  }

  *winner_extension_id = suggesting_extension_id;
  warnings->insert(
      extensions::ExtensionWarning::CreateDownloadFilenameConflictWarning(
          incumbent_extension_id,
          suggesting_extension_id,
          *determined_filename,
          filename));
  *determined_filename = filename;
  *determined_conflict_action = conflict_action;
}

bool ExtensionDownloadsEventRouter::DetermineFilename(
    Profile* profile,
    bool include_incognito,
    const std::string& ext_id,
    int download_id,
    const base::FilePath& const_filename,
    extensions::api::downloads::FilenameConflictAction conflict_action,
    std::string* error) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DownloadItem* item = GetDownload(profile, include_incognito, download_id);
  ExtensionDownloadsEventRouterData* data =
      item ? ExtensionDownloadsEventRouterData::Get(item) : NULL;
  // maxListeners=1 in downloads.idl and suggestCallback in
  // downloads_custom_bindings.js should prevent duplicate DeterminerCallback
  // calls from the same renderer, but an extension may have more than one
  // renderer, so don't DCHECK(!reported).
  if (InvalidId(item, error) ||
      Fault(item->GetState() != DownloadItem::IN_PROGRESS,
            errors::kNotInProgress, error) ||
      Fault(!data, errors::kUnexpectedDeterminer, error) ||
      Fault(data->DeterminerAlreadyReported(ext_id),
            errors::kTooManyListeners, error))
    return false;
  base::FilePath::StringType filename_str(const_filename.value());
  // Allow windows-style directory separators on all platforms.
  std::replace(filename_str.begin(), filename_str.end(),
               FILE_PATH_LITERAL('\\'), FILE_PATH_LITERAL('/'));
  base::FilePath filename(filename_str);
  bool valid_filename = net::IsSafePortableRelativePath(filename);
  filename = (valid_filename ? filename.NormalizePathSeparators() :
              base::FilePath());
  // If the invalid filename check is moved to before DeterminerCallback(), then
  // it will block forever waiting for this ext_id to report.
  if (Fault(!data->DeterminerCallback(
                profile, ext_id, filename, conflict_action),
            errors::kUnexpectedDeterminer, error) ||
      Fault((!const_filename.empty() && !valid_filename),
            errors::kInvalidFilename, error))
    return false;
  return true;
}

void ExtensionDownloadsEventRouter::OnListenerRemoved(
    const extensions::EventListenerInfo& details) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DownloadManager* manager = notifier_.GetManager();
  if (!manager)
    return;
  bool determiner_removed = (
      details.event_name == events::kOnDownloadDeterminingFilename);
  extensions::EventRouter* router = extensions::ExtensionSystem::Get(profile_)->
      event_router();
  bool any_listeners =
    router->HasEventListener(events::kOnDownloadChanged) ||
    router->HasEventListener(events::kOnDownloadDeterminingFilename);
  if (!determiner_removed && any_listeners)
    return;
  DownloadManager::DownloadVector items;
  manager->GetAllDownloads(&items);
  for (DownloadManager::DownloadVector::const_iterator iter =
       items.begin();
       iter != items.end(); ++iter) {
    ExtensionDownloadsEventRouterData* data =
        ExtensionDownloadsEventRouterData::Get(*iter);
    if (!data)
      continue;
    if (determiner_removed) {
      // Notify any items that may be waiting for callbacks from this
      // extension/determiner.  This will almost always be a no-op, however, it
      // is possible for an extension renderer to be unloaded while a download
      // item is waiting for a determiner. In that case, the download item
      // should proceed.
      data->DeterminerRemoved(details.extension_id);
    }
    if (!any_listeners &&
        data->creator_suggested_filename().empty()) {
      ExtensionDownloadsEventRouterData::Remove(*iter);
    }
  }
}

// That's all the methods that have to do with filename determination. The rest
// have to do with the other, less special events.

void ExtensionDownloadsEventRouter::OnDownloadCreated(
    DownloadManager* manager, DownloadItem* download_item) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (download_item->IsTemporary())
    return;

  extensions::EventRouter* router = extensions::ExtensionSystem::Get(profile_)->
      event_router();
  // Avoid allocating a bunch of memory in DownloadItemToJSON if it isn't going
  // to be used.
  if (!router ||
      (!router->HasEventListener(events::kOnDownloadCreated) &&
       !router->HasEventListener(events::kOnDownloadChanged) &&
       !router->HasEventListener(events::kOnDownloadDeterminingFilename))) {
    return;
  }
  scoped_ptr<base::DictionaryValue> json_item(
      DownloadItemToJSON(download_item, profile_));
  DispatchEvent(events::kOnDownloadCreated,
                true,
                extensions::Event::WillDispatchCallback(),
                json_item->DeepCopy());
  if (!ExtensionDownloadsEventRouterData::Get(download_item) &&
      (router->HasEventListener(events::kOnDownloadChanged) ||
       router->HasEventListener(events::kOnDownloadDeterminingFilename))) {
    new ExtensionDownloadsEventRouterData(download_item, json_item.Pass());
  }
}

void ExtensionDownloadsEventRouter::OnDownloadUpdated(
    DownloadManager* manager, DownloadItem* download_item) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  extensions::EventRouter* router = extensions::ExtensionSystem::Get(profile_)->
      event_router();
  ExtensionDownloadsEventRouterData* data =
    ExtensionDownloadsEventRouterData::Get(download_item);
  if (download_item->IsTemporary() ||
      !router->HasEventListener(events::kOnDownloadChanged)) {
    return;
  }
  if (!data) {
    // The download_item probably transitioned from temporary to not temporary,
    // or else an event listener was added.
    data = new ExtensionDownloadsEventRouterData(
        download_item,
        scoped_ptr<base::DictionaryValue>(new base::DictionaryValue()));
  }
  scoped_ptr<base::DictionaryValue> new_json(DownloadItemToJSON(
      download_item, profile_));
  scoped_ptr<base::DictionaryValue> delta(new base::DictionaryValue());
  delta->SetInteger(kIdKey, download_item->GetId());
  std::set<std::string> new_fields;
  bool changed = false;

  // For each field in the new json representation of the download_item except
  // the bytesReceived field, if the field has changed from the previous old
  // json, set the differences in the |delta| object and remember that something
  // significant changed.
  for (base::DictionaryValue::Iterator iter(*new_json.get());
       !iter.IsAtEnd(); iter.Advance()) {
    new_fields.insert(iter.key());
    if (IsDownloadDeltaField(iter.key())) {
      const base::Value* old_value = NULL;
      if (!data->json().HasKey(iter.key()) ||
          (data->json().Get(iter.key(), &old_value) &&
           !iter.value().Equals(old_value))) {
        delta->Set(iter.key() + ".current", iter.value().DeepCopy());
        if (old_value)
          delta->Set(iter.key() + ".previous", old_value->DeepCopy());
        changed = true;
      }
    }
  }

  // If a field was in the previous json but is not in the new json, set the
  // difference in |delta|.
  for (base::DictionaryValue::Iterator iter(data->json());
       !iter.IsAtEnd(); iter.Advance()) {
    if ((new_fields.find(iter.key()) == new_fields.end()) &&
        IsDownloadDeltaField(iter.key())) {
      // estimatedEndTime disappears after completion, but bytesReceived stays.
      delta->Set(iter.key() + ".previous", iter.value().DeepCopy());
      changed = true;
    }
  }

  // Update the OnChangedStat and dispatch the event if something significant
  // changed. Replace the stored json with the new json.
  data->OnItemUpdated();
  if (changed) {
    DispatchEvent(events::kOnDownloadChanged,
                  true,
                  extensions::Event::WillDispatchCallback(),
                  delta.release());
    data->OnChangedFired();
  }
  data->set_json(new_json.Pass());
}

void ExtensionDownloadsEventRouter::OnDownloadRemoved(
    DownloadManager* manager, DownloadItem* download_item) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (download_item->IsTemporary())
    return;
  DispatchEvent(events::kOnDownloadErased,
                true,
                extensions::Event::WillDispatchCallback(),
                base::Value::CreateIntegerValue(download_item->GetId()));
}

void ExtensionDownloadsEventRouter::DispatchEvent(
    const char* event_name,
    bool include_incognito,
    const extensions::Event::WillDispatchCallback& will_dispatch_callback,
    base::Value* arg) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (!extensions::ExtensionSystem::Get(profile_)->event_router())
    return;
  scoped_ptr<base::ListValue> args(new base::ListValue());
  args->Append(arg);
  std::string json_args;
  base::JSONWriter::Write(args.get(), &json_args);
  scoped_ptr<extensions::Event> event(new extensions::Event(
      event_name, args.Pass()));
  // The downloads system wants to share on-record events with off-record
  // extension renderers even in incognito_split_mode because that's how
  // chrome://downloads works. The "restrict_to_profile" mechanism does not
  // anticipate this, so it does not automatically prevent sharing off-record
  // events with on-record extension renderers.
  event->restrict_to_profile =
      (include_incognito && !profile_->IsOffTheRecord()) ? NULL : profile_;
  event->will_dispatch_callback = will_dispatch_callback;
  extensions::ExtensionSystem::Get(profile_)->event_router()->
      BroadcastEvent(event.Pass());
  DownloadsNotificationSource notification_source;
  notification_source.event_name = event_name;
  notification_source.profile = profile_;
  content::Source<DownloadsNotificationSource> content_source(
      &notification_source);
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_EXTENSION_DOWNLOADS_EVENT,
      content_source,
      content::Details<std::string>(&json_args));
}

void ExtensionDownloadsEventRouter::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  switch (type) {
    case chrome::NOTIFICATION_EXTENSION_UNLOADED: {
      extensions::UnloadedExtensionInfo* unloaded =
          content::Details<extensions::UnloadedExtensionInfo>(details).ptr();
      std::set<const extensions::Extension*>::iterator iter =
        shelf_disabling_extensions_.find(unloaded->extension);
      if (iter != shelf_disabling_extensions_.end())
        shelf_disabling_extensions_.erase(iter);
      break;
    }
  }
}
