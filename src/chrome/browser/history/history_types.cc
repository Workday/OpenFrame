// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/history/history_types.h"

#include <limits>

#include "base/logging.h"
#include "base/stl_util.h"
#include "chrome/browser/history/page_usage_data.h"

namespace history {

// URLRow ----------------------------------------------------------------------

URLRow::URLRow() {
  Initialize();
}

URLRow::URLRow(const GURL& url) : url_(url) {
  // Initialize will not set the URL, so our initialization above will stay.
  Initialize();
}

URLRow::URLRow(const GURL& url, URLID id) : url_(url) {
  // Initialize will not set the URL, so our initialization above will stay.
  Initialize();
  // Initialize will zero the id_, so set it here.
  id_ = id;
}

URLRow::~URLRow() {
}

URLRow& URLRow::operator=(const URLRow& other) {
  id_ = other.id_;
  url_ = other.url_;
  title_ = other.title_;
  visit_count_ = other.visit_count_;
  typed_count_ = other.typed_count_;
  last_visit_ = other.last_visit_;
  hidden_ = other.hidden_;
  return *this;
}

void URLRow::Swap(URLRow* other) {
  std::swap(id_, other->id_);
  url_.Swap(&other->url_);
  title_.swap(other->title_);
  std::swap(visit_count_, other->visit_count_);
  std::swap(typed_count_, other->typed_count_);
  std::swap(last_visit_, other->last_visit_);
  std::swap(hidden_, other->hidden_);
}

void URLRow::Initialize() {
  id_ = 0;
  visit_count_ = 0;
  typed_count_ = 0;
  last_visit_ = base::Time();
  hidden_ = false;
}

// VisitRow --------------------------------------------------------------------

VisitRow::VisitRow()
    : visit_id(0),
      url_id(0),
      referring_visit(0),
      transition(content::PAGE_TRANSITION_LINK),
      segment_id(0) {
}

VisitRow::VisitRow(URLID arg_url_id,
                   base::Time arg_visit_time,
                   VisitID arg_referring_visit,
                   content::PageTransition arg_transition,
                   SegmentID arg_segment_id)
    : visit_id(0),
      url_id(arg_url_id),
      visit_time(arg_visit_time),
      referring_visit(arg_referring_visit),
      transition(arg_transition),
      segment_id(arg_segment_id) {
}

VisitRow::~VisitRow() {
}

// URLResult -------------------------------------------------------------------

URLResult::URLResult()
    : blocked_visit_(false) {
}

URLResult::URLResult(const GURL& url, base::Time visit_time)
    : URLRow(url),
      visit_time_(visit_time),
      blocked_visit_(false) {
}

URLResult::URLResult(const GURL& url,
                     const Snippet::MatchPositions& title_matches)
    : URLRow(url) {
  title_match_positions_ = title_matches;
}
URLResult::URLResult(const URLRow& url_row)
    : URLRow(url_row),
      blocked_visit_(false) {
}

URLResult::~URLResult() {
}

void URLResult::SwapResult(URLResult* other) {
  URLRow::Swap(other);
  std::swap(visit_time_, other->visit_time_);
  snippet_.Swap(&other->snippet_);
  title_match_positions_.swap(other->title_match_positions_);
  std::swap(blocked_visit_, other->blocked_visit_);
}

// static
bool URLResult::CompareVisitTime(const URLResult& lhs, const URLResult& rhs) {
  return lhs.visit_time() > rhs.visit_time();
}

// QueryResults ----------------------------------------------------------------

QueryResults::QueryResults() : reached_beginning_(false) {
}

QueryResults::~QueryResults() {}

const size_t* QueryResults::MatchesForURL(const GURL& url,
                                          size_t* num_matches) const {
  URLToResultIndices::const_iterator found = url_to_results_.find(url);
  if (found == url_to_results_.end()) {
    if (num_matches)
      *num_matches = 0;
    return NULL;
  }

  // All entries in the map should have at least one index, otherwise it
  // shouldn't be in the map.
  DCHECK(!found->second->empty());
  if (num_matches)
    *num_matches = found->second->size();
  return &found->second->front();
}

void QueryResults::Swap(QueryResults* other) {
  std::swap(first_time_searched_, other->first_time_searched_);
  std::swap(reached_beginning_, other->reached_beginning_);
  results_.swap(other->results_);
  url_to_results_.swap(other->url_to_results_);
}

void QueryResults::AppendURLBySwapping(URLResult* result) {
  URLResult* new_result = new URLResult;
  new_result->SwapResult(result);

  results_.push_back(new_result);
  AddURLUsageAtIndex(new_result->url(), results_.size() - 1);
}

void QueryResults::DeleteURL(const GURL& url) {
  // Delete all instances of this URL. We re-query each time since each
  // mutation will cause the indices to change.
  while (const size_t* match_indices = MatchesForURL(url, NULL))
    DeleteRange(*match_indices, *match_indices);
}

void QueryResults::DeleteRange(size_t begin, size_t end) {
  DCHECK(begin <= end && begin < size() && end < size());

  // First delete the pointers in the given range and store all the URLs that
  // were modified. We will delete references to these later.
  std::set<GURL> urls_modified;
  for (size_t i = begin; i <= end; i++) {
    urls_modified.insert(results_[i]->url());
  }

  // Now just delete that range in the vector en masse (the STL ending is
  // exclusive, while ours is inclusive, hence the +1).
  results_.erase(results_.begin() + begin, results_.begin() + end + 1);

  // Delete the indicies referencing the deleted entries.
  for (std::set<GURL>::const_iterator url = urls_modified.begin();
       url != urls_modified.end(); ++url) {
    URLToResultIndices::iterator found = url_to_results_.find(*url);
    if (found == url_to_results_.end()) {
      NOTREACHED();
      continue;
    }

    // Need a signed loop type since we do -- which may take us to -1.
    for (int match = 0; match < static_cast<int>(found->second->size());
         match++) {
      if (found->second[match] >= begin && found->second[match] <= end) {
        // Remove this referece from the list.
        found->second->erase(found->second->begin() + match);
        match--;
      }
    }

    // Clear out an empty lists if we just made one.
    if (found->second->empty())
      url_to_results_.erase(found);
  }

  // Shift all other indices over to account for the removed ones.
  AdjustResultMap(end + 1, std::numeric_limits<size_t>::max(),
                  -static_cast<ptrdiff_t>(end - begin + 1));
}

void QueryResults::AddURLUsageAtIndex(const GURL& url, size_t index) {
  URLToResultIndices::iterator found = url_to_results_.find(url);
  if (found != url_to_results_.end()) {
    // The URL is already in the list, so we can just append the new index.
    found->second->push_back(index);
    return;
  }

  // Need to add a new entry for this URL.
  base::StackVector<size_t, 4> new_list;
  new_list->push_back(index);
  url_to_results_[url] = new_list;
}

void QueryResults::AdjustResultMap(size_t begin, size_t end, ptrdiff_t delta) {
  for (URLToResultIndices::iterator i = url_to_results_.begin();
       i != url_to_results_.end(); ++i) {
    for (size_t match = 0; match < i->second->size(); match++) {
      size_t match_index = i->second[match];
      if (match_index >= begin && match_index <= end)
        i->second[match] += delta;
    }
  }
}

// QueryOptions ----------------------------------------------------------------

QueryOptions::QueryOptions()
    : max_count(0),
      duplicate_policy(QueryOptions::REMOVE_ALL_DUPLICATES) {
}

void QueryOptions::SetRecentDayRange(int days_ago) {
  end_time = base::Time::Now();
  begin_time = end_time - base::TimeDelta::FromDays(days_ago);
}

int64 QueryOptions::EffectiveBeginTime() const {
  return begin_time.ToInternalValue();
}

int64 QueryOptions::EffectiveEndTime() const {
  return end_time.is_null() ?
      std::numeric_limits<int64>::max() : end_time.ToInternalValue();
}

int QueryOptions::EffectiveMaxCount() const {
  return max_count ? max_count : std::numeric_limits<int>::max();
}

// KeywordSearchTermVisit -----------------------------------------------------

KeywordSearchTermVisit::KeywordSearchTermVisit() : visits(0) {}

KeywordSearchTermVisit::~KeywordSearchTermVisit() {}

// KeywordSearchTermRow --------------------------------------------------------

KeywordSearchTermRow::KeywordSearchTermRow() : keyword_id(0), url_id(0) {}

KeywordSearchTermRow::~KeywordSearchTermRow() {}

// MostVisitedURL --------------------------------------------------------------

MostVisitedURL::MostVisitedURL() {}

MostVisitedURL::MostVisitedURL(const GURL& url,
                               const string16& title)
    : url(url),
      title(title) {
}

MostVisitedURL::~MostVisitedURL() {}

// FilteredURL -----------------------------------------------------------------

FilteredURL::FilteredURL() : score(0.0) {}

FilteredURL::FilteredURL(const PageUsageData& page_data)
    : url(page_data.GetURL()),
      title(page_data.GetTitle()),
      score(page_data.GetScore()) {
}

FilteredURL::~FilteredURL() {}

// FilteredURL::ExtendedInfo ---------------------------------------------------

FilteredURL::ExtendedInfo::ExtendedInfo()
    : total_visits(0),
      visits(0),
      duration_opened(0) {
}

// Images ---------------------------------------------------------------------

Images::Images() {}

Images::~Images() {}

// TopSitesDelta --------------------------------------------------------------

TopSitesDelta::TopSitesDelta() {}

TopSitesDelta::~TopSitesDelta() {}

// HistoryAddPageArgs ---------------------------------------------------------

HistoryAddPageArgs::HistoryAddPageArgs()
    : id_scope(NULL),
      page_id(0),
      transition(content::PAGE_TRANSITION_LINK),
      visit_source(SOURCE_BROWSED),
      did_replace_entry(false) {}

HistoryAddPageArgs::HistoryAddPageArgs(
    const GURL& url,
    base::Time time,
    const void* id_scope,
    int32 page_id,
    const GURL& referrer,
    const history::RedirectList& redirects,
    content::PageTransition transition,
    VisitSource source,
    bool did_replace_entry)
      : url(url),
        time(time),
        id_scope(id_scope),
        page_id(page_id),
        referrer(referrer),
        redirects(redirects),
        transition(transition),
        visit_source(source),
        did_replace_entry(did_replace_entry) {
}

HistoryAddPageArgs::~HistoryAddPageArgs() {}

ThumbnailMigration::ThumbnailMigration() {}

ThumbnailMigration::~ThumbnailMigration() {}

MostVisitedThumbnails::MostVisitedThumbnails() {}

MostVisitedThumbnails::~MostVisitedThumbnails() {}

// Autocomplete thresholds -----------------------------------------------------

const int kLowQualityMatchTypedLimit = 1;
const int kLowQualityMatchVisitLimit = 4;
const int kLowQualityMatchAgeLimitInDays = 3;

base::Time AutocompleteAgeThreshold() {
  return (base::Time::Now() -
          base::TimeDelta::FromDays(kLowQualityMatchAgeLimitInDays));
}

bool RowQualifiesAsSignificant(const URLRow& row,
                               const base::Time& threshold) {
  const base::Time& real_threshold =
      threshold.is_null() ? AutocompleteAgeThreshold() : threshold;
  return (row.typed_count() >= kLowQualityMatchTypedLimit) ||
         (row.visit_count() >= kLowQualityMatchVisitLimit) ||
         (row.last_visit() >= real_threshold);
}

// IconMapping ----------------------------------------------------------------

IconMapping::IconMapping()
    : mapping_id(0),
      icon_id(0),
      icon_type(chrome::INVALID_ICON) {
}

IconMapping::~IconMapping() {}

// FaviconBitmapIDSize ---------------------------------------------------------

FaviconBitmapIDSize::FaviconBitmapIDSize()
    : bitmap_id(0) {
}

FaviconBitmapIDSize::~FaviconBitmapIDSize() {
}

// FaviconBitmap --------------------------------------------------------------

FaviconBitmap::FaviconBitmap()
    : bitmap_id(0),
      icon_id(0) {
}

FaviconBitmap::~FaviconBitmap() {
}

// VisitDatabaseObserver -------------------------------------------------------

VisitDatabaseObserver::~VisitDatabaseObserver() {}

ExpireHistoryArgs::ExpireHistoryArgs() {
}

ExpireHistoryArgs::~ExpireHistoryArgs() {
}

void ExpireHistoryArgs::SetTimeRangeForOneDay(base::Time time) {
  begin_time = time.LocalMidnight();

  // Due to DST, leap seconds, etc., the next day at midnight may be more than
  // 24 hours away, so add 36 hours and round back down to midnight.
  end_time = (begin_time + base::TimeDelta::FromHours(36)).LocalMidnight();
}

}  // namespace history
