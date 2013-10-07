// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/url_blacklist_manager.h"

#include "base/bind.h"
#include "base/files/file_path.h"
#include "base/message_loop/message_loop.h"
#include "base/prefs/pref_service.h"
#include "base/stl_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/common/net/url_fixer_upper.h"
#include "chrome/common/pref_names.h"
#include "components/user_prefs/pref_registry_syncable.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_source.h"
#include "content/public/common/url_constants.h"
#include "google_apis/gaia/gaia_urls.h"
#include "net/base/load_flags.h"
#include "net/base/net_util.h"
#include "net/url_request/url_request.h"
#include "url/gurl.h"

#if !defined(OS_CHROMEOS)
#include "chrome/browser/signin/signin_manager.h"
#endif

using content::BrowserThread;
using extensions::URLMatcher;
using extensions::URLMatcherCondition;
using extensions::URLMatcherConditionFactory;
using extensions::URLMatcherConditionSet;
using extensions::URLMatcherPortFilter;
using extensions::URLMatcherSchemeFilter;

namespace policy {

namespace {

// Maximum filters per policy. Filters over this index are ignored.
const size_t kMaxFiltersPerPolicy = 1000;

const char kServiceLoginAuth[] = "/ServiceLoginAuth";

#if !defined(OS_CHROMEOS)

bool IsSigninFlowURL(const GURL& url) {
  // Whitelist all the signin flow URLs flagged by the SigninManager.
  if (SigninManager::IsWebBasedSigninFlowURL(url))
    return true;

  // Additionally whitelist /ServiceLoginAuth.
  if (url.GetOrigin() != GaiaUrls::GetInstance()->gaia_url().GetOrigin())
    return false;
  return url.path() == kServiceLoginAuth;
}

#endif  // !defined(OS_CHROMEOS)

// A task that builds the blacklist on the FILE thread.
scoped_ptr<URLBlacklist> BuildBlacklist(scoped_ptr<base::ListValue> block,
                                        scoped_ptr<base::ListValue> allow) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));

  scoped_ptr<URLBlacklist> blacklist(new URLBlacklist);
  blacklist->Block(block.get());
  blacklist->Allow(allow.get());
  return blacklist.Pass();
}

}  // namespace

struct URLBlacklist::FilterComponents {
  FilterComponents() : port(0), match_subdomains(true), allow(true) {}
  ~FilterComponents() {}

  std::string scheme;
  std::string host;
  uint16 port;
  std::string path;
  bool match_subdomains;
  bool allow;
};

URLBlacklist::URLBlacklist() : id_(0),
                               url_matcher_(new URLMatcher) {
}

URLBlacklist::~URLBlacklist() {
}

void URLBlacklist::AddFilters(bool allow,
                              const base::ListValue* list) {
  URLMatcherConditionSet::Vector all_conditions;
  size_t size = std::min(kMaxFiltersPerPolicy, list->GetSize());
  for (size_t i = 0; i < size; ++i) {
    std::string pattern;
    bool success = list->GetString(i, &pattern);
    DCHECK(success);
    FilterComponents components;
    components.allow = allow;
    if (!FilterToComponents(pattern, &components.scheme, &components.host,
                            &components.match_subdomains, &components.port,
                            &components.path)) {
      LOG(ERROR) << "Invalid pattern " << pattern;
      continue;
    }

    all_conditions.push_back(
        CreateConditionSet(url_matcher_.get(), ++id_, components.scheme,
                           components.host, components.match_subdomains,
                           components.port, components.path));
    filters_[id_] = components;
  }
  url_matcher_->AddConditionSets(all_conditions);
}

void URLBlacklist::Block(const base::ListValue* filters) {
  AddFilters(false, filters);
}

void URLBlacklist::Allow(const base::ListValue* filters) {
  AddFilters(true, filters);
}

bool URLBlacklist::IsURLBlocked(const GURL& url) const {
  std::set<URLMatcherConditionSet::ID> matching_ids =
      url_matcher_->MatchURL(url);

  const FilterComponents* max = NULL;
  for (std::set<URLMatcherConditionSet::ID>::iterator id = matching_ids.begin();
       id != matching_ids.end(); ++id) {
    std::map<int, FilterComponents>::const_iterator it = filters_.find(*id);
    DCHECK(it != filters_.end());
    const FilterComponents& filter = it->second;
    if (!max || FilterTakesPrecedence(filter, *max))
      max = &filter;
  }

  // Default to allow.
  if (!max)
    return false;

  return !max->allow;
}

