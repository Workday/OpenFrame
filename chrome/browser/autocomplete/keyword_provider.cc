// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/autocomplete/keyword_provider.h"

#include <algorithm>
#include <vector>

#include "base/strings/string16.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/autocomplete/autocomplete_match.h"
#include "chrome/browser/autocomplete/autocomplete_provider_listener.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/api/omnibox/omnibox_api.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/browser/search_engines/template_url_service.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_source.h"
#include "grit/generated_resources.h"
#include "net/base/escape.h"
#include "net/base/net_util.h"
#include "ui/base/l10n/l10n_util.h"

namespace omnibox_api = extensions::api::omnibox;

// Helper functor for Start(), for ending keyword mode unless explicitly told
// otherwise.
class KeywordProvider::ScopedEndExtensionKeywordMode {
 public:
  explicit ScopedEndExtensionKeywordMode(KeywordProvider* provider)
      : provider_(provider) { }
  ~ScopedEndExtensionKeywordMode() {
    if (provider_)
      provider_->MaybeEndExtensionKeywordMode();
  }

  void StayInKeywordMode() {
    provider_ = NULL;
  }
 private:
  KeywordProvider* provider_;
};

KeywordProvider::KeywordProvider(AutocompleteProviderListener* listener,
                                 Profile* profile)
    : AutocompleteProvider(listener, profile,
          AutocompleteProvider::TYPE_KEYWORD),
      model_(NULL),
      current_input_id_(0) {
  // Extension suggestions always come from the original profile, since that's
  // where extensions run. We use the input ID to distinguish whether the
  // suggestions are meant for us.
  registrar_.Add(this,
                 chrome::NOTIFICATION_EXTENSION_OMNIBOX_SUGGESTIONS_READY,
                 content::Source<Profile>(profile->GetOriginalProfile()));
  registrar_.Add(
      this, chrome::NOTIFICATION_EXTENSION_OMNIBOX_DEFAULT_SUGGESTION_CHANGED,
      content::Source<Profile>(profile->GetOriginalProfile()));
  registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_OMNIBOX_INPUT_ENTERED,
                 content::Source<Profile>(profile));
}

KeywordProvider::KeywordProvider(AutocompleteProviderListener* listener,
                                 TemplateURLService* model)
    : AutocompleteProvider(listener, NULL, AutocompleteProvider::TYPE_KEYWORD),
      model_(model),
      current_input_id_(0) {
}


namespace {

// Helper functor for Start(), for sorting keyword matches by quality.
class CompareQuality {
 public:
  // A keyword is of higher quality when a greater fraction of it has been
  // typed, that is, when it is shorter.
  //
  // TODO(pkasting): http://b/740691 Most recent and most frequent keywords are
  // probably better rankings than the fraction of the keyword typed.  We should
  // always put any exact matches first no matter what, since the code in
  // Start() assumes this (and it makes sense).
  bool operator()(const TemplateURL* t_url1, const TemplateURL* t_url2) const {
    return t_url1->keyword().length() < t_url2->keyword().length();
  }
};

// We need our input IDs to be unique across all profiles, so we keep a global
// UID that each provider uses.
static int global_input_uid_;

}  // namespace

// static
string16 KeywordProvider::SplitKeywordFromInput(
    const string16& input,
    bool trim_leading_whitespace,
    string16* remaining_input) {
  // Find end of first token.  The AutocompleteController has trimmed leading
  // whitespace, so we need not skip over that.
  const size_t first_white(input.find_first_of(kWhitespaceUTF16));
  DCHECK_NE(0U, first_white);
  if (first_white == string16::npos)
    return input;  // Only one token provided.

  // Set |remaining_input| to everything after the first token.
  DCHECK(remaining_input != NULL);
  const size_t remaining_start = trim_leading_whitespace ?
      input.find_first_not_of(kWhitespaceUTF16, first_white) : first_white + 1;

  if (remaining_start < input.length())
    remaining_input->assign(input.begin() + remaining_start, input.end());

  // Return first token as keyword.
  return input.substr(0, first_white);
}

