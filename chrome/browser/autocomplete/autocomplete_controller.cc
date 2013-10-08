// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/autocomplete/autocomplete_controller.h"

#include <set>
#include <string>

#include "base/command_line.h"
#include "base/format_macros.h"
#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "chrome/browser/autocomplete/autocomplete_controller_delegate.h"
#include "chrome/browser/autocomplete/bookmark_provider.h"
#include "chrome/browser/autocomplete/builtin_provider.h"
#include "chrome/browser/autocomplete/extension_app_provider.h"
#include "chrome/browser/autocomplete/history_quick_provider.h"
#include "chrome/browser/autocomplete/history_url_provider.h"
#include "chrome/browser/autocomplete/keyword_provider.h"
#include "chrome/browser/autocomplete/search_provider.h"
#include "chrome/browser/autocomplete/shortcuts_provider.h"
#include "chrome/browser/autocomplete/zero_suggest_provider.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/omnibox/omnibox_field_trial.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search/search.h"
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/common/chrome_switches.h"
#include "content/public/browser/notification_service.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "ui/base/l10n/l10n_util.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/autocomplete/contact_provider_chromeos.h"
#include "chrome/browser/chromeos/contacts/contact_manager.h"
#endif

namespace {

// Converts the given match to a type (and possibly subtype) based on the AQS
// specification. For more details, see
// http://goto.google.com/binary-clients-logging.
void AutocompleteMatchToAssistedQuery(
    const AutocompleteMatch::Type& match, size_t* type, size_t* subtype) {
  // This type indicates a native chrome suggestion.
  *type = 69;
  // Default value, indicating no subtype.
  *subtype = string16::npos;

  switch (match) {
    case AutocompleteMatchType::SEARCH_SUGGEST: {
      *type = 0;
      return;
    }
    case AutocompleteMatchType::NAVSUGGEST: {
      *type = 5;
      return;
    }
    case AutocompleteMatchType::SEARCH_WHAT_YOU_TYPED: {
      *subtype = 57;
      return;
    }
    case AutocompleteMatchType::URL_WHAT_YOU_TYPED: {
      *subtype = 58;
      return;
    }
    case AutocompleteMatchType::SEARCH_HISTORY: {
      *subtype = 59;
      return;
    }
    case AutocompleteMatchType::HISTORY_URL: {
      *subtype = 60;
      return;
    }
    case AutocompleteMatchType::HISTORY_TITLE: {
      *subtype = 61;
      return;
    }
    case AutocompleteMatchType::HISTORY_BODY: {
      *subtype = 62;
      return;
    }
    case AutocompleteMatchType::HISTORY_KEYWORD: {
      *subtype = 63;
      return;
    }
    case AutocompleteMatchType::BOOKMARK_TITLE: {
      *subtype = 65;
      return;
    }
    default: {
      // This value indicates a native chrome suggestion with no named subtype
      // (yet).
      *subtype = 64;
    }
  }
}

// Appends available autocompletion of the given type, subtype, and number to
// the existing available autocompletions string, encoding according to the
// spec.
void AppendAvailableAutocompletion(size_t type,
                                   size_t subtype,
                                   int count,
                                   std::string* autocompletions) {
  if (!autocompletions->empty())
    autocompletions->append("j");
  base::StringAppendF(autocompletions, "%" PRIuS, type);
  // Subtype is optional - string16::npos indicates no subtype.
  if (subtype != string16::npos)
    base::StringAppendF(autocompletions, "i%" PRIuS, subtype);
  if (count > 1)
    base::StringAppendF(autocompletions, "l%d", count);
}

// Returns whether the autocompletion is trivial enough that we consider it
// an autocompletion for which the omnibox autocompletion code did not add
// any value.
bool IsTrivialAutocompletion(const AutocompleteMatch& match) {
  return match.type == AutocompleteMatchType::SEARCH_WHAT_YOU_TYPED ||
      match.type == AutocompleteMatchType::URL_WHAT_YOU_TYPED ||
      match.type == AutocompleteMatchType::SEARCH_OTHER_ENGINE;
}

}  // namespace

