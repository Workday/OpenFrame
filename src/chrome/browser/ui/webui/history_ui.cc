// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/history_ui.h"

#include <set>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/i18n/rtl.h"
#include "base/i18n/time_formatting.h"
#include "base/memory/singleton.h"
#include "base/message_loop/message_loop.h"
#include "base/metrics/histogram.h"
#include "base/prefs/pref_service.h"
#include "base/strings/string16.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/bookmarks/bookmark_utils.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/history/history_notifications.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/history/history_types.h"
#include "chrome/browser/history/web_history_service.h"
#include "chrome/browser/history/web_history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search/search.h"
#include "chrome/browser/sync/glue/device_info.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "chrome/browser/sync/profile_sync_service_factory.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/webui/favicon_source.h"
#include "chrome/browser/ui/webui/metrics_handler.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/url_data_source.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "grit/browser_resources.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "net/base/escape.h"
#include "sync/protocol/history_delete_directive_specifics.pb.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/l10n/time_format.h"
#include "ui/base/resource/resource_bundle.h"

#if defined(ENABLE_MANAGED_USERS)
#include "chrome/browser/managed_mode/managed_mode_navigation_observer.h"
#include "chrome/browser/managed_mode/managed_mode_url_filter.h"
#include "chrome/browser/managed_mode/managed_user_service.h"
#include "chrome/browser/managed_mode/managed_user_service_factory.h"
#endif

#if defined(OS_ANDROID)
#include "chrome/browser/ui/android/tab_model/tab_model.h"
#include "chrome/browser/ui/android/tab_model/tab_model_list.h"
#endif

#if !defined(OS_ANDROID) && !defined(OS_IOS)
#include "chrome/browser/ui/webui/ntp/foreign_session_handler.h"
#include "chrome/browser/ui/webui/ntp/ntp_login_handler.h"
#endif

static const char kStringsJsFile[] = "strings.js";
static const char kHistoryJsFile[] = "history.js";
static const char kOtherDevicesJsFile[] = "other_devices.js";

// The amount of time to wait for a response from the WebHistoryService.
static const int kWebHistoryTimeoutSeconds = 3;