size_t URLBlacklist::Size() const {
  return filters_.size();
}

// static
bool URLBlacklist::FilterToComponents(const std::string& filter,
                                      std::string* scheme,
                                      std::string* host,
                                      bool* match_subdomains,
                                      uint16* port,
                                      std::string* path) {
  url_parse::Parsed parsed;

  if (URLFixerUpper::SegmentURL(filter, &parsed) == chrome::kFileScheme) {
    base::FilePath file_path;
    if (!net::FileURLToFilePath(GURL(filter), &file_path))
      return false;

    *scheme = chrome::kFileScheme;
    host->clear();
    *match_subdomains = true;
    *port = 0;
    // Special path when the |filter| is 'file://*'.
    *path = (filter == "file://*") ? "" : file_path.AsUTF8Unsafe();
#if defined(FILE_PATH_USES_WIN_SEPARATORS)
    // Separators have to be canonicalized on Windows.
    std::replace(path->begin(), path->end(), '\\', '/');
    *path = "/" + *path;
#endif
    return true;
  }

  if (!parsed.host.is_nonempty())
    return false;

  if (parsed.scheme.is_nonempty())
    scheme->assign(filter, parsed.scheme.begin, parsed.scheme.len);
  else
    scheme->clear();

  host->assign(filter, parsed.host.begin, parsed.host.len);
  // Special '*' host, matches all hosts.
  if (*host == "*") {
    host->clear();
    *match_subdomains = true;
  } else if ((*host)[0] == '.') {
    // A leading dot in the pattern syntax means that we don't want to match
    // subdomains.
    host->erase(0, 1);
    *match_subdomains = false;
  } else {
    url_canon::RawCanonOutputT<char> output;
    url_canon::CanonHostInfo host_info;
    url_canon::CanonicalizeHostVerbose(filter.c_str(), parsed.host,
                                       &output, &host_info);
    if (host_info.family == url_canon::CanonHostInfo::NEUTRAL) {
      // We want to match subdomains. Add a dot in front to make sure we only
      // match at domain component boundaries.
      *host = "." + *host;
      *match_subdomains = true;
    } else {
      *match_subdomains = false;
    }
  }

  if (parsed.port.is_nonempty()) {
    int int_port;
    if (!base::StringToInt(filter.substr(parsed.port.begin, parsed.port.len),
                           &int_port)) {
      return false;
    }
    if (int_port <= 0 || int_port > kuint16max)
      return false;
    *port = int_port;
  } else {
    // Match any port.
    *port = 0;
  }

  if (parsed.path.is_nonempty())
    path->assign(filter, parsed.path.begin, parsed.path.len);
  else
    path->clear();

  return true;
}

// static
scoped_refptr<extensions::URLMatcherConditionSet>
URLBlacklist::CreateConditionSet(
    extensions::URLMatcher* url_matcher,
    int id,
    const std::string& scheme,
    const std::string& host,
    bool match_subdomains,
    uint16 port,
    const std::string& path) {
  URLMatcherConditionFactory* condition_factory =
      url_matcher->condition_factory();
  std::set<URLMatcherCondition> conditions;
  conditions.insert(match_subdomains ?
      condition_factory->CreateHostSuffixPathPrefixCondition(host, path) :
      condition_factory->CreateHostEqualsPathPrefixCondition(host, path));

  scoped_ptr<URLMatcherSchemeFilter> scheme_filter;
  if (!scheme.empty())
    scheme_filter.reset(new URLMatcherSchemeFilter(scheme));

  scoped_ptr<URLMatcherPortFilter> port_filter;
  if (port != 0) {
    std::vector<URLMatcherPortFilter::Range> ranges;
    ranges.push_back(URLMatcherPortFilter::CreateRange(port));
    port_filter.reset(new URLMatcherPortFilter(ranges));
  }

  return new URLMatcherConditionSet(id, conditions,
                                    scheme_filter.Pass(), port_filter.Pass());
}