// static
string16 KeywordProvider::SplitReplacementStringFromInput(
    const string16& input,
    bool trim_leading_whitespace) {
  // The input may contain leading whitespace, strip it.
  string16 trimmed_input;
  TrimWhitespace(input, TRIM_LEADING, &trimmed_input);

  // And extract the replacement string.
  string16 remaining_input;
  SplitKeywordFromInput(trimmed_input, trim_leading_whitespace,
      &remaining_input);
  return remaining_input;
}

// static
const TemplateURL* KeywordProvider::GetSubstitutingTemplateURLForInput(
    TemplateURLService* model,
    AutocompleteInput* input) {
  if (!input->allow_exact_keyword_match())
    return NULL;

  string16 keyword, remaining_input;
  if (!ExtractKeywordFromInput(*input, &keyword, &remaining_input))
    return NULL;

  DCHECK(model);
  const TemplateURL* template_url = model->GetTemplateURLForKeyword(keyword);
  if (template_url && template_url->SupportsReplacement()) {
    // Adjust cursor position iff it was set before, otherwise leave it as is.
    size_t cursor_position = string16::npos;
    // The adjustment assumes that the keyword was stripped from the beginning
    // of the original input.
    if (input->cursor_position() != string16::npos &&
        !remaining_input.empty() &&
        EndsWith(input->text(), remaining_input, true)) {
      int offset = input->text().length() - input->cursor_position();
      // The cursor should never be past the last character.
      DCHECK_GE(offset, 0);
      // The cursor should never be in the keyword part, which is guaranteed
      // by OmniboxEditModel implementation (see omnibox_edit_model.cc).
      DCHECK_LE(offset, static_cast<int>(remaining_input.length()));
      if (offset <= 0) {
        // Normalize the cursor to be exactly after the last character.
        cursor_position = remaining_input.length();
      } else {
        // If somehow the cursor was before the remaining text, set it to 0,
        // otherwise adjust it relative to the remaining text.
        cursor_position = offset > static_cast<int>(remaining_input.length()) ?
            0u : remaining_input.length() - offset;
      }
    }
    input->UpdateText(remaining_input, cursor_position, input->parts());
    return template_url;
  }

  return NULL;
}

string16 KeywordProvider::GetKeywordForText(const string16& text) const {
  const string16 keyword(TemplateURLService::CleanUserInputKeyword(text));

  if (keyword.empty())
    return keyword;

  TemplateURLService* url_service = GetTemplateURLService();
  if (!url_service)
    return string16();

  // Don't provide a keyword if it doesn't support replacement.
  const TemplateURL* const template_url =
      url_service->GetTemplateURLForKeyword(keyword);
  if (!template_url || !template_url->SupportsReplacement())
    return string16();

  // Don't provide a keyword for inactive/disabled extension keywords.
  if (template_url->IsExtensionKeyword()) {
    ExtensionService* extension_service =
        extensions::ExtensionSystem::Get(profile_)->extension_service();
    const extensions::Extension* extension = extension_service->
        GetExtensionById(template_url->GetExtensionId(), false);
    if (!extension ||
        (profile_->IsOffTheRecord() &&
        !extension_service->IsIncognitoEnabled(extension->id())))
      return string16();
  }

  return keyword;
}

AutocompleteMatch KeywordProvider::CreateVerbatimMatch(
    const string16& text,
    const string16& keyword,
    const AutocompleteInput& input) {
  // A verbatim match is allowed to be the default match.
  return CreateAutocompleteMatch(
      GetTemplateURLService()->GetTemplateURLForKeyword(keyword), input,
      keyword.length(), SplitReplacementStringFromInput(text, true), true, 0);
}

