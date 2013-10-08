// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/password_manager/password_form_manager.h"

#include <algorithm>

#include "base/metrics/histogram.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "chrome/browser/password_manager/password_manager.h"
#include "chrome/browser/password_manager/password_store.h"
#include "chrome/browser/password_manager/password_store_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/autofill/core/browser/validation.h"
#include "components/autofill/core/common/autofill_messages.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/password_form.h"

using base::Time;
using content::PasswordForm;
using content::PasswordFormMap;

PasswordFormManager::PasswordFormManager(Profile* profile,
                                         PasswordManager* password_manager,
                                         content::WebContents* web_contents,
                                         const PasswordForm& observed_form,
                                         bool ssl_valid)
    : best_matches_deleter_(&best_matches_),
      observed_form_(observed_form),
      is_new_login_(true),
      has_generated_password_(false),
      password_manager_(password_manager),
      preferred_match_(NULL),
      state_(PRE_MATCHING_PHASE),
      profile_(profile),
      web_contents_(web_contents),
      manager_action_(kManagerActionNone),
      user_action_(kUserActionNone),
      submit_result_(kSubmitResultNotSubmitted) {
  DCHECK(profile_);
  if (observed_form_.origin.is_valid())
    base::SplitString(observed_form_.origin.path(), '/', &form_path_tokens_);
  observed_form_.ssl_valid = ssl_valid;
}

PasswordFormManager::~PasswordFormManager() {
  UMA_HISTOGRAM_ENUMERATION("PasswordManager.ActionsTaken",
                            GetActionsTaken(),
                            kMaxNumActionsTaken);
}

int PasswordFormManager::GetActionsTaken() {
  return user_action_ + kUserActionMax * (manager_action_ +
         kManagerActionMax * submit_result_);
};

// TODO(timsteele): use a hash of some sort in the future?
bool PasswordFormManager::DoesManage(const PasswordForm& form,
                                     ActionMatch action_match) const {
  if (form.scheme != PasswordForm::SCHEME_HTML)
      return observed_form_.signon_realm == form.signon_realm;

  // HTML form case.
  // At a minimum, username and password element must match.
  if (!((form.username_element == observed_form_.username_element) &&
        (form.password_element == observed_form_.password_element))) {
    return false;
  }

  // When action match is required, the action URL must match, but
  // the form is allowed to have an empty action URL (See bug 1107719).
  // Otherwise ignore action URL, this is to allow saving password form with
  // dynamically changed action URL (See bug 27246).
  if (form.action.is_valid() && (form.action != observed_form_.action)) {
    if (action_match == ACTION_MATCH_REQUIRED)
      return false;
  }

  // If this is a replay of the same form in the case a user entered an invalid
  // password, the origin of the new form may equal the action of the "first"
  // form.
  if (!((form.origin == observed_form_.origin) ||
        (form.origin == observed_form_.action))) {
    if (form.origin.SchemeIsSecure() &&
        !observed_form_.origin.SchemeIsSecure()) {
      // Compare origins, ignoring scheme. There is no easy way to do this
      // with GURL because clearing the scheme would result in an invalid url.
      // This is for some sites (such as Hotmail) that begin on an http page and
      // head to https for the retry when password was invalid.
      std::string::const_iterator after_scheme1 = form.origin.spec().begin() +
                                                  form.origin.scheme().length();
      std::string::const_iterator after_scheme2 =
          observed_form_.origin.spec().begin() +
          observed_form_.origin.scheme().length();
      return std::search(after_scheme1,
                         form.origin.spec().end(),
                         after_scheme2,
                         observed_form_.origin.spec().end())
                         != form.origin.spec().end();
    }
    return false;
  }
  return true;
}

bool PasswordFormManager::IsBlacklisted() {
  DCHECK_EQ(state_, POST_MATCHING_PHASE);
  if (preferred_match_ && preferred_match_->blacklisted_by_user)
    return true;
  return false;
}