namespace {

// Buckets for UMA histograms.
enum WebHistoryQueryBuckets {
  WEB_HISTORY_QUERY_FAILED = 0,
  WEB_HISTORY_QUERY_SUCCEEDED,
  WEB_HISTORY_QUERY_TIMED_OUT,
  NUM_WEB_HISTORY_QUERY_BUCKETS
};

#if defined(OS_MACOSX)
const char kIncognitoModeShortcut[] = "("
    "\xE2\x87\xA7"  // Shift symbol (U+21E7 'UPWARDS WHITE ARROW').
    "\xE2\x8C\x98"  // Command symbol (U+2318 'PLACE OF INTEREST SIGN').
    "N)";
#elif defined(OS_WIN)
const char kIncognitoModeShortcut[] = "(Ctrl+Shift+N)";
#else
const char kIncognitoModeShortcut[] = "(Shift+Ctrl+N)";
#endif

// Identifiers for the type of device from which a history entry originated.
static const char kDeviceTypeLaptop[] = "laptop";
static const char kDeviceTypePhone[] = "phone";
static const char kDeviceTypeTablet[] = "tablet";

content::WebUIDataSource* CreateHistoryUIHTMLSource(Profile* profile) {
  PrefService* prefs = profile->GetPrefs();

  content::WebUIDataSource* source =
      content::WebUIDataSource::Create(chrome::kChromeUIHistoryFrameHost);
  source->AddBoolean("isUserSignedIn",
      !prefs->GetString(prefs::kGoogleServicesUsername).empty());
  source->AddLocalizedString("collapseSessionMenuItemText",
      IDS_NEW_TAB_OTHER_SESSIONS_COLLAPSE_SESSION);
  source->AddLocalizedString("expandSessionMenuItemText",
      IDS_NEW_TAB_OTHER_SESSIONS_EXPAND_SESSION);
  source->AddLocalizedString("restoreSessionMenuItemText",
      IDS_NEW_TAB_OTHER_SESSIONS_OPEN_ALL);
  source->AddLocalizedString("xMore", IDS_OTHER_DEVICES_X_MORE);
  source->AddLocalizedString("loading", IDS_HISTORY_LOADING);
  source->AddLocalizedString("title", IDS_HISTORY_TITLE);
  source->AddLocalizedString("newest", IDS_HISTORY_NEWEST);
  source->AddLocalizedString("newer", IDS_HISTORY_NEWER);
  source->AddLocalizedString("older", IDS_HISTORY_OLDER);
  source->AddLocalizedString("searchResultsFor", IDS_HISTORY_SEARCHRESULTSFOR);
  source->AddLocalizedString("history", IDS_HISTORY_BROWSERESULTS);
  source->AddLocalizedString("cont", IDS_HISTORY_CONTINUED);
  source->AddLocalizedString("searchButton", IDS_HISTORY_SEARCH_BUTTON);
  source->AddLocalizedString("noSearchResults", IDS_HISTORY_NO_SEARCH_RESULTS);
  source->AddLocalizedString("noResults", IDS_HISTORY_NO_RESULTS);
  source->AddLocalizedString("historyInterval", IDS_HISTORY_INTERVAL);
  source->AddLocalizedString("removeSelected",
                             IDS_HISTORY_REMOVE_SELECTED_ITEMS);
  source->AddLocalizedString("clearAllHistory",
                             IDS_HISTORY_OPEN_CLEAR_BROWSING_DATA_DIALOG);
  source->AddString(
      "deleteWarning",
      l10n_util::GetStringFUTF16(IDS_HISTORY_DELETE_PRIOR_VISITS_WARNING,
                                 UTF8ToUTF16(kIncognitoModeShortcut)));
  source->AddLocalizedString("actionMenuDescription",
                             IDS_HISTORY_ACTION_MENU_DESCRIPTION);
  source->AddLocalizedString("removeFromHistory", IDS_HISTORY_REMOVE_PAGE);
  source->AddLocalizedString("moreFromSite", IDS_HISTORY_MORE_FROM_SITE);
  source->AddLocalizedString("groupByDomainLabel", IDS_GROUP_BY_DOMAIN_LABEL);
  source->AddLocalizedString("rangeLabel", IDS_HISTORY_RANGE_LABEL);
  source->AddLocalizedString("rangeAllTime", IDS_HISTORY_RANGE_ALL_TIME);
  source->AddLocalizedString("rangeWeek", IDS_HISTORY_RANGE_WEEK);
  source->AddLocalizedString("rangeMonth", IDS_HISTORY_RANGE_MONTH);
  source->AddLocalizedString("rangeToday", IDS_HISTORY_RANGE_TODAY);
  source->AddLocalizedString("rangeNext", IDS_HISTORY_RANGE_NEXT);
  source->AddLocalizedString("rangePrevious", IDS_HISTORY_RANGE_PREVIOUS);
  source->AddLocalizedString("numberVisits", IDS_HISTORY_NUMBER_VISITS);
  source->AddLocalizedString("filterAllowed", IDS_HISTORY_FILTER_ALLOWED);
  source->AddLocalizedString("filterBlocked", IDS_HISTORY_FILTER_BLOCKED);
  source->AddLocalizedString("inContentPack", IDS_HISTORY_IN_CONTENT_PACK);
  source->AddLocalizedString("allowItems", IDS_HISTORY_FILTER_ALLOW_ITEMS);
  source->AddLocalizedString("blockItems", IDS_HISTORY_FILTER_BLOCK_ITEMS);
  source->AddLocalizedString("lockButton", IDS_HISTORY_LOCK_BUTTON);
  source->AddLocalizedString("blockedVisitText",
                             IDS_HISTORY_BLOCKED_VISIT_TEXT);
  source->AddLocalizedString("unlockButton", IDS_HISTORY_UNLOCK_BUTTON);
  source->AddLocalizedString("hasSyncedResults",
                             IDS_HISTORY_HAS_SYNCED_RESULTS);
  source->AddLocalizedString("noSyncedResults", IDS_HISTORY_NO_SYNCED_RESULTS);
  source->AddLocalizedString("cancel", IDS_CANCEL);
  source->AddLocalizedString("deleteConfirm",
                             IDS_HISTORY_DELETE_PRIOR_VISITS_CONFIRM_BUTTON);
  source->AddBoolean("isFullHistorySyncEnabled",
                     WebHistoryServiceFactory::GetForProfile(profile) != NULL);
  source->AddBoolean("groupByDomain",
      CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kHistoryEnableGroupByDomain));
  source->AddBoolean("allowDeletingHistory",
                     prefs->GetBoolean(prefs::kAllowDeletingBrowserHistory));
  source->AddBoolean("isInstantExtendedApiEnabled",
                     chrome::IsInstantExtendedAPIEnabled());

  source->SetJsonPath(kStringsJsFile);
  source->AddResourcePath(kHistoryJsFile, IDR_HISTORY_JS);
  source->AddResourcePath(kOtherDevicesJsFile, IDR_OTHER_DEVICES_JS);
  source->SetDefaultResource(IDR_HISTORY_HTML);
  source->SetUseJsonJSFormatV2();
  source->DisableDenyXFrameOptions();
  source->AddBoolean("isManagedProfile", profile->IsManaged());
  source->AddBoolean("showDeleteVisitUI", !profile->IsManaged());

  return source;
}

// Returns a localized version of |visit_time| including a relative
// indicator (e.g. today, yesterday).
string16 getRelativeDateLocalized(const base::Time& visit_time) {
  base::Time midnight = base::Time::Now().LocalMidnight();
  string16 date_str = ui::TimeFormat::RelativeDate(visit_time, &midnight);
  if (date_str.empty()) {
    date_str = base::TimeFormatFriendlyDate(visit_time);
  } else {
    date_str = l10n_util::GetStringFUTF16(
        IDS_HISTORY_DATE_WITH_RELATIVE_TIME,
        date_str,
        base::TimeFormatFriendlyDate(visit_time));
  }
  return date_str;
}