const int AutocompleteController::kNoItemSelected = -1;

AutocompleteController::AutocompleteController(
    Profile* profile,
    AutocompleteControllerDelegate* delegate,
    int provider_types)
    : delegate_(delegate),
      history_url_provider_(NULL),
      keyword_provider_(NULL),
      search_provider_(NULL),
      zero_suggest_provider_(NULL),
      in_stop_timer_field_trial_(
          OmniboxFieldTrial::InStopTimerFieldTrialExperimentGroup()),
      done_(true),
      in_start_(false),
      in_zero_suggest_(false),
      profile_(profile) {
  // AND with the disabled providers, if any.
  provider_types &= ~OmniboxFieldTrial::GetDisabledProviderTypes();
  bool use_hqp = !!(provider_types & AutocompleteProvider::TYPE_HISTORY_QUICK);
  // TODO(mrossetti): Permanently modify the HistoryURLProvider to not search
  // titles once HQP is turned on permanently.

  if (provider_types & AutocompleteProvider::TYPE_BUILTIN)
    providers_.push_back(new BuiltinProvider(this, profile));
#if defined(OS_CHROMEOS)
  if (provider_types & AutocompleteProvider::TYPE_CONTACT)
    providers_.push_back(new ContactProvider(this, profile,
        contacts::ContactManager::GetInstance()->GetWeakPtr()));
#endif
  if (provider_types & AutocompleteProvider::TYPE_EXTENSION_APP)
    providers_.push_back(new ExtensionAppProvider(this, profile));
  if (use_hqp)
    providers_.push_back(new HistoryQuickProvider(this, profile));
  if (provider_types & AutocompleteProvider::TYPE_HISTORY_URL) {
    history_url_provider_ = new HistoryURLProvider(this, profile);
    providers_.push_back(history_url_provider_);
  }
  // Search provider/"tab to search" can be used on all platforms other than
  // Android.
#if !defined(OS_ANDROID)
  if (provider_types & AutocompleteProvider::TYPE_KEYWORD) {
    keyword_provider_ = new KeywordProvider(this, profile);
    providers_.push_back(keyword_provider_);
  }
#endif
  if (provider_types & AutocompleteProvider::TYPE_SEARCH) {
    search_provider_ = new SearchProvider(this, profile);
    providers_.push_back(search_provider_);
  }
  if (provider_types & AutocompleteProvider::TYPE_SHORTCUTS)
    providers_.push_back(new ShortcutsProvider(this, profile));

  // Create ZeroSuggest if it is enabled.
  if (provider_types & AutocompleteProvider::TYPE_ZERO_SUGGEST) {
    zero_suggest_provider_ = ZeroSuggestProvider::Create(this, profile);
    if (zero_suggest_provider_)
      providers_.push_back(zero_suggest_provider_);
  }

  if ((provider_types & AutocompleteProvider::TYPE_BOOKMARK) &&
      !CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kDisableBookmarkAutocompleteProvider))
    providers_.push_back(new BookmarkProvider(this, profile));

  for (ACProviders::iterator i(providers_.begin()); i != providers_.end(); ++i)
    (*i)->AddRef();
}

AutocompleteController::~AutocompleteController() {
  // The providers may have tasks outstanding that hold refs to them.  We need
  // to ensure they won't call us back if they outlive us.  (Practically,
  // calling Stop() should also cancel those tasks and make it so that we hold
  // the only refs.)  We also don't want to bother notifying anyone of our
  // result changes here, because the notification observer is in the midst of
  // shutdown too, so we don't ask Stop() to clear |result_| (and notify).
  result_.Reset();  // Not really necessary.
  Stop(false);

  for (ACProviders::iterator i(providers_.begin()); i != providers_.end(); ++i)
    (*i)->Release();

  providers_.clear();  // Not really necessary.
}