void PasswordFormManager::PermanentlyBlacklist() {
  DCHECK_EQ(state_, POST_MATCHING_PHASE);

  // Configure the form about to be saved for blacklist status.
  pending_credentials_.preferred = true;
  pending_credentials_.blacklisted_by_user = true;
  pending_credentials_.username_value.clear();
  pending_credentials_.password_value.clear();

  // Retroactively forget existing matches for this form, so we NEVER prompt or
  // autofill it again.
  if (!best_matches_.empty()) {
    PasswordFormMap::const_iterator iter;
    PasswordStore* password_store = PasswordStoreFactory::GetForProfile(
        profile_, Profile::EXPLICIT_ACCESS).get();
    if (!password_store) {
      NOTREACHED();
      return;
    }
    for (iter = best_matches_.begin(); iter != best_matches_.end(); ++iter) {
      // We want to remove existing matches for this form so that the exact
      // origin match with |blackisted_by_user == true| is the only result that
      // shows up in the future for this origin URL. However, we don't want to
      // delete logins that were actually saved on a different page (hence with
      // different origin URL) and just happened to match this form because of
      // the scoring algorithm. See bug 1204493.
      if (iter->second->origin == observed_form_.origin)
        password_store->RemoveLogin(*iter->second);
    }
  }

  // Save the pending_credentials_ entry marked as blacklisted.
  SaveAsNewLogin(false);
}

bool PasswordFormManager::IsNewLogin() {
  DCHECK_EQ(state_, POST_MATCHING_PHASE);
  return is_new_login_;
}

bool PasswordFormManager::IsPendingCredentialsPublicSuffixMatch() {
  return pending_credentials_.IsPublicSuffixMatch();
}

void PasswordFormManager::SetHasGeneratedPassword() {
  has_generated_password_ = true;
}

bool PasswordFormManager::HasGeneratedPassword() {
  // This check is permissive, as the user may have generated a password and
  // then edited it in the form itself. However, even in this case the user
  // has already given consent, so we treat these cases the same.
  return has_generated_password_;
}

bool PasswordFormManager::HasValidPasswordForm() {
  DCHECK_EQ(state_, POST_MATCHING_PHASE);
  // Non-HTML password forms (primarily HTTP and FTP autentication)
  // do not contain username_element and password_element values.
  if (observed_form_.scheme != PasswordForm::SCHEME_HTML)
    return true;
  return !observed_form_.username_element.empty() &&
      !observed_form_.password_element.empty();
}

void PasswordFormManager::ProvisionallySave(
    const PasswordForm& credentials,
    OtherPossibleUsernamesAction action) {
  DCHECK_EQ(state_, POST_MATCHING_PHASE);
  DCHECK(DoesManage(credentials, ACTION_MATCH_NOT_REQUIRED));

  // Make sure the important fields stay the same as the initially observed or
  // autofilled ones, as they may have changed if the user experienced a login
  // failure.
  // Look for these credentials in the list containing auto-fill entries.
  PasswordFormMap::const_iterator it =
      best_matches_.find(credentials.username_value);
  if (it != best_matches_.end()) {
    // The user signed in with a login we autofilled.
    pending_credentials_ = *it->second;

    // Public suffix matches should always be new logins, since we want to store
    // them so they can automatically be filled in later.
    is_new_login_ = IsPendingCredentialsPublicSuffixMatch();

    // Check to see if we're using a known username but a new password.
    if (pending_credentials_.password_value != credentials.password_value)
      user_action_ = kUserActionOverride;
  } else if (action == ALLOW_OTHER_POSSIBLE_USERNAMES &&
             UpdatePendingCredentialsIfOtherPossibleUsername(
                 credentials.username_value)) {
    // |pending_credentials_| is now set. Note we don't update
    // |pending_credentials_.username_value| to |credentials.username_value|
    // yet because we need to keep the original username to modify the stored
    // credential.
    selected_username_ = credentials.username_value;
    is_new_login_ = false;
  } else {
    // User typed in a new, unknown username.
    user_action_ = kUserActionOverride;
    pending_credentials_ = observed_form_;
    pending_credentials_.username_value = credentials.username_value;
    pending_credentials_.other_possible_usernames =
        credentials.other_possible_usernames;
  }

  pending_credentials_.action = credentials.action;
  // If the user selected credentials we autofilled from a PasswordForm
  // that contained no action URL (IE6/7 imported passwords, for example),
  // bless it with the action URL from the observed form. See bug 1107719.
  if (pending_credentials_.action.is_empty())
    pending_credentials_.action = observed_form_.action;

  pending_credentials_.password_value = credentials.password_value;
  pending_credentials_.preferred = credentials.preferred;

  if (has_generated_password_)
    pending_credentials_.type = PasswordForm::TYPE_GENERATED;
}

void PasswordFormManager::Save() {
  DCHECK_EQ(state_, POST_MATCHING_PHASE);
  DCHECK(!profile_->IsOffTheRecord());

  if (IsNewLogin())
    SaveAsNewLogin(true);
  else
    UpdateLogin();
}