void KeywordProvider::Start(const AutocompleteInput& input,
                            bool minimal_changes) {
  // This object ensures we end keyword mode if we exit the function without
  // toggling keyword mode to on.
  ScopedEndExtensionKeywordMode keyword_mode_toggle(this);

  matches_.clear();

  if (!minimal_changes) {
    done_ = true;

    // Input has changed. Increment the input ID so that we can discard any
    // stale extension suggestions that may be incoming.
    current_input_id_ = ++global_input_uid_;
  }

  // Split user input into a keyword and some query input.
  //
  // We want to suggest keywords even when users have started typing URLs, on
  // the assumption that they might not realize they no longer need to go to a
  // site to be able to search it.  So we call CleanUserInputKeyword() to strip
  // any initial scheme and/or "www.".  NOTE: Any heuristics or UI used to
  // automatically/manually create keywords will need to be in sync with
  // whatever we do here!
  //
  // TODO(pkasting): http://b/1112681 If someday we remember usage frequency for
  // keywords, we might suggest keywords that haven't even been partially typed,
  // if the user uses them enough and isn't obviously typing something else.  In
  // this case we'd consider all input here to be query input.
  string16 keyword, remaining_input;
  if (!ExtractKeywordFromInput(input, &keyword, &remaining_input))
    return;

  // Get the best matches for this keyword.
  //
  // NOTE: We could cache the previous keywords and reuse them here in the
  // |minimal_changes| case, but since we'd still have to recalculate their
  // relevances and we can just recreate the results synchronously anyway, we
  // don't bother.
  //
  // TODO(pkasting): http://b/893701 We should remember the user's use of a
  // search query both from the autocomplete popup and from web pages
  // themselves.
  TemplateURLService::TemplateURLVector matches;
  GetTemplateURLService()->FindMatchingKeywords(
      keyword, !remaining_input.empty(), &matches);

  for (TemplateURLService::TemplateURLVector::iterator i(matches.begin());
       i != matches.end(); ) {
    const TemplateURL* template_url = *i;

    // Prune any extension keywords that are disallowed in incognito mode (if
    // we're incognito), or disabled.
    if (profile_ && template_url->IsExtensionKeyword()) {
      ExtensionService* service = extensions::ExtensionSystem::Get(profile_)->
          extension_service();
      const extensions::Extension* extension =
          service->GetExtensionById(template_url->GetExtensionId(), false);
      bool enabled =
          extension && (!profile_->IsOffTheRecord() ||
                        service->IsIncognitoEnabled(extension->id()));
      if (!enabled) {
        i = matches.erase(i);
        continue;
      }
    }

    // Prune any substituting keywords if there is no substitution.
    if (template_url->SupportsReplacement() && remaining_input.empty() &&
        !input.allow_exact_keyword_match()) {
      i = matches.erase(i);
      continue;
    }

    ++i;
  }
  if (matches.empty())
    return;
  std::sort(matches.begin(), matches.end(), CompareQuality());

  // Limit to one exact or three inexact matches, and mark them up for display
  // in the autocomplete popup.
  // Any exact match is going to be the highest quality match, and thus at the
  // front of our vector.
  if (matches.front()->keyword() == keyword) {
    const TemplateURL* template_url = matches.front();
    const bool is_extension_keyword = template_url->IsExtensionKeyword();

    // Only create an exact match if |remaining_input| is empty or if
    // this is an extension keyword.  If |remaining_input| is a
    // non-empty non-extension keyword (i.e., a regular keyword that
    // supports replacement and that has extra text following it),
    // then SearchProvider creates the exact (a.k.a. verbatim) match.
    if (!remaining_input.empty() && !is_extension_keyword)
      return;

    // TODO(pkasting): We should probably check that if the user explicitly
    // typed a scheme, that scheme matches the one in |template_url|.

    // When creating an exact match (either for the keyword itself, no
    // remaining query or an extension keyword, possibly with remaining
    // input), allow the match to be the default match.
    matches_.push_back(CreateAutocompleteMatch(
        template_url, input, keyword.length(), remaining_input, true, -1));

    if (profile_ && is_extension_keyword) {
      if (input.matches_requested() == AutocompleteInput::ALL_MATCHES) {
        if (template_url->GetExtensionId() != current_keyword_extension_id_)
          MaybeEndExtensionKeywordMode();
        if (current_keyword_extension_id_.empty())
          EnterExtensionKeywordMode(template_url->GetExtensionId());
        keyword_mode_toggle.StayInKeywordMode();
      }

      extensions::ApplyDefaultSuggestionForExtensionKeyword(
          profile_, template_url,
          remaining_input,
          &matches_[0]);

      if (minimal_changes &&
          (input.matches_requested() != AutocompleteInput::BEST_MATCH)) {
        // If the input hasn't significantly changed, we can just use the
        // suggestions from last time. We need to readjust the relevance to
        // ensure it is less than the main match's relevance.
        for (size_t i = 0; i < extension_suggest_matches_.size(); ++i) {
          matches_.push_back(extension_suggest_matches_[i]);
          matches_.back().relevance = matches_[0].relevance - (i + 1);
        }
      } else if (input.matches_requested() == AutocompleteInput::ALL_MATCHES) {
        extension_suggest_last_input_ = input;
        extension_suggest_matches_.clear();

        bool have_listeners =
          extensions::ExtensionOmniboxEventRouter::OnInputChanged(
              profile_, template_url->GetExtensionId(),
              UTF16ToUTF8(remaining_input), current_input_id_);

        // We only have to wait for suggest results if there are actually
        // extensions listening for input changes.
        if (have_listeners)
          done_ = false;
      }
    }
  } else {
    if (matches.size() > kMaxMatches)
      matches.erase(matches.begin() + kMaxMatches, matches.end());
    for (TemplateURLService::TemplateURLVector::const_iterator i(
         matches.begin()); i != matches.end(); ++i) {
      matches_.push_back(CreateAutocompleteMatch(
          *i, input, keyword.length(), remaining_input, false, -1));
    }
  }
}