// static
bool URLBlacklist::FilterTakesPrecedence(const FilterComponents& lhs,
                                         const FilterComponents& rhs) {
  if (lhs.match_subdomains && !rhs.match_subdomains)
    return false;
  if (!lhs.match_subdomains && rhs.match_subdomains)
    return true;

  size_t host_length = lhs.host.length();
  size_t other_host_length = rhs.host.length();
  if (host_length != other_host_length)
    return host_length > other_host_length;

  size_t path_length = lhs.path.length();
  size_t other_path_length = rhs.path.length();
  if (path_length != other_path_length)
    return path_length > other_path_length;

  if (lhs.allow && !rhs.allow)
    return true;

  return false;
}

URLBlacklistManager::URLBlacklistManager(PrefService* pref_service)
    : ui_weak_ptr_factory_(this),
      pref_service_(pref_service),
      io_weak_ptr_factory_(this),
      blacklist_(new URLBlacklist) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  pref_change_registrar_.Init(pref_service_);
  base::Closure callback = base::Bind(&URLBlacklistManager::ScheduleUpdate,
                                      base::Unretained(this));
  pref_change_registrar_.Add(prefs::kUrlBlacklist, callback);
  pref_change_registrar_.Add(prefs::kUrlWhitelist, callback);

  // Start enforcing the policies without a delay when they are present at
  // startup.
  if (pref_service_->HasPrefPath(prefs::kUrlBlacklist))
    Update();
}

void URLBlacklistManager::ShutdownOnUIThread() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  // Cancel any pending updates, and stop listening for pref change updates.
  ui_weak_ptr_factory_.InvalidateWeakPtrs();
  pref_change_registrar_.RemoveAll();
}

URLBlacklistManager::~URLBlacklistManager() {
}

void URLBlacklistManager::ScheduleUpdate() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  // Cancel pending updates, if any. This can happen if two preferences that
  // change the blacklist are updated in one message loop cycle. In those cases,
  // only rebuild the blacklist after all the preference updates are processed.
  ui_weak_ptr_factory_.InvalidateWeakPtrs();
  base::MessageLoop::current()->PostTask(
      FROM_HERE,
      base::Bind(&URLBlacklistManager::Update,
                 ui_weak_ptr_factory_.GetWeakPtr()));
}

void URLBlacklistManager::Update() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // The preferences can only be read on the UI thread.
  scoped_ptr<base::ListValue> block(
      pref_service_->GetList(prefs::kUrlBlacklist)->DeepCopy());
  scoped_ptr<base::ListValue> allow(
      pref_service_->GetList(prefs::kUrlWhitelist)->DeepCopy());

  // Go through the IO thread to grab a WeakPtr to |this|. This is safe from
  // here, since this task will always execute before a potential deletion of
  // ProfileIOData on IO.
  BrowserThread::PostTask(BrowserThread::IO, FROM_HERE,
                          base::Bind(&URLBlacklistManager::UpdateOnIO,
                                     base::Unretained(this),
                                     base::Passed(&block),
                                     base::Passed(&allow)));
}

void URLBlacklistManager::UpdateOnIO(scoped_ptr<base::ListValue> block,
                                     scoped_ptr<base::ListValue> allow) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  // The URLBlacklist is built on the FILE thread. Once it's ready, it is passed
  // to the URLBlacklistManager on IO.
  BrowserThread::PostTaskAndReplyWithResult(
      BrowserThread::FILE, FROM_HERE,
      base::Bind(&BuildBlacklist,
                 base::Passed(&block),
                 base::Passed(&allow)),
      base::Bind(&URLBlacklistManager::SetBlacklist,
                 io_weak_ptr_factory_.GetWeakPtr()));
}

void URLBlacklistManager::SetBlacklist(scoped_ptr<URLBlacklist> blacklist) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  blacklist_ = blacklist.Pass();
}

bool URLBlacklistManager::IsURLBlocked(const GURL& url) const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  return blacklist_->IsURLBlocked(url);
}

bool URLBlacklistManager::IsRequestBlocked(
    const net::URLRequest& request) const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  int filter_flags = net::LOAD_MAIN_FRAME | net::LOAD_SUB_FRAME;
  if ((request.load_flags() & filter_flags) == 0)
    return false;

#if !defined(OS_CHROMEOS)
  if (IsSigninFlowURL(request.url()))
    return false;
#endif

  return IsURLBlocked(request.url());
}

// static
void URLBlacklistManager::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterListPref(prefs::kUrlBlacklist,
                             user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
  registry->RegisterListPref(prefs::kUrlWhitelist,
                             user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
}

}  // namespace policy
