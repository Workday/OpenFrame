// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_ONLINE_ATTEMPT_HOST_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_ONLINE_ATTEMPT_HOST_H_

#include <string>

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/chromeos/login/auth_attempt_state_resolver.h"

class Profile;

namespace chromeos {

class AuthAttemptState;
class OnlineAttempt;
struct UserContext;

// Helper class which hosts OnlineAttempt for online credentials checking.
class OnlineAttemptHost : public AuthAttemptStateResolver {
 public:
  class Delegate {
    public:
     // Called after user_context were checked online.
     virtual void OnChecked(const std::string &username, bool success) = 0;
  };

  explicit OnlineAttemptHost(Delegate *delegate);
  virtual ~OnlineAttemptHost();

  // Checks user credentials using an online attempt. Calls callback with the
  // check result (whether authentication was successful). Note, only one
  // checking at a time (the newest call stops the old one, if called with
  // another username and password combination).
  void Check(Profile* profile,
             const UserContext& user_context);

  // Resets the checking process.
  void Reset();

  // AuthAttemptStateResolver overrides.
  // Executed on IO thread.
  virtual void Resolve() OVERRIDE;

  // Does an actual resolve on UI thread.
  void ResolveOnUIThread(bool success);

 private:
  Delegate* delegate_;
  std::string current_attempt_hash_;
  std::string current_username_;
  scoped_ptr<OnlineAttempt> online_attempt_;
  scoped_ptr<AuthAttemptState> state_;
  base::WeakPtrFactory<OnlineAttemptHost> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(OnlineAttemptHost);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_ONLINE_ATTEMPT_HOST_H_