void KeywordProvider::Stop(bool clear_cached_results) {
  done_ = true;
  MaybeEndExtensionKeywordMode();
}

KeywordProvider::~KeywordProvider() {}

// static
bool KeywordProvider::ExtractKeywordFromInput(const AutocompleteInput& input,
                                              string16* keyword,
                                              string16* remaining_input) {
  if ((input.type() == AutocompleteInput::INVALID) ||
      (input.type() == AutocompleteInput::FORCED_QUERY))
    return false;

  string16 trimmed_input;
  TrimWhitespace(input.text(), TRIM_TRAILING, &trimmed_input);
  *keyword = TemplateURLService::CleanUserInputKeyword(
      SplitKeywordFromInput(trimmed_input, true, remaining_input));
  return !keyword->empty();
}

// static
int KeywordProvider::CalculateRelevance(AutocompleteInput::Type type,
                                        bool complete,
                                        bool supports_replacement,
                                        bool prefer_keyword,
                                        bool allow_exact_keyword_match) {
  // This function is responsible for scoring suggestions of keywords
  // themselves and the suggestion of the verbatim query on an
  // extension keyword.  SearchProvider::CalculateRelevanceForKeywordVerbatim()
  // scores verbatim query suggestions for non-extension keywords.
  // These two functions are currently in sync, but there's no reason
  // we couldn't decide in the future to score verbatim matches
  // differently for extension and non-extension keywords.  If you
  // make such a change, however, you should update this comment to
  // describe it, so it's clear why the functions diverge.
  if (!complete)
    return (type == AutocompleteInput::URL) ? 700 : 450;
  if (!supports_replacement || (allow_exact_keyword_match && prefer_keyword))
    return 1500;
  return (allow_exact_keyword_match && (type == AutocompleteInput::QUERY)) ?
      1450 : 1100;
}