void PasswordFormManager::FetchMatchingLoginsFromPasswordStore() {
  DCHECK_EQ(state_, PRE_MATCHING_PHASE);
  state_ = MATCHING_PHASE;
  PasswordStore* password_store = PasswordStoreFactory::GetForProfile(
      profile_, Profile::EXPLICIT_ACCESS).get();
  if (!password_store) {
    NOTREACHED();
    return;
  }
  password_store->GetLogins(observed_form_, this);
}

bool PasswordFormManager::HasCompletedMatching() {
  return state_ == POST_MATCHING_PHASE;
}

void PasswordFormManager::OnRequestDone(
    const std::vector<PasswordForm*>& logins_result) {
  // Note that the result gets deleted after this call completes, but we own
  // the PasswordForm objects pointed to by the result vector, thus we keep
  // copies to a minimum here.

  int best_score = 0;
  std::vector<PasswordForm> empties;  // Empty-path matches in result set.
  for (size_t i = 0; i < logins_result.size(); i++) {
    if (IgnoreResult(*logins_result[i])) {
      delete logins_result[i];
      continue;
    }
    // Score and update best matches.
    int current_score = ScoreResult(*logins_result[i]);
    // This check is here so we can append empty path matches in the event
    // they don't score as high as others and aren't added to best_matches_.
    // This is most commonly imported firefox logins. We skip blacklisted
    // ones because clearly we don't want to autofill them, and secondly
    // because they only mean something when we have no other matches already
    // saved in Chrome - in which case they'll make it through the regular
    // scoring flow below by design. Note signon_realm == origin implies empty
    // path logins_result, since signon_realm is a prefix of origin for HTML
    // password forms.
    // TODO(timsteele): Bug 1269400. We probably should do something more
    // elegant for any shorter-path match instead of explicitly handling empty
    // path matches.
    if ((observed_form_.scheme == PasswordForm::SCHEME_HTML) &&
        (observed_form_.signon_realm == logins_result[i]->origin.spec()) &&
        (current_score > 0) && (!logins_result[i]->blacklisted_by_user)) {
      empties.push_back(*logins_result[i]);
    }

    if (current_score < best_score) {
      delete logins_result[i];
      continue;
    }
    if (current_score == best_score) {
      best_matches_[logins_result[i]->username_value] = logins_result[i];
    } else if (current_score > best_score) {
      best_score = current_score;
      // This new login has a better score than all those up to this point
      // Note 'this' owns all the PasswordForms in best_matches_.
      STLDeleteValues(&best_matches_);
      best_matches_.clear();
      preferred_match_ = NULL;  // Don't delete, its owned by best_matches_.
      best_matches_[logins_result[i]->username_value] = logins_result[i];
    }
    preferred_match_ = logins_result[i]->preferred ? logins_result[i]
                                                   : preferred_match_;
  }
  // We're done matching now.
  state_ = POST_MATCHING_PHASE;

  if (best_score <= 0) {
    return;
  }

  for (std::vector<PasswordForm>::const_iterator it = empties.begin();
       it != empties.end(); ++it) {
    // If we don't already have a result with the same username, add the
    // lower-scored empty-path match (if it had equal score it would already be
    // in best_matches_).
    if (best_matches_.find(it->username_value) == best_matches_.end())
      best_matches_[it->username_value] = new PasswordForm(*it);
  }

  // It is possible we have at least one match but have no preferred_match_,
  // because a user may have chosen to 'Forget' the preferred match. So we
  // just pick the first one and whichever the user selects for submit will
  // be saved as preferred.
  DCHECK(!best_matches_.empty());
  if (!preferred_match_)
    preferred_match_ = best_matches_.begin()->second;

  // Check to see if the user told us to ignore this site in the past.
  if (preferred_match_->blacklisted_by_user) {
    manager_action_ = kManagerActionBlacklisted;
    return;
  }

  // If not blacklisted, send a message to allow password generation.
  SendNotBlacklistedToRenderer();

  // Proceed to autofill.
  // Note that we provide the choices but don't actually prefill a value if:
  // (1) we are in Incognito mode, (2) the ACTION paths don't match,
  // or (3) if it matched using public suffix domain matching.
  bool wait_for_username =
      profile_->IsOffTheRecord() ||
      observed_form_.action.GetWithEmptyPath() !=
          preferred_match_->action.GetWithEmptyPath() ||
          preferred_match_->IsPublicSuffixMatch();
  if (wait_for_username)
    manager_action_ = kManagerActionNone;
  else
    manager_action_ = kManagerActionAutofilled;
  password_manager_->Autofill(observed_form_, best_matches_,
                              *preferred_match_, wait_for_username);
}

