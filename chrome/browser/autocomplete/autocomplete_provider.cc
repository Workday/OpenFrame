// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/autocomplete/autocomplete_provider.h"

#include "base/logging.h"
#include "base/prefs/pref_service.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/autocomplete/autocomplete_match.h"
#include "chrome/browser/autocomplete/autocomplete_provider_listener.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "content/public/common/url_constants.h"
#include "net/base/net_util.h"
#include "url/gurl.h"
#include "url/url_util.h"

// static
const size_t AutocompleteProvider::kMaxMatches = 3;

AutocompleteProvider::AutocompleteProvider(
    AutocompleteProviderListener* listener,
    Profile* profile,
    Type type)
    : profile_(profile),
      listener_(listener),
      done_(true),
      type_(type) {
}

// static
const char* AutocompleteProvider::TypeToString(Type type) {
  switch (type) {
    case TYPE_BOOKMARK:
      return "Bookmark";
    case TYPE_BUILTIN:
      return "Builtin";
    case TYPE_CONTACT:
      return "Contact";
    case TYPE_EXTENSION_APP:
      return "ExtensionApp";
    case TYPE_HISTORY_QUICK:
      return "HistoryQuick";
    case TYPE_HISTORY_URL:
      return "HistoryURL";
    case TYPE_KEYWORD:
      return "Keyword";
    case TYPE_SEARCH:
      return "Search";
    case TYPE_SHORTCUTS:
      return "Shortcuts";
    case TYPE_ZERO_SUGGEST:
      return "ZeroSuggest";
    default:
      NOTREACHED() << "Unhandled AutocompleteProvider::Type " << type;
      return "Unknown";
  }
}

void AutocompleteProvider::Stop(bool clear_cached_results) {
  done_ = true;
}

const char* AutocompleteProvider::GetName() const {
  return TypeToString(type_);
}

metrics::OmniboxEventProto_ProviderType AutocompleteProvider::
    AsOmniboxEventProviderType() const {
  switch (type_) {
    case TYPE_BOOKMARK:
      return metrics::OmniboxEventProto::BOOKMARK;
    case TYPE_BUILTIN:
      return metrics::OmniboxEventProto::BUILTIN;
    case TYPE_CONTACT:
      return metrics::OmniboxEventProto::CONTACT;
    case TYPE_EXTENSION_APP:
      return metrics::OmniboxEventProto::EXTENSION_APPS;
    case TYPE_HISTORY_QUICK:
      return metrics::OmniboxEventProto::HISTORY_QUICK;
    case TYPE_HISTORY_URL:
      return metrics::OmniboxEventProto::HISTORY_URL;
    case TYPE_KEYWORD:
      return metrics::OmniboxEventProto::KEYWORD;
    case TYPE_SEARCH:
      return metrics::OmniboxEventProto::SEARCH;
    case TYPE_SHORTCUTS:
      return metrics::OmniboxEventProto::SHORTCUTS;
    case TYPE_ZERO_SUGGEST:
      return metrics::OmniboxEventProto::ZERO_SUGGEST;
    default:
      NOTREACHED() << "Unhandled AutocompleteProvider::Type " << type_;
      return metrics::OmniboxEventProto::UNKNOWN_PROVIDER;
  }
}

void AutocompleteProvider::DeleteMatch(const AutocompleteMatch& match) {
  DLOG(WARNING) << "The AutocompleteProvider '" << GetName()
                << "' has not implemented DeleteMatch.";
}

void AutocompleteProvider::AddProviderInfo(ProvidersInfo* provider_info) const {
}

void AutocompleteProvider::ResetSession() {
}

string16 AutocompleteProvider::StringForURLDisplay(const GURL& url,
                                                   bool check_accept_lang,
                                                   bool trim_http) const {
  std::string languages = (check_accept_lang && profile_) ?
      profile_->GetPrefs()->GetString(prefs::kAcceptLanguages) : std::string();
  return net::FormatUrl(url, languages,
      net::kFormatUrlOmitAll & ~(trim_http ? 0 : net::kFormatUrlOmitHTTP),
      net::UnescapeRule::SPACES, NULL, NULL, NULL);
}

AutocompleteProvider::~AutocompleteProvider() {
  Stop(false);
}

// static
bool AutocompleteProvider::HasHTTPScheme(const string16& input) {
  std::string utf8_input(UTF16ToUTF8(input));
  url_parse::Component scheme;
  if (url_util::FindAndCompareScheme(utf8_input, content::kViewSourceScheme,
                                     &scheme))
    utf8_input.erase(0, scheme.end() + 1);
  return url_util::FindAndCompareScheme(utf8_input, chrome::kHttpScheme, NULL);
}

void AutocompleteProvider::UpdateStarredStateOfMatches() {
  if (matches_.empty())
    return;

  if (!profile_)
    return;

  BookmarkModel* bookmark_model = BookmarkModelFactory::GetForProfile(profile_);
  if (!bookmark_model || !bookmark_model->loaded())
    return;

  for (ACMatches::iterator i(matches_.begin()); i != matches_.end(); ++i)
    i->starred = bookmark_model->IsBookmarked(i->destination_url);
}