// Sets the correct year when substracting months from a date.
void normalizeMonths(base::Time::Exploded* exploded) {
  // Decrease a year at a time until we have a proper date.
  while (exploded->month < 1) {
    exploded->month += 12;
    exploded->year--;
  }
}

// Returns the URL of a query result value.
bool GetResultTimeAndUrl(Value* result, base::Time* time, string16* url) {
  DictionaryValue* result_dict;
  double timestamp;

  if (result->GetAsDictionary(&result_dict) &&
      result_dict->GetDouble("time", &timestamp) &&
      result_dict->GetString("url", url)) {
    *time = base::Time::FromJsTime(timestamp);
    return true;
  }
  return false;
}

// Returns true if |entry| represents a local visit that had no corresponding
// visit on the server.
bool IsLocalOnlyResult(const BrowsingHistoryHandler::HistoryEntry& entry) {
  return entry.entry_type == BrowsingHistoryHandler::HistoryEntry::LOCAL_ENTRY;
}

// Gets the name and type of a device for the given sync client ID.
// |name| and |type| are out parameters.
void GetDeviceNameAndType(const ProfileSyncService* sync_service,
                          const std::string& client_id,
                          std::string* name,
                          std::string* type) {
  if (sync_service && sync_service->sync_initialized()) {
    scoped_ptr<browser_sync::DeviceInfo> device_info =
        sync_service->GetDeviceInfo(client_id);
    if (device_info.get()) {
      *name = device_info->client_name();
      switch (device_info->device_type()) {
        case sync_pb::SyncEnums::TYPE_PHONE:
          *type = kDeviceTypePhone;
          break;
        case sync_pb::SyncEnums::TYPE_TABLET:
          *type = kDeviceTypeTablet;
          break;
        default:
          *type = kDeviceTypeLaptop;
      }
      return;
    }
  } else {
    NOTREACHED() << "Got a remote history entry but no ProfileSyncService.";
  }
  *name = l10n_util::GetStringUTF8(IDS_HISTORY_UNKNOWN_DEVICE);
  *type = kDeviceTypeLaptop;
}

}  // namespace

////////////////////////////////////////////////////////////////////////////////
//
// BrowsingHistoryHandler
//
////////////////////////////////////////////////////////////////////////////////

BrowsingHistoryHandler::HistoryEntry::HistoryEntry(
    BrowsingHistoryHandler::HistoryEntry::EntryType entry_type,
    const GURL& url, const string16& title, base::Time time,
    const std::string& client_id, bool is_search_result,
    const string16& snippet, bool blocked_visit) {
  this->entry_type = entry_type;
  this->url = url;
  this->title = title;
  this->time = time;
  this->client_id = client_id;
  all_timestamps.insert(time.ToInternalValue());
  this->is_search_result = is_search_result;
  this->snippet = snippet;
  this->blocked_visit = blocked_visit;
}

BrowsingHistoryHandler::HistoryEntry::HistoryEntry()
    : entry_type(EMPTY_ENTRY), is_search_result(false), blocked_visit(false) {
}

BrowsingHistoryHandler::HistoryEntry::~HistoryEntry() {
}

void BrowsingHistoryHandler::HistoryEntry::SetUrlAndTitle(
    DictionaryValue* result) const {
  result->SetString("url", url.spec());

  bool using_url_as_the_title = false;
  string16 title_to_set(title);
  if (title.empty()) {
    using_url_as_the_title = true;
    title_to_set = UTF8ToUTF16(url.spec());
  }

  // Since the title can contain BiDi text, we need to mark the text as either
  // RTL or LTR, depending on the characters in the string. If we use the URL
  // as the title, we mark the title as LTR since URLs are always treated as
  // left to right strings.
  if (base::i18n::IsRTL()) {
    if (using_url_as_the_title)
      base::i18n::WrapStringWithLTRFormatting(&title_to_set);
    else
      base::i18n::AdjustStringForLocaleDirection(&title_to_set);
  }
  result->SetString("title", title_to_set);
}