void AutocompleteController::Start(const AutocompleteInput& input) {
  const string16 old_input_text(input_.text());
  const AutocompleteInput::MatchesRequested old_matches_requested =
      input_.matches_requested();
  input_ = input;

  // See if we can avoid rerunning autocomplete when the query hasn't changed
  // much.  When the user presses or releases the ctrl key, the desired_tld
  // changes, and when the user finishes an IME composition, inline autocomplete
  // may no longer be prevented.  In both these cases the text itself hasn't
  // changed since the last query, and some providers can do much less work (and
  // get matches back more quickly).  Taking advantage of this reduces flicker.
  //
  // NOTE: This comes after constructing |input_| above since that construction
  // can change the text string (e.g. by stripping off a leading '?').
  const bool minimal_changes = (input_.text() == old_input_text) &&
      (input_.matches_requested() == old_matches_requested);

  expire_timer_.Stop();
  stop_timer_.Stop();

  // Start the new query.
  in_zero_suggest_ = false;
  in_start_ = true;
  base::TimeTicks start_time = base::TimeTicks::Now();
  for (ACProviders::iterator i(providers_.begin()); i != providers_.end();
       ++i) {
    // TODO(mpearson): Remove timing code once bugs 178705 / 237703 / 168933
    // are resolved.
    base::TimeTicks provider_start_time = base::TimeTicks::Now();
    (*i)->Start(input_, minimal_changes);
    if (input.matches_requested() != AutocompleteInput::ALL_MATCHES)
      DCHECK((*i)->done());
    base::TimeTicks provider_end_time = base::TimeTicks::Now();
    std::string name = std::string("Omnibox.ProviderTime.") + (*i)->GetName();
    base::HistogramBase* counter = base::Histogram::FactoryGet(
        name, 1, 5000, 20, base::Histogram::kUmaTargetedHistogramFlag);
    counter->Add(static_cast<int>(
        (provider_end_time - provider_start_time).InMilliseconds()));
  }
  if (input.matches_requested() == AutocompleteInput::ALL_MATCHES &&
      (input.text().length() < 6)) {
    base::TimeTicks end_time = base::TimeTicks::Now();
    std::string name = "Omnibox.QueryTime." + base::IntToString(
        input.text().length());
    base::HistogramBase* counter = base::Histogram::FactoryGet(
        name, 1, 1000, 50, base::Histogram::kUmaTargetedHistogramFlag);
    counter->Add(static_cast<int>((end_time - start_time).InMilliseconds()));
  }
  in_start_ = false;
  CheckIfDone();
  // The second true forces saying the default match has changed.
  // This triggers the edit model to update things such as the inline
  // autocomplete state.  In particular, if the user has typed a key
  // since the last notification, and we're now re-running
  // autocomplete, then we need to update the inline autocompletion
  // even if the current match is for the same URL as the last run's
  // default match.  Likewise, the controller doesn't know what's
  // happened in the edit since the last time it ran autocomplete.
  // The user might have selected all the text and hit delete, then
  // typed a new character.  The selection and delete won't send any
  // signals to the controller so it doesn't realize that anything was
  // cleared or changed.  Even if the default match hasn't changed, we
  // need the edit model to update the display.
  UpdateResult(false, true);

  if (!done_) {
    StartExpireTimer();
    StartStopTimer();
  }
}

void AutocompleteController::Stop(bool clear_result) {
  for (ACProviders::const_iterator i(providers_.begin()); i != providers_.end();
       ++i) {
    (*i)->Stop(clear_result);
  }

  expire_timer_.Stop();
  stop_timer_.Stop();
  done_ = true;
  if (clear_result && !result_.empty()) {
    result_.Reset();
    // NOTE: We pass in false since we're trying to only clear the popup, not
    // touch the edit... this is all a mess and should be cleaned up :(
    NotifyChanged(false);
  }
}

void AutocompleteController::StartZeroSuggest(
    const GURL& url,
    AutocompleteInput::PageClassification page_classification,
    const string16& permanent_text) {
  if (zero_suggest_provider_ != NULL) {
    DCHECK(!in_start_);  // We should not be already running a query.
    in_zero_suggest_ = true;
    zero_suggest_provider_->StartZeroSuggest(
        url, page_classification, permanent_text);
  }
}