AutocompleteMatch KeywordProvider::CreateAutocompleteMatch(
    const TemplateURL* template_url,
    const AutocompleteInput& input,
    size_t prefix_length,
    const string16& remaining_input,
    bool allowed_to_be_default_match,
    int relevance) {
  DCHECK(template_url);
  const bool supports_replacement =
      template_url->url_ref().SupportsReplacement();

  // Create an edit entry of "[keyword] [remaining input]".  This is helpful
  // even when [remaining input] is empty, as the user can select the popup
  // choice and immediately begin typing in query input.
  const string16& keyword = template_url->keyword();
  const bool keyword_complete = (prefix_length == keyword.length());
  if (relevance < 0) {
    relevance =
        CalculateRelevance(input.type(), keyword_complete,
                           // When the user wants keyword matches to take
                           // preference, score them highly regardless of
                           // whether the input provides query text.
                           supports_replacement, input.prefer_keyword(),
                           input.allow_exact_keyword_match());
  }
  AutocompleteMatch match(this, relevance, false,
      supports_replacement ? AutocompleteMatchType::SEARCH_OTHER_ENGINE :
                             AutocompleteMatchType::HISTORY_KEYWORD);
  match.allowed_to_be_default_match = allowed_to_be_default_match;
  match.fill_into_edit = keyword;
  if (!remaining_input.empty() || !keyword_complete || supports_replacement)
    match.fill_into_edit.push_back(L' ');
  match.fill_into_edit.append(remaining_input);
  // If we wanted to set |result.inline_autocompletion| correctly, we'd need
  // CleanUserInputKeyword() to return the amount of adjustment it's made to
  // the user's input.  Because right now inexact keyword matches can't score
  // more highly than a "what you typed" match from one of the other providers,
  // we just don't bother to do this, and leave inline autocompletion off.

  // Create destination URL and popup entry content by substituting user input
  // into keyword templates.
  FillInURLAndContents(remaining_input, template_url, &match);

  match.keyword = keyword;
  match.transition = content::PAGE_TRANSITION_KEYWORD;

  return match;
}

void KeywordProvider::FillInURLAndContents(const string16& remaining_input,
                                           const TemplateURL* element,
                                           AutocompleteMatch* match) const {
  DCHECK(!element->short_name().empty());
  const TemplateURLRef& element_ref = element->url_ref();
  DCHECK(element_ref.IsValid());
  int message_id = element->IsExtensionKeyword() ?
      IDS_EXTENSION_KEYWORD_COMMAND : IDS_KEYWORD_SEARCH;
  if (remaining_input.empty()) {
    // Allow extension keyword providers to accept empty string input. This is
    // useful to allow extensions to do something in the case where no input is
    // entered.
    if (element_ref.SupportsReplacement() && !element->IsExtensionKeyword()) {
      // No query input; return a generic, no-destination placeholder.
      match->contents.assign(
          l10n_util::GetStringFUTF16(message_id,
              element->AdjustedShortNameForLocaleDirection(),
              l10n_util::GetStringUTF16(IDS_EMPTY_KEYWORD_VALUE)));
      match->contents_class.push_back(
          ACMatchClassification(0, ACMatchClassification::DIM));
    } else {
      // Keyword that has no replacement text (aka a shorthand for a URL).
      match->destination_url = GURL(element->url());
      match->contents.assign(element->short_name());
      AutocompleteMatch::ClassifyLocationInString(0, match->contents.length(),
          match->contents.length(), ACMatchClassification::NONE,
          &match->contents_class);
    }
  } else {
    // Create destination URL by escaping user input and substituting into
    // keyword template URL.  The escaping here handles whitespace in user
    // input, but we rely on later canonicalization functions to do more
    // fixup to make the URL valid if necessary.
    DCHECK(element_ref.SupportsReplacement());
    TemplateURLRef::SearchTermsArgs search_terms_args(remaining_input);
    search_terms_args.append_extra_query_params =
        element == GetTemplateURLService()->GetDefaultSearchProvider();
    match->destination_url =
        GURL(element_ref.ReplaceSearchTerms(search_terms_args));
    std::vector<size_t> content_param_offsets;
    match->contents.assign(l10n_util::GetStringFUTF16(message_id,
                                                      element->short_name(),
                                                      remaining_input,
                                                      &content_param_offsets));
    DCHECK_EQ(2U, content_param_offsets.size());
    AutocompleteMatch::ClassifyLocationInString(content_param_offsets[1],
        remaining_input.length(), match->contents.length(),
        ACMatchClassification::NONE, &match->contents_class);
  }
}