scoped_ptr<DictionaryValue> BrowsingHistoryHandler::HistoryEntry::ToValue(
    BookmarkModel* bookmark_model,
    ManagedUserService* managed_user_service,
    const ProfileSyncService* sync_service) const {
  scoped_ptr<DictionaryValue> result(new DictionaryValue());
  SetUrlAndTitle(result.get());
  result->SetDouble("time", time.ToJsTime());

  // Pass the timestamps in a list.
  scoped_ptr<ListValue> timestamps(new ListValue);
  for (std::set<int64>::const_iterator it = all_timestamps.begin();
       it != all_timestamps.end(); ++it) {
    timestamps->AppendDouble(base::Time::FromInternalValue(*it).ToJsTime());
  }
  result->Set("allTimestamps", timestamps.release());

  // Always pass the short date since it is needed both in the search and in
  // the monthly view.
  result->SetString("dateShort", base::TimeFormatShortDate(time));

  // Only pass in the strings we need (search results need a shortdate
  // and snippet, browse results need day and time information).
  if (is_search_result) {
    result->SetString("snippet", snippet);
  } else {
    base::Time midnight = base::Time::Now().LocalMidnight();
    string16 date_str = ui::TimeFormat::RelativeDate(time, &midnight);
    if (date_str.empty()) {
      date_str = base::TimeFormatFriendlyDate(time);
    } else {
      date_str = l10n_util::GetStringFUTF16(
          IDS_HISTORY_DATE_WITH_RELATIVE_TIME,
          date_str,
          base::TimeFormatFriendlyDate(time));
    }
    result->SetString("dateRelativeDay", date_str);
    result->SetString("dateTimeOfDay", base::TimeFormatTimeOfDay(time));
  }
  result->SetBoolean("starred", bookmark_model->IsBookmarked(url));

  std::string device_name;
  std::string device_type;
  if (!client_id.empty())
    GetDeviceNameAndType(sync_service, client_id, &device_name, &device_type);
  result->SetString("deviceName", device_name);
  result->SetString("deviceType", device_type);

#if defined(ENABLE_MANAGED_USERS)
  if (managed_user_service) {
    const ManagedModeURLFilter* url_filter =
        managed_user_service->GetURLFilterForUIThread();
    int filtering_behavior =
        url_filter->GetFilteringBehaviorForURL(url.GetWithEmptyPath());
    result->SetInteger("hostFilteringBehavior", filtering_behavior);

    result->SetBoolean("blockedVisit", blocked_visit);
  }
#endif

  return result.Pass();
}

bool BrowsingHistoryHandler::HistoryEntry::SortByTimeDescending(
    const BrowsingHistoryHandler::HistoryEntry& entry1,
    const BrowsingHistoryHandler::HistoryEntry& entry2) {
  return entry1.time > entry2.time;
}

BrowsingHistoryHandler::BrowsingHistoryHandler() {}

BrowsingHistoryHandler::~BrowsingHistoryHandler() {
  history_request_consumer_.CancelAllRequests();
  web_history_request_.reset();
}

