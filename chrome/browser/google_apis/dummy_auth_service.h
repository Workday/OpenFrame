// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_GOOGLE_APIS_DUMMY_AUTH_SERVICE_H_
#define CHROME_BROWSER_GOOGLE_APIS_DUMMY_AUTH_SERVICE_H_

#include "base/compiler_specific.h"
#include "chrome/browser/google_apis/auth_service_interface.h"

namespace google_apis {

// Dummy implementation of AuthServiceInterface that always return a dummy
// access token.
class DummyAuthService : public AuthServiceInterface {
 public:
  DummyAuthService();

  // AuthServiceInterface overrides.
  virtual void AddObserver(AuthServiceObserver* observer) OVERRIDE;
  virtual void RemoveObserver(AuthServiceObserver* observer) OVERRIDE;
  virtual void StartAuthentication(const AuthStatusCallback& callback) OVERRIDE;
  virtual bool HasAccessToken() const OVERRIDE;
  virtual bool HasRefreshToken() const OVERRIDE;
  virtual const std::string& access_token() const OVERRIDE;
  virtual void ClearAccessToken() OVERRIDE;
  virtual void ClearRefreshToken() OVERRIDE;

 private:
  const std::string dummy_token;
};

}  // namespace google_apis

#endif  // CHROME_BROWSER_GOOGLE_APIS_DUMMY_AUTH_SERVICE_H_