void AutocompleteController::StopZeroSuggest() {
  if (zero_suggest_provider_ != NULL) {
    DCHECK(!in_start_);  // We should not be already running a query.
    zero_suggest_provider_->Stop(false);
  }
}

void AutocompleteController::DeleteMatch(const AutocompleteMatch& match) {
  DCHECK(match.deletable);
  match.provider->DeleteMatch(match);  // This may synchronously call back to
                                       // OnProviderUpdate().
  // If DeleteMatch resulted in a callback to OnProviderUpdate and we're
  // not done, we might attempt to redisplay the deleted match. Make sure
  // we aren't displaying it by removing any old entries.
  ExpireCopiedEntries();
}

void AutocompleteController::ExpireCopiedEntries() {
  // The first true makes UpdateResult() clear out the results and
  // regenerate them, thus ensuring that no results from the previous
  // result set remain.
  UpdateResult(true, false);
}

void AutocompleteController::OnProviderUpdate(bool updated_matches) {
  if (in_zero_suggest_) {
    // We got ZeroSuggest results before Start(). Show only those results,
    // because results from other providers are stale.
    result_.Reset();
    result_.AppendMatches(zero_suggest_provider_->matches());
    result_.SortAndCull(input_, profile_);
    NotifyChanged(true);
  } else {
    CheckIfDone();
    // Multiple providers may provide synchronous results, so we only update the
    // results if we're not in Start().
    if (!in_start_ && (updated_matches || done_))
      UpdateResult(false, false);
  }
}

void AutocompleteController::AddProvidersInfo(
    ProvidersInfo* provider_info) const {
  provider_info->clear();
  for (ACProviders::const_iterator i(providers_.begin()); i != providers_.end();
       ++i) {
    // Add per-provider info, if any.
    (*i)->AddProviderInfo(provider_info);

    // This is also a good place to put code to add info that you want to
    // add for every provider.
  }
}

void AutocompleteController::ResetSession() {
  for (ACProviders::const_iterator i(providers_.begin()); i != providers_.end();
       ++i)
    (*i)->ResetSession();
  in_zero_suggest_ = false;
}

void AutocompleteController::UpdateResult(
    bool regenerate_result,
    bool force_notify_default_match_changed) {
  const bool last_default_was_valid = result_.default_match() != result_.end();
  // The following three variables are only set and used if
  // |last_default_was_valid|.
  string16 last_default_fill_into_edit, last_default_keyword,
      last_default_associated_keyword;
  if (last_default_was_valid) {
    last_default_fill_into_edit = result_.default_match()->fill_into_edit;
    last_default_keyword = result_.default_match()->keyword;
    if (result_.default_match()->associated_keyword != NULL)
      last_default_associated_keyword =
          result_.default_match()->associated_keyword->keyword;
  }

  if (regenerate_result)
    result_.Reset();

  AutocompleteResult last_result;
  last_result.Swap(&result_);

  for (ACProviders::const_iterator i(providers_.begin()); i != providers_.end();
       ++i)
    result_.AppendMatches((*i)->matches());

  // Sort the matches and trim to a small number of "best" matches.
  result_.SortAndCull(input_, profile_);

  // Need to validate before invoking CopyOldMatches as the old matches are not
  // valid against the current input.
#ifndef NDEBUG
  result_.Validate();
#endif

  if (!done_) {
    // This conditional needs to match the conditional in Start that invokes
    // StartExpireTimer.
    result_.CopyOldMatches(input_, last_result, profile_);
  }

  UpdateKeywordDescriptions(&result_);
  UpdateAssociatedKeywords(&result_);
  UpdateAssistedQueryStats(&result_);

  const bool default_is_valid = result_.default_match() != result_.end();
  string16 default_associated_keyword;
  if (default_is_valid &&
      (result_.default_match()->associated_keyword != NULL)) {
    default_associated_keyword =
        result_.default_match()->associated_keyword->keyword;
  }
  // We've gotten async results. Send notification that the default match
  // updated if fill_into_edit, associated_keyword, or keyword differ.  (The
  // second can change if we've just started Chrome and the keyword database
  // finishes loading while processing this request.  The third can change
  // if we swapped from interpreting the input as a search--which gets
  // labeled with the default search provider's keyword--to a URL.)
  // We don't check the URL as that may change for the default match
  // even though the fill into edit hasn't changed (see SearchProvider
  // for one case of this).
  const bool notify_default_match =
      (last_default_was_valid != default_is_valid) ||
      (last_default_was_valid &&
       ((result_.default_match()->fill_into_edit !=
          last_default_fill_into_edit) ||
        (default_associated_keyword != last_default_associated_keyword) ||
        (result_.default_match()->keyword != last_default_keyword)));
  if (notify_default_match)
    last_time_default_match_changed_ = base::TimeTicks::Now();

  NotifyChanged(force_notify_default_match_changed || notify_default_match);
}