void KeywordProvider::Observe(int type,
                              const content::NotificationSource& source,
                              const content::NotificationDetails& details) {
  TemplateURLService* model = GetTemplateURLService();
  const AutocompleteInput& input = extension_suggest_last_input_;

  switch (type) {
    case chrome::NOTIFICATION_EXTENSION_OMNIBOX_INPUT_ENTERED:
      // Input has been accepted, so we're done with this input session. Ensure
      // we don't send the OnInputCancelled event, or handle any more stray
      // suggestions_ready events.
      current_keyword_extension_id_.clear();
      current_input_id_ = 0;
      return;

    case chrome::NOTIFICATION_EXTENSION_OMNIBOX_DEFAULT_SUGGESTION_CHANGED: {
      // It's possible to change the default suggestion while not in an editing
      // session.
      string16 keyword, remaining_input;
      if (matches_.empty() || current_keyword_extension_id_.empty() ||
          !ExtractKeywordFromInput(input, &keyword, &remaining_input))
        return;

      const TemplateURL* template_url(
          model->GetTemplateURLForKeyword(keyword));
      extensions::ApplyDefaultSuggestionForExtensionKeyword(
          profile_, template_url,
          remaining_input,
          &matches_[0]);
      listener_->OnProviderUpdate(true);
      return;
    }

    case chrome::NOTIFICATION_EXTENSION_OMNIBOX_SUGGESTIONS_READY: {
      const omnibox_api::SendSuggestions::Params& suggestions =
          *content::Details<
              omnibox_api::SendSuggestions::Params>(details).ptr();
      if (suggestions.request_id != current_input_id_)
        return;  // This is an old result. Just ignore.

      string16 keyword, remaining_input;
      bool result = ExtractKeywordFromInput(input, &keyword, &remaining_input);
      DCHECK(result);
      const TemplateURL* template_url =
          model->GetTemplateURLForKeyword(keyword);

      // TODO(mpcomplete): consider clamping the number of suggestions to
      // AutocompleteProvider::kMaxMatches.
      for (size_t i = 0; i < suggestions.suggest_results.size(); ++i) {
        const omnibox_api::SuggestResult& suggestion =
            *suggestions.suggest_results[i];
        // We want to order these suggestions in descending order, so start with
        // the relevance of the first result (added synchronously in Start()),
        // and subtract 1 for each subsequent suggestion from the extension.
        // We recompute the first match's relevance; we know that |complete|
        // is true, because we wouldn't get results from the extension unless
        // the full keyword had been typed.
        int first_relevance = CalculateRelevance(input.type(), true, true,
            input.prefer_keyword(), input.allow_exact_keyword_match());
        // Because these matches are async, we should never let them become the
        // default match, lest we introduce race conditions in the omnibox user
        // interaction.
        extension_suggest_matches_.push_back(CreateAutocompleteMatch(
            template_url, input, keyword.length(),
            UTF8ToUTF16(suggestion.content), false, first_relevance - (i + 1)));

        AutocompleteMatch* match = &extension_suggest_matches_.back();
        match->contents.assign(UTF8ToUTF16(suggestion.description));
        match->contents_class =
            extensions::StyleTypesToACMatchClassifications(suggestion);
        match->description.clear();
        match->description_class.clear();
      }

      done_ = true;
      matches_.insert(matches_.end(), extension_suggest_matches_.begin(),
                      extension_suggest_matches_.end());
      listener_->OnProviderUpdate(!extension_suggest_matches_.empty());
      return;
    }

    default:
      NOTREACHED();
      return;
  }
}

TemplateURLService* KeywordProvider::GetTemplateURLService() const {
  TemplateURLService* service = profile_ ?
      TemplateURLServiceFactory::GetForProfile(profile_) : model_;
  // Make sure the model is loaded. This is cheap and quickly bails out if
  // the model is already loaded.
  DCHECK(service);
  service->Load();
  return service;
}

void KeywordProvider::EnterExtensionKeywordMode(
    const std::string& extension_id) {
  DCHECK(current_keyword_extension_id_.empty());
  current_keyword_extension_id_ = extension_id;

  extensions::ExtensionOmniboxEventRouter::OnInputStarted(
      profile_, current_keyword_extension_id_);
}

void KeywordProvider::MaybeEndExtensionKeywordMode() {
  if (!current_keyword_extension_id_.empty()) {
    extensions::ExtensionOmniboxEventRouter::OnInputCancelled(
        profile_, current_keyword_extension_id_);

    current_keyword_extension_id_.clear();
  }
}
