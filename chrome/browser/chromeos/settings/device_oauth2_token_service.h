// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_SETTINGS_DEVICE_OAUTH2_TOKEN_SERVICE_H_
#define CHROME_BROWSER_CHROMEOS_SETTINGS_DEVICE_OAUTH2_TOKEN_SERVICE_H_

#include <set>
#include <string>

#include "base/basictypes.h"
#include "base/gtest_prod_util.h"
#include "base/memory/scoped_ptr.h"
#include "base/stl_util.h"
#include "base/time/time.h"
#include "chrome/browser/signin/oauth2_token_service.h"
#include "google_apis/gaia/gaia_oauth_client.h"
#include "net/url_request/url_request_context_getter.h"

namespace net {
class URLRequestContextGetter;
}

class GoogleServiceAuthError;
class PrefRegistrySimple;
class PrefService;
class Profile;

namespace chromeos {

// DeviceOAuth2TokenService retrieves OAuth2 access tokens for a given
// set of scopes using the device-level OAuth2 any-api refresh token
// obtained during enterprise device enrollment.
//
// See |OAuth2TokenService| for usage details.
//
// Note that requests must be made from the UI thread.
class DeviceOAuth2TokenService : public OAuth2TokenService {
 public:
  // Specialization of StartRequest that in parallel validates that the refresh
  // token stored on the device is owned by the device service account.
  virtual scoped_ptr<Request> StartRequest(const ScopeSet& scopes,
                                           Consumer* consumer) OVERRIDE;

  // Persist the given refresh token on the device.  Overwrites any previous
  // value.  Should only be called during initial device setup.
  void SetAndSaveRefreshToken(const std::string& refresh_token);

  static void RegisterPrefs(PrefRegistrySimple* registry);

  virtual std::string GetRefreshToken() OVERRIDE;

 protected:
  // Pull the robot account ID from device policy.
  virtual std::string GetRobotAccountId();

 private:
  class ValidatingConsumer;
  friend class ValidatingConsumer;
  friend class DeviceOAuth2TokenServiceFactory;
  friend class DeviceOAuth2TokenServiceTest;
  friend class TestDeviceOAuth2TokenService;

  // Use DeviceOAuth2TokenServiceFactory to get an instance of this class.
  explicit DeviceOAuth2TokenService(net::URLRequestContextGetter* getter,
                                    PrefService* local_state);
  virtual ~DeviceOAuth2TokenService();

  // Implementation of OAuth2TokenService.
  virtual net::URLRequestContextGetter* GetRequestContext() OVERRIDE;

  void OnValidationComplete(bool token_is_valid);

  bool refresh_token_is_valid_;
  int max_refresh_token_validation_retries_;

  scoped_refptr<net::URLRequestContextGetter> url_request_context_getter_;

  // Cache the decrypted refresh token, so we only decrypt once.
  std::string refresh_token_;
  PrefService* local_state_;
  DISALLOW_COPY_AND_ASSIGN(DeviceOAuth2TokenService);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_SETTINGS_DEVICE_OAUTH2_TOKEN_SERVICE_H_