void BrowsingHistoryHandler::RegisterMessages() {
  // Create our favicon data source.
  Profile* profile = Profile::FromWebUI(web_ui());
  content::URLDataSource::Add(
      profile, new FaviconSource(profile, FaviconSource::ANY));

  // Get notifications when history is cleared.
  registrar_.Add(this, chrome::NOTIFICATION_HISTORY_URLS_DELETED,
      content::Source<Profile>(profile->GetOriginalProfile()));

  web_ui()->RegisterMessageCallback("queryHistory",
      base::Bind(&BrowsingHistoryHandler::HandleQueryHistory,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("removeVisits",
      base::Bind(&BrowsingHistoryHandler::HandleRemoveVisits,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("clearBrowsingData",
      base::Bind(&BrowsingHistoryHandler::HandleClearBrowsingData,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("removeBookmark",
      base::Bind(&BrowsingHistoryHandler::HandleRemoveBookmark,
                 base::Unretained(this)));
}

bool BrowsingHistoryHandler::ExtractIntegerValueAtIndex(const ListValue* value,
                                                        int index,
                                                        int* out_int) {
  double double_value;
  if (value->GetDouble(index, &double_value)) {
    *out_int = static_cast<int>(double_value);
    return true;
  }
  NOTREACHED();
  return false;
}

void BrowsingHistoryHandler::WebHistoryTimeout() {
  // TODO(dubroy): Communicate the failure to the front end.
  if (!history_request_consumer_.HasPendingRequests())
    ReturnResultsToFrontEnd();

  UMA_HISTOGRAM_ENUMERATION(
      "WebHistory.QueryCompletion",
      WEB_HISTORY_QUERY_TIMED_OUT, NUM_WEB_HISTORY_QUERY_BUCKETS);
}

void BrowsingHistoryHandler::QueryHistory(
    string16 search_text, const history::QueryOptions& options) {
  Profile* profile = Profile::FromWebUI(web_ui());

  // Anything in-flight is invalid.
  history_request_consumer_.CancelAllRequests();
  web_history_request_.reset();

  query_results_.clear();
  results_info_value_.Clear();

  HistoryService* hs = HistoryServiceFactory::GetForProfile(
      profile, Profile::EXPLICIT_ACCESS);
  hs->QueryHistory(search_text,
      options,
      &history_request_consumer_,
      base::Bind(&BrowsingHistoryHandler::QueryComplete,
                 base::Unretained(this), search_text, options));

  history::WebHistoryService* web_history =
      WebHistoryServiceFactory::GetForProfile(profile);
  if (web_history) {
    web_history_query_results_.clear();
    web_history_request_ = web_history->QueryHistory(
        search_text,
        options,
        base::Bind(&BrowsingHistoryHandler::WebHistoryQueryComplete,
                   base::Unretained(this),
                   search_text, options,
                   base::TimeTicks::Now()));
    // Start a timer so we know when to give up.
    web_history_timer_.Start(
        FROM_HERE, base::TimeDelta::FromSeconds(kWebHistoryTimeoutSeconds),
        this, &BrowsingHistoryHandler::WebHistoryTimeout);

    // Set this to false until the results actually arrive.
    results_info_value_.SetBoolean("hasSyncedResults", false);
  }
}

void BrowsingHistoryHandler::HandleQueryHistory(const ListValue* args) {
  history::QueryOptions options;

  // Parse the arguments from JavaScript. There are five required arguments:
  // - the text to search for (may be empty)
  // - the offset from which the search should start (in multiples of week or
  //   month, set by the next argument).
  // - the range (BrowsingHistoryHandler::Range) Enum value that sets the range
  //   of the query.
  // - the end time for the query. Only results older than this time will be
  //   returned.
  // - the maximum number of results to return (may be 0, meaning that there
  //   is no maximum).
  string16 search_text = ExtractStringValue(args);
  int offset;
  if (!args->GetInteger(1, &offset)) {
    NOTREACHED() << "Failed to convert argument 1. ";
    return;
  }
  int range;
  if (!args->GetInteger(2, &range)) {
    NOTREACHED() << "Failed to convert argument 2. ";
    return;
  }

  if (range == BrowsingHistoryHandler::MONTH)
    SetQueryTimeInMonths(offset, &options);
  else if (range == BrowsingHistoryHandler::WEEK)
    SetQueryTimeInWeeks(offset, &options);

  double end_time;
  if (!args->GetDouble(3, &end_time)) {
    NOTREACHED() << "Failed to convert argument 3. ";
    return;
  }
  if (end_time)
    options.end_time = base::Time::FromJsTime(end_time);

  if (!ExtractIntegerValueAtIndex(args, 4, &options.max_count)) {
    NOTREACHED() << "Failed to convert argument 4.";
    return;
  }

  options.duplicate_policy = history::QueryOptions::REMOVE_DUPLICATES_PER_DAY;
  QueryHistory(search_text, options);
}

void BrowsingHistoryHandler::HandleRemoveVisits(const ListValue* args) {
  Profile* profile = Profile::FromWebUI(web_ui());
  if (delete_task_tracker_.HasTrackedTasks() ||
      !profile->GetPrefs()->GetBoolean(prefs::kAllowDeletingBrowserHistory)) {
    web_ui()->CallJavascriptFunction("deleteFailed");
    return;
  }

  HistoryService* history_service =
      HistoryServiceFactory::GetForProfile(profile, Profile::EXPLICIT_ACCESS);
  history::WebHistoryService* web_history =
      WebHistoryServiceFactory::GetForProfile(profile);

  base::Time now = base::Time::Now();
  std::vector<history::ExpireHistoryArgs> expire_list;
  expire_list.reserve(args->GetSize());

  DCHECK(urls_to_be_deleted_.empty());
  for (ListValue::const_iterator it = args->begin(); it != args->end(); ++it) {
    DictionaryValue* deletion = NULL;
    string16 url;
    ListValue* timestamps = NULL;

    // Each argument is a dictionary with properties "url" and "timestamps".
    if (!((*it)->GetAsDictionary(&deletion) &&
        deletion->GetString("url", &url) &&
        deletion->GetList("timestamps", &timestamps))) {
      NOTREACHED() << "Unable to extract arguments";
      return;
    }
    DCHECK(timestamps->GetSize() > 0);

    // In order to ensure that visits will be deleted from the server and other
    // clients (even if they are offline), create a sync delete directive for
    // each visit to be deleted.
    sync_pb::HistoryDeleteDirectiveSpecifics delete_directive;
    sync_pb::GlobalIdDirective* global_id_directive =
        delete_directive.mutable_global_id_directive();

    double timestamp;
    history::ExpireHistoryArgs* expire_args = NULL;
    for (ListValue::const_iterator ts_iterator = timestamps->begin();
         ts_iterator != timestamps->end(); ++ts_iterator) {
      if (!(*ts_iterator)->GetAsDouble(&timestamp)) {
        NOTREACHED() << "Unable to extract visit timestamp.";
        continue;
      }
      base::Time visit_time = base::Time::FromJsTime(timestamp);
      if (!expire_args) {
        GURL gurl(url);
        expire_list.resize(expire_list.size() + 1);
        expire_args = &expire_list.back();
        expire_args->SetTimeRangeForOneDay(visit_time);
        expire_args->urls.insert(gurl);
        urls_to_be_deleted_.insert(gurl);
      }
      // The local visit time is treated as a global ID for the visit.
      global_id_directive->add_global_id(visit_time.ToInternalValue());
    }

    // Set the start and end time in microseconds since the Unix epoch.
    global_id_directive->set_start_time_usec(
        (expire_args->begin_time - base::Time::UnixEpoch()).InMicroseconds());

    // Delete directives shouldn't have an end time in the future.
    // TODO(dubroy): Use sane time (crbug.com/146090) here when it's ready.
    base::Time end_time = std::min(expire_args->end_time, now);

    // -1 because end time in delete directives is inclusive.
    global_id_directive->set_end_time_usec(
        (end_time - base::Time::UnixEpoch()).InMicroseconds() - 1);

    // TODO(dubroy): Figure out the proper way to handle an error here.
    if (web_history)
      history_service->ProcessLocalDeleteDirective(delete_directive);
  }

  history_service->ExpireHistory(
      expire_list,
      base::Bind(&BrowsingHistoryHandler::RemoveComplete,
                 base::Unretained(this)),
      &delete_task_tracker_);

  if (web_history) {
    web_history_delete_request_ = web_history->ExpireHistory(
        expire_list,
        base::Bind(&BrowsingHistoryHandler::RemoveWebHistoryComplete,
                   base::Unretained(this)));
  }
}

void BrowsingHistoryHandler::HandleClearBrowsingData(const ListValue* args) {
#if defined(OS_ANDROID)
  Profile* profile = Profile::FromWebUI(web_ui());
  const TabModel* tab_model =
      TabModelList::GetTabModelWithProfile(profile);
  if (tab_model)
    tab_model->OpenClearBrowsingData();
#else
  // TODO(beng): This is an improper direct dependency on Browser. Route this
  // through some sort of delegate.
  Browser* browser = chrome::FindBrowserWithWebContents(
      web_ui()->GetWebContents());
  chrome::ShowClearBrowsingDataDialog(browser);
#endif
}

void BrowsingHistoryHandler::HandleRemoveBookmark(const ListValue* args) {
  string16 url = ExtractStringValue(args);
  Profile* profile = Profile::FromWebUI(web_ui());
  BookmarkModel* model = BookmarkModelFactory::GetForProfile(profile);
  bookmark_utils::RemoveAllBookmarks(model, GURL(url));
}

// static
void BrowsingHistoryHandler::MergeDuplicateResults(
    std::vector<BrowsingHistoryHandler::HistoryEntry>* results) {
  std::vector<BrowsingHistoryHandler::HistoryEntry> new_results;
  // Pre-reserve the size of the new vector. Since we're working with pointers
  // later on not doing this could lead to the vector being resized and to
  // pointers to invalid locations.
  new_results.reserve(results->size());
  // Maps a URL to the most recent entry on a particular day.
  std::map<GURL,BrowsingHistoryHandler::HistoryEntry*>
      current_day_entries;

  // Keeps track of the day that |current_day_urls| is holding the URLs for,
  // in order to handle removing per-day duplicates.
  base::Time current_day_midnight;

  std::sort(
      results->begin(), results->end(), HistoryEntry::SortByTimeDescending);

  for (std::vector<BrowsingHistoryHandler::HistoryEntry>::const_iterator it =
           results->begin(); it != results->end(); ++it) {
    // Reset the list of found URLs when a visit from a new day is encountered.
    if (current_day_midnight != it->time.LocalMidnight()) {
      current_day_entries.clear();
      current_day_midnight = it->time.LocalMidnight();
    }

    // Keep this visit if it's the first visit to this URL on the current day.
    if (current_day_entries.count(it->url) == 0) {
      new_results.push_back(*it);
      current_day_entries[it->url] = &new_results.back();
    } else {
      // Keep track of the timestamps of all visits to the URL on the same day.
      BrowsingHistoryHandler::HistoryEntry* entry =
          current_day_entries[it->url];
      entry->all_timestamps.insert(
          it->all_timestamps.begin(), it->all_timestamps.end());

      if (entry->entry_type != it->entry_type) {
        entry->entry_type =
            BrowsingHistoryHandler::HistoryEntry::COMBINED_ENTRY;
      }
    }
  }
  results->swap(new_results);
}

void BrowsingHistoryHandler::ReturnResultsToFrontEnd() {
  Profile* profile = Profile::FromWebUI(web_ui());
  BookmarkModel* bookmark_model = BookmarkModelFactory::GetForProfile(profile);
  ManagedUserService* managed_user_service = NULL;
#if defined(ENABLE_MANAGED_USERS)
  if (profile->IsManaged())
    managed_user_service = ManagedUserServiceFactory::GetForProfile(profile);
#endif
  ProfileSyncService* sync_service =
      ProfileSyncServiceFactory::GetInstance()->GetForProfile(profile);

  // Combine the local and remote results into |query_results_|, and remove
  // any duplicates.
  if (!web_history_query_results_.empty()) {
    int local_result_count = query_results_.size();
    query_results_.insert(query_results_.end(),
                          web_history_query_results_.begin(),
                          web_history_query_results_.end());
    MergeDuplicateResults(&query_results_);

    if (local_result_count) {
      // In the best case, we expect that all local results are duplicated on
      // the server. Keep track of how many are missing.
      int missing_count = std::count_if(
          query_results_.begin(), query_results_.end(), IsLocalOnlyResult);
      UMA_HISTOGRAM_PERCENTAGE("WebHistory.LocalResultMissingOnServer",
                               missing_count * 100.0 / local_result_count);
    }
  }

  // Convert the result vector into a ListValue.
  ListValue results_value;
  for (std::vector<BrowsingHistoryHandler::HistoryEntry>::iterator it =
           query_results_.begin(); it != query_results_.end(); ++it) {
    scoped_ptr<base::Value> value(
        it->ToValue(bookmark_model, managed_user_service, sync_service));
    results_value.Append(value.release());
  }

  web_ui()->CallJavascriptFunction(
      "historyResult", results_info_value_, results_value);
  results_info_value_.Clear();
  query_results_.clear();
  web_history_query_results_.clear();
}

void BrowsingHistoryHandler::QueryComplete(
    const string16& search_text,
    const history::QueryOptions& options,
    HistoryService::Handle request_handle,
    history::QueryResults* results) {
  DCHECK_EQ(0U, query_results_.size());
  query_results_.reserve(results->size());

  for (size_t i = 0; i < results->size(); ++i) {
    history::URLResult const &page = (*results)[i];
    // TODO(dubroy): Use sane time (crbug.com/146090) here when it's ready.
    query_results_.push_back(
        HistoryEntry(
            HistoryEntry::LOCAL_ENTRY,
            page.url(),
            page.title(),
            page.visit_time(),
            std::string(),
            !search_text.empty(),
            page.snippet().text(),
            page.blocked_visit()));
  }

  results_info_value_.SetString("term", search_text);
  results_info_value_.SetBoolean("finished", results->reached_beginning());

  // Add the specific dates that were searched to display them.
  // TODO(sergiu): Put today if the start is in the future.
  results_info_value_.SetString("queryStartTime",
                                getRelativeDateLocalized(options.begin_time));
  if (!options.end_time.is_null()) {
    results_info_value_.SetString("queryEndTime",
        getRelativeDateLocalized(options.end_time -
                                 base::TimeDelta::FromDays(1)));
  } else {
    results_info_value_.SetString("queryEndTime",
        getRelativeDateLocalized(base::Time::Now()));
  }
  if (!web_history_timer_.IsRunning())
    ReturnResultsToFrontEnd();
}

void BrowsingHistoryHandler::WebHistoryQueryComplete(
    const string16& search_text,
    const history::QueryOptions& options,
    base::TimeTicks start_time,
    history::WebHistoryService::Request* request,
    const DictionaryValue* results_value) {
  base::TimeDelta delta = base::TimeTicks::Now() - start_time;
  UMA_HISTOGRAM_TIMES("WebHistory.ResponseTime", delta);

  // If the response came in too late, do nothing.
  // TODO(dubroy): Maybe show a banner, and prompt the user to reload?
  if (!web_history_timer_.IsRunning())
    return;
  web_history_timer_.Stop();

  UMA_HISTOGRAM_ENUMERATION(
      "WebHistory.QueryCompletion",
      results_value ? WEB_HISTORY_QUERY_SUCCEEDED : WEB_HISTORY_QUERY_FAILED,
      NUM_WEB_HISTORY_QUERY_BUCKETS);

  DCHECK_EQ(0U, web_history_query_results_.size());
  const ListValue* events = NULL;
  if (results_value && results_value->GetList("event", &events)) {
    web_history_query_results_.reserve(events->GetSize());
    for (unsigned int i = 0; i < events->GetSize(); ++i) {
      const DictionaryValue* event = NULL;
      const DictionaryValue* result = NULL;
      const ListValue* results = NULL;
      const ListValue* ids = NULL;
      string16 url;
      string16 title;
      base::Time visit_time;

      if (!(events->GetDictionary(i, &event) &&
          event->GetList("result", &results) &&
          results->GetDictionary(0, &result) &&
          result->GetString("url", &url) &&
          result->GetList("id", &ids) &&
          ids->GetSize() > 0)) {
        LOG(WARNING) << "Improperly formed JSON response from history server.";
        continue;
      }
      // Title is optional, so the return value is ignored here.
      result->GetString("title", &title);

      // Extract the timestamps of all the visits to this URL.
      // They are referred to as "IDs" by the server.
      for (int j = 0; j < static_cast<int>(ids->GetSize()); ++j) {
        const DictionaryValue* id = NULL;
        std::string timestamp_string;
        int64 timestamp_usec;

        if (!(ids->GetDictionary(j, &id) &&
            id->GetString("timestamp_usec", &timestamp_string) &&
              base::StringToInt64(timestamp_string, &timestamp_usec))) {
          NOTREACHED() << "Unable to extract timestamp.";
          continue;
        }
        // The timestamp on the server is a Unix time.
        base::Time time = base::Time::UnixEpoch() +
                          base::TimeDelta::FromMicroseconds(timestamp_usec);

        // Get the ID of the client that this visit came from.
        std::string client_id;
        id->GetString("client_id", &client_id);

        web_history_query_results_.push_back(
            HistoryEntry(
                HistoryEntry::REMOTE_ENTRY,
                GURL(url),
                title,
                time,
                client_id,
                !search_text.empty(),
                string16(),
                /* blocked_visit */ false));
      }
    }
  } else if (results_value) {
    NOTREACHED() << "Failed to parse JSON response.";
  }
  results_info_value_.SetBoolean("hasSyncedResults", results_value != NULL);
  if (!history_request_consumer_.HasPendingRequests())
    ReturnResultsToFrontEnd();
}

void BrowsingHistoryHandler::RemoveComplete() {
  urls_to_be_deleted_.clear();

  // Notify the page that the deletion request is complete, but only if a web
  // history delete request is not still pending.
  if (!(web_history_delete_request_.get() &&
        web_history_delete_request_->is_pending())) {
    web_ui()->CallJavascriptFunction("deleteComplete");
  }
}

void BrowsingHistoryHandler::RemoveWebHistoryComplete(
    history::WebHistoryService::Request* request, bool success) {
  // TODO(dubroy): Should we handle failure somehow? Delete directives will
  // ensure that the visits are eventually deleted, so maybe it's not necessary.
  if (!delete_task_tracker_.HasTrackedTasks())
    RemoveComplete();
}

void BrowsingHistoryHandler::SetQueryTimeInWeeks(
    int offset, history::QueryOptions* options) {
  // LocalMidnight returns the beginning of the current day so get the
  // beginning of the next one.
  base::Time midnight = base::Time::Now().LocalMidnight() +
                              base::TimeDelta::FromDays(1);
  options->end_time = midnight -
      base::TimeDelta::FromDays(7 * offset);
  options->begin_time = midnight -
      base::TimeDelta::FromDays(7 * (offset + 1));
}

void BrowsingHistoryHandler::SetQueryTimeInMonths(
    int offset, history::QueryOptions* options) {
  // Configure the begin point of the search to the start of the
  // current month.
  base::Time::Exploded exploded;
  base::Time::Now().LocalMidnight().LocalExplode(&exploded);
  exploded.day_of_month = 1;

  if (offset == 0) {
    options->begin_time = base::Time::FromLocalExploded(exploded);

    // Set the end time of this first search to null (which will
    // show results from the future, should the user's clock have
    // been set incorrectly).
    options->end_time = base::Time();
  } else {
    // Go back |offset| months in the past. The end time is not inclusive, so
    // use the first day of the |offset| - 1 and |offset| months (e.g. for
    // the last month, |offset| = 1, use the first days of the last month and
    // the current month.
    exploded.month -= offset - 1;
    // Set the correct year.
    normalizeMonths(&exploded);
    options->end_time = base::Time::FromLocalExploded(exploded);

    exploded.month -= 1;
    // Set the correct year
    normalizeMonths(&exploded);
    options->begin_time = base::Time::FromLocalExploded(exploded);
  }
}

// Helper function for Observe that determines if there are any differences
// between the URLs noticed for deletion and the ones we are expecting.
static bool DeletionsDiffer(const history::URLRows& deleted_rows,
                            const std::set<GURL>& urls_to_be_deleted) {
  if (deleted_rows.size() != urls_to_be_deleted.size())
    return true;
  for (history::URLRows::const_iterator i = deleted_rows.begin();
       i != deleted_rows.end(); ++i) {
    if (urls_to_be_deleted.find(i->url()) == urls_to_be_deleted.end())
      return true;
  }
  return false;
}

void BrowsingHistoryHandler::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  if (type != chrome::NOTIFICATION_HISTORY_URLS_DELETED) {
    NOTREACHED();
    return;
  }
  history::URLsDeletedDetails* deletedDetails =
      content::Details<history::URLsDeletedDetails>(details).ptr();
  if (deletedDetails->all_history ||
      DeletionsDiffer(deletedDetails->rows, urls_to_be_deleted_))
    web_ui()->CallJavascriptFunction("historyDeleted");
}

////////////////////////////////////////////////////////////////////////////////
//
// HistoryUI
//
////////////////////////////////////////////////////////////////////////////////

HistoryUI::HistoryUI(content::WebUI* web_ui) : WebUIController(web_ui) {
  web_ui->AddMessageHandler(new BrowsingHistoryHandler());
  web_ui->AddMessageHandler(new MetricsHandler());

// On mobile we deal with foreign sessions differently.
#if !defined(OS_ANDROID) && !defined(OS_IOS)
  if (chrome::IsInstantExtendedAPIEnabled()) {
    web_ui->AddMessageHandler(new browser_sync::ForeignSessionHandler());
    web_ui->AddMessageHandler(new NTPLoginHandler());
  }
#endif

  // Set up the chrome://history-frame/ source.
  Profile* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource::Add(profile, CreateHistoryUIHTMLSource(profile));
}

// static
const GURL HistoryUI::GetHistoryURLWithSearchText(const string16& text) {
  return GURL(std::string(chrome::kChromeUIHistoryURL) + "#q=" +
              net::EscapeQueryParamValue(UTF16ToUTF8(text), true));
}

// static
base::RefCountedMemory* HistoryUI::GetFaviconResourceBytes(
      ui::ScaleFactor scale_factor) {
  return ResourceBundle::GetSharedInstance().
      LoadDataResourceBytesForScale(IDR_HISTORY_FAVICON, scale_factor);
}