void AutocompleteController::UpdateAssociatedKeywords(
    AutocompleteResult* result) {
  if (!keyword_provider_)
    return;

  std::set<string16> keywords;
  for (ACMatches::iterator match(result->begin()); match != result->end();
       ++match) {
    string16 keyword(match->GetSubstitutingExplicitlyInvokedKeyword(profile_));
    if (!keyword.empty()) {
      keywords.insert(keyword);
      continue;
    }

    // Only add the keyword if the match does not have a duplicate keyword with
    // a more relevant match.
    keyword = match->associated_keyword.get() ?
        match->associated_keyword->keyword :
        keyword_provider_->GetKeywordForText(match->fill_into_edit);
    if (!keyword.empty() && !keywords.count(keyword)) {
      keywords.insert(keyword);

      if (!match->associated_keyword.get())
        match->associated_keyword.reset(new AutocompleteMatch(
            keyword_provider_->CreateVerbatimMatch(match->fill_into_edit,
                                                   keyword, input_)));
    } else {
      match->associated_keyword.reset();
    }
  }
}

void AutocompleteController::UpdateAssistedQueryStats(
    AutocompleteResult* result) {
  if (result->empty())
    return;

  // Build the impressions string (the AQS part after ".").
  std::string autocompletions;
  int count = 0;
  size_t last_type = string16::npos;
  size_t last_subtype = string16::npos;
  for (ACMatches::iterator match(result->begin()); match != result->end();
       ++match) {
    size_t type = string16::npos;
    size_t subtype = string16::npos;
    AutocompleteMatchToAssistedQuery(match->type, &type, &subtype);
    if (last_type != string16::npos &&
        (type != last_type || subtype != last_subtype)) {
      AppendAvailableAutocompletion(
          last_type, last_subtype, count, &autocompletions);
      count = 1;
    } else {
      count++;
    }
    last_type = type;
    last_subtype = subtype;
  }
  AppendAvailableAutocompletion(
      last_type, last_subtype, count, &autocompletions);
  // Go over all matches and set AQS if the match supports it.
  for (size_t index = 0; index < result->size(); ++index) {
    AutocompleteMatch* match = result->match_at(index);
    const TemplateURL* template_url = match->GetTemplateURL(profile_, false);
    if (!template_url || !match->search_terms_args.get())
      continue;
    std::string selected_index;
    // Prevent trivial suggestions from getting credit for being selected.
    if (!IsTrivialAutocompletion(*match))
      selected_index = base::StringPrintf("%" PRIuS, index);
    match->search_terms_args->assisted_query_stats =
        base::StringPrintf("chrome.%s.%s",
                           selected_index.c_str(),
                           autocompletions.c_str());
    match->destination_url = GURL(template_url->url_ref().ReplaceSearchTerms(
        *match->search_terms_args));
  }
}