void PasswordFormManager::OnPasswordStoreRequestDone(
      CancelableRequestProvider::Handle handle,
      const std::vector<content::PasswordForm*>& result) {
  // TODO(kaiwang): Remove this function.
  NOTREACHED();
}

void PasswordFormManager::OnGetPasswordStoreResults(
      const std::vector<content::PasswordForm*>& results) {
  DCHECK_EQ(state_, MATCHING_PHASE);

  if (results.empty()) {
    state_ = POST_MATCHING_PHASE;
    // No result means that we visit this site the first time so we don't need
    // to check whether this site is blacklisted or not. Just send a message
    // to allow password generation.
    SendNotBlacklistedToRenderer();
    return;
  }
  OnRequestDone(results);
}

bool PasswordFormManager::IgnoreResult(const PasswordForm& form) const {
  // Ignore change password forms until we have some change password
  // functionality
  if (observed_form_.old_password_element.length() != 0) {
    return true;
  }
  // Don't match an invalid SSL form with one saved under secure
  // circumstances.
  if (form.ssl_valid && !observed_form_.ssl_valid) {
    return true;
  }
  return false;
}

void PasswordFormManager::SaveAsNewLogin(bool reset_preferred_login) {
  DCHECK_EQ(state_, POST_MATCHING_PHASE);
  DCHECK(IsNewLogin());
  // The new_form is being used to sign in, so it is preferred.
  DCHECK(pending_credentials_.preferred);
  // new_form contains the same basic data as observed_form_ (because its the
  // same form), but with the newly added credentials.

  DCHECK(!profile_->IsOffTheRecord());

  PasswordStore* password_store = PasswordStoreFactory::GetForProfile(
      profile_, Profile::IMPLICIT_ACCESS).get();
  if (!password_store) {
    NOTREACHED();
    return;
  }

  pending_credentials_.date_created = Time::Now();
  SanitizePossibleUsernames(&pending_credentials_);
  password_store->AddLogin(pending_credentials_);

  if (reset_preferred_login) {
    UpdatePreferredLoginState(password_store);
  }
}

void PasswordFormManager::SanitizePossibleUsernames(PasswordForm* form) {
  // Remove any possible usernames that could be credit cards or SSN for privacy
  // reasons. Also remove duplicates, both in other_possible_usernames and
  // between other_possible_usernames and username_value.
  std::set<string16> set;
  for (std::vector<string16>::iterator it =
           form->other_possible_usernames.begin();
       it != form->other_possible_usernames.end(); ++it) {
    if (!autofill::IsValidCreditCardNumber(*it) && !autofill::IsSSN(*it))
      set.insert(*it);
  }
  set.erase(form->username_value);
  std::vector<string16> temp(set.begin(), set.end());
  form->other_possible_usernames.swap(temp);
}

void PasswordFormManager::UpdatePreferredLoginState(
    PasswordStore* password_store) {
  DCHECK(password_store);
  PasswordFormMap::iterator iter;
  for (iter = best_matches_.begin(); iter != best_matches_.end(); iter++) {
    if (iter->second->username_value != pending_credentials_.username_value &&
        iter->second->preferred) {
      // This wasn't the selected login but it used to be preferred.
      iter->second->preferred = false;
      if (user_action_ == kUserActionNone)
        user_action_ = kUserActionChoose;
      password_store->UpdateLogin(*iter->second);
    }
  }
}

