// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/online_attempt_host.h"

#include "base/bind.h"
#include "base/sha1.h"
#include "chrome/browser/chromeos/login/auth_attempt_state.h"
#include "chrome/browser/chromeos/login/online_attempt.h"
#include "chrome/browser/chromeos/login/user.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_thread.h"
#include "google_apis/gaia/gaia_auth_util.h"

namespace chromeos {

OnlineAttemptHost::OnlineAttemptHost(Delegate* delegate)
    : delegate_(delegate), weak_ptr_factory_(this) {}

OnlineAttemptHost::~OnlineAttemptHost() {
  Reset();
}

void OnlineAttemptHost::Check(Profile* profile,
                              const UserContext& user_context) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  std::string attempt_hash = base::SHA1HashString(
      user_context.username + "\n" + user_context.password);
  if (attempt_hash != current_attempt_hash_) {
    Reset();
    current_attempt_hash_ = attempt_hash;
    current_username_ = user_context.username;

    state_.reset(
        new AuthAttemptState(
            UserContext(gaia::CanonicalizeEmail(user_context.username),
                        user_context.password,
                        user_context.auth_code),
            std::string(),  // ascii_hash
            std::string(),  // login_token
            std::string(),  // login_captcha
            User::USER_TYPE_REGULAR,
            false));  // user_is_new.
    online_attempt_.reset(new OnlineAttempt(state_.get(),
                                            this));
    online_attempt_->Initiate(profile);
  }
}

void OnlineAttemptHost::Reset() {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  online_attempt_.reset(NULL);
  current_attempt_hash_.clear();
  current_username_.clear();
}

void OnlineAttemptHost::Resolve() {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  if (state_->online_complete()) {
    bool success = state_->online_outcome().reason() == LoginFailure::NONE;
    content::BrowserThread::PostTask(
        content::BrowserThread::UI,
        FROM_HERE,
        base::Bind(&OnlineAttemptHost::ResolveOnUIThread,
                   weak_ptr_factory_.GetWeakPtr(),
                   success));
  }
}

void OnlineAttemptHost::ResolveOnUIThread(bool success) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  delegate_->OnChecked(current_username_, success);
  Reset();
}

}  // namespace chromeos