GURL AutocompleteController::GetDestinationURL(
    const AutocompleteMatch& match,
    base::TimeDelta query_formulation_time) const {
  GURL destination_url(match.destination_url);
  TemplateURL* template_url = match.GetTemplateURL(profile_, false);

  // Append the query formulation time (time from when the user first typed a
  // character into the omnibox to when the user selected a query) and whether
  // a field trial has triggered to the AQS parameter, if other AQS parameters
  // were already populated.
  if (template_url && match.search_terms_args.get() &&
      !match.search_terms_args->assisted_query_stats.empty()) {
    TemplateURLRef::SearchTermsArgs search_terms_args(*match.search_terms_args);
    search_terms_args.assisted_query_stats += base::StringPrintf(
        ".%" PRId64 "j%dj%d",
        query_formulation_time.InMilliseconds(),
        search_provider_ &&
        search_provider_->field_trial_triggered_in_session(),
        input_.current_page_classification());
    destination_url = GURL(template_url->url_ref().
                           ReplaceSearchTerms(search_terms_args));
  }
  return destination_url;
}

void AutocompleteController::UpdateKeywordDescriptions(
    AutocompleteResult* result) {
  string16 last_keyword;
  for (AutocompleteResult::iterator i(result->begin()); i != result->end();
       ++i) {
    if ((i->provider->type() == AutocompleteProvider::TYPE_KEYWORD &&
         !i->keyword.empty()) ||
        (i->provider->type() == AutocompleteProvider::TYPE_SEARCH &&
         AutocompleteMatch::IsSearchType(i->type))) {
      i->description.clear();
      i->description_class.clear();
      DCHECK(!i->keyword.empty());
      if (i->keyword != last_keyword) {
        const TemplateURL* template_url = i->GetTemplateURL(profile_, false);
        if (template_url) {
          // For extension keywords, just make the description the extension
          // name -- don't assume that the normal search keyword description is
          // applicable.
          i->description = template_url->AdjustedShortNameForLocaleDirection();
          if (!template_url->IsExtensionKeyword()) {
            i->description = l10n_util::GetStringFUTF16(
                IDS_AUTOCOMPLETE_SEARCH_DESCRIPTION, i->description);
          }
          i->description_class.push_back(
              ACMatchClassification(0, ACMatchClassification::DIM));
        }
        last_keyword = i->keyword;
      }
    } else {
      last_keyword.clear();
    }
  }
}

void AutocompleteController::NotifyChanged(bool notify_default_match) {
  if (delegate_)
    delegate_->OnResultChanged(notify_default_match);
  if (done_) {
    content::NotificationService::current()->Notify(
        chrome::NOTIFICATION_AUTOCOMPLETE_CONTROLLER_RESULT_READY,
        content::Source<AutocompleteController>(this),
        content::NotificationService::NoDetails());
  }
}

void AutocompleteController::CheckIfDone() {
  for (ACProviders::const_iterator i(providers_.begin()); i != providers_.end();
       ++i) {
    if (!(*i)->done()) {
      done_ = false;
      return;
    }
  }
  done_ = true;
}

void AutocompleteController::StartExpireTimer() {
  // Amount of time (in ms) between when the user stops typing and
  // when we remove any copied entries. We do this from the time the
  // user stopped typing as some providers (such as SearchProvider)
  // wait for the user to stop typing before they initiate a query.
  const int kExpireTimeMS = 500;

  if (result_.HasCopiedMatches())
    expire_timer_.Start(FROM_HERE,
                        base::TimeDelta::FromMilliseconds(kExpireTimeMS),
                        this, &AutocompleteController::ExpireCopiedEntries);
}

void AutocompleteController::StartStopTimer() {
  if (!in_stop_timer_field_trial_)
    return;

  // Amount of time (in ms) between when the user stops typing and
  // when we send Stop() to every provider.  This is intended to avoid
  // the disruptive effect of belated omnibox updates, updates that
  // come after the user has had to time to read the whole dropdown
  // and doesn't expect it to change.
  const int kStopTimeMS = 1500;
  stop_timer_.Start(FROM_HERE,
                    base::TimeDelta::FromMilliseconds(kStopTimeMS),
                    base::Bind(&AutocompleteController::Stop,
                               base::Unretained(this),
                               false));
}