void PasswordFormManager::UpdateLogin() {
  DCHECK_EQ(state_, POST_MATCHING_PHASE);
  DCHECK(preferred_match_);
  // If we're doing an Update, we either autofilled correctly and need to
  // update the stats, or the user typed in a new password for autofilled
  // username, or the user selected one of the non-preferred matches,
  // thus requiring a swap of preferred bits.
  DCHECK(!IsNewLogin() && pending_credentials_.preferred);
  DCHECK(!profile_->IsOffTheRecord());

  PasswordStore* password_store = PasswordStoreFactory::GetForProfile(
      profile_, Profile::IMPLICIT_ACCESS).get();
  if (!password_store) {
    NOTREACHED();
    return;
  }

  // Update metadata.
  ++pending_credentials_.times_used;

  UpdatePreferredLoginState(password_store);

  // Remove alternate usernames. At this point we assume that we have found
  // the right username.
  pending_credentials_.other_possible_usernames.clear();

  // Update the new preferred login.
  if (!selected_username_.empty()) {
    // An other possible username is selected. We set this selected username
    // as the real username. The PasswordStore API isn't designed to update
    // username, so we delete the old credentials and add a new one instead.
    password_store->RemoveLogin(pending_credentials_);
    pending_credentials_.username_value = selected_username_;
    password_store->AddLogin(pending_credentials_);
  } else if ((observed_form_.scheme == PasswordForm::SCHEME_HTML) &&
             (observed_form_.origin.spec().length() >
              observed_form_.signon_realm.length()) &&
             (observed_form_.signon_realm ==
              pending_credentials_.origin.spec())) {
    // Note origin.spec().length > signon_realm.length implies the origin has a
    // path, since signon_realm is a prefix of origin for HTML password forms.
    //
    // The user logged in successfully with one of our autofilled logins on a
    // page with non-empty path, but the autofilled entry was initially saved/
    // imported with an empty path. Rather than just mark this entry preferred,
    // we create a more specific copy for this exact page and leave the "master"
    // unchanged. This is to prevent the case where that master login is used
    // on several sites (e.g site.com/a and site.com/b) but the user actually
    // has a different preference on each site. For example, on /a, he wants the
    // general empty-path login so it is flagged as preferred, but on /b he logs
    // in with a different saved entry - we don't want to remove the preferred
    // status of the former because upon return to /a it won't be the default-
    // fill match.
    // TODO(timsteele): Bug 1188626 - expire the master copies.
    PasswordForm copy(pending_credentials_);
    copy.origin = observed_form_.origin;
    copy.action = observed_form_.action;
    password_store->AddLogin(copy);
  } else {
    password_store->UpdateLogin(pending_credentials_);
  }
}

bool PasswordFormManager::UpdatePendingCredentialsIfOtherPossibleUsername(
    const string16& username) {
  for (PasswordFormMap::const_iterator it = best_matches_.begin();
       it != best_matches_.end(); ++it) {
    for (size_t i = 0; i < it->second->other_possible_usernames.size(); ++i) {
      if (it->second->other_possible_usernames[i] == username) {
        pending_credentials_ = *it->second;
        return true;
      }
    }
  }
  return false;
}

int PasswordFormManager::ScoreResult(const PasswordForm& candidate) const {
  DCHECK_EQ(state_, MATCHING_PHASE);
  // For scoring of candidate login data:
  // The most important element that should match is the origin, followed by
  // the action, the password name, the submit button name, and finally the
  // username input field name.
  // Exact origin match gives an addition of 64 (1 << 6) + # of matching url
  // dirs.
  // Partial match gives an addition of 32 (1 << 5) + # matching url dirs
  // That way, a partial match cannot trump an exact match even if
  // the partial one matches all other attributes (action, elements) (and
  // regardless of the matching depth in the URL path).
  // If public suffix origin match was not used, it gives an addition of
  // 16 (1 << 4).
  int score = 0;
  if (candidate.origin == observed_form_.origin) {
    // This check is here for the most common case which
    // is we have a single match in the db for the given host,
    // so we don't generally need to walk the entire URL path (the else
    // clause).
    score += (1 << 6) + static_cast<int>(form_path_tokens_.size());
  } else {
    // Walk the origin URL paths one directory at a time to see how
    // deep the two match.
    std::vector<std::string> candidate_path_tokens;
    base::SplitString(candidate.origin.path(), '/', &candidate_path_tokens);
    size_t depth = 0;
    size_t max_dirs = std::min(form_path_tokens_.size(),
                               candidate_path_tokens.size());
    while ((depth < max_dirs) && (form_path_tokens_[depth] ==
                                  candidate_path_tokens[depth])) {
      depth++;
      score++;
    }
    // do we have a partial match?
    score += (depth > 0) ? 1 << 5 : 0;
  }
  if (observed_form_.scheme == PasswordForm::SCHEME_HTML) {
    if (!candidate.IsPublicSuffixMatch())
      score += 1 << 4;
    if (candidate.action == observed_form_.action)
      score += 1 << 3;
    if (candidate.password_element == observed_form_.password_element)
      score += 1 << 2;
    if (candidate.submit_element == observed_form_.submit_element)
      score += 1 << 1;
    if (candidate.username_element == observed_form_.username_element)
      score += 1 << 0;
  }

  return score;
}

void PasswordFormManager::SubmitPassed() {
  submit_result_ = kSubmitResultPassed;
}

void PasswordFormManager::SubmitFailed() {
  submit_result_ = kSubmitResultFailed;
}

void PasswordFormManager::SendNotBlacklistedToRenderer() {
  content::RenderViewHost* host = web_contents_->GetRenderViewHost();
  host->Send(new AutofillMsg_FormNotBlacklisted(host->GetRoutingID(),
                                                 observed_form_));
}
