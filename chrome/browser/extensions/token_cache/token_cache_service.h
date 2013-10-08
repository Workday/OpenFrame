// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_TOKEN_CACHE_TOKEN_CACHE_SERVICE_H_
#define CHROME_BROWSER_EXTENSIONS_TOKEN_CACHE_TOKEN_CACHE_SERVICE_H_

#include <map>
#include <string>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/time/time.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"

namespace content {
class NotificationSource;
}

class Profile;

namespace extensions {

// This class caches tokens for the current user.  It will clear tokens out
// when the user logs out or after the specified timeout interval, or when
// the instance of chrome shuts down.
class TokenCacheService : public BrowserContextKeyedService,
                          public content::NotificationObserver {
 public:
  explicit TokenCacheService(Profile* profile);
  virtual ~TokenCacheService();

  // Store a token for the currently logged in user. We will look it up later by
  // the name given here, and we will expire the token after the timeout.  For a
  // timeout of 0, we never expire the token.  After time_to_live expires, the
  // token will be expired.
  void StoreToken(const std::string& token_name, const std::string& token_value,
                  base::TimeDelta time_to_live);

  // Retrieve a token for the currently logged in user.  This returns an empty
  // string if the token was not found or timed out.
  std::string RetrieveToken(const std::string& token_name);

  // Inherited from NotificationObserver.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

 private:
  friend class TokenCacheTest;

  struct TokenCacheData {
    std::string token;
    base::Time expiration_time;
  };

  // Map the token name (string) to token data.
  std::map<std::string, TokenCacheData> token_cache_;
  content::NotificationRegistrar registrar_;
  const Profile* const profile_;

  DISALLOW_COPY_AND_ASSIGN(TokenCacheService);
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_TOKEN_CACHE_TOKEN_CACHE_SERVICE_H_
