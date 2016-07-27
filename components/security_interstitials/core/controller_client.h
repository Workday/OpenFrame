// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SECURITY_INTERSTITIALS_CORE_CONTROLLER_CLIENT_H_
#define COMPONENTS_SECURITY_INTERSTITIALS_CORE_CONTROLLER_CLIENT_H_

#include <string>

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"

class GURL;
class PrefService;

namespace security_interstitials {

class MetricsHelper;

// Constants used to communicate with the JavaScript.
extern const char kBoxChecked[];
extern const char kDisplayCheckBox[];
extern const char kOptInLink[];
extern const char kPrivacyLinkHtml[];

// These represent the commands sent from the interstitial JavaScript.
// DO NOT reorder or change these without also changing the JavaScript!
// See components/security_interstitials/core/browser/resources/
enum SecurityInterstitialCommands {
  // Used by tests
  CMD_ERROR = -3,
  CMD_TEXT_FOUND = -2,
  CMD_TEXT_NOT_FOUND = -1,
  // Decisions
  CMD_DONT_PROCEED = 0,
  CMD_PROCEED = 1,
  // Ways for user to get more information
  CMD_SHOW_MORE_SECTION = 2,
  CMD_OPEN_HELP_CENTER = 3,
  CMD_OPEN_DIAGNOSTIC = 4,
  // Primary button actions
  CMD_RELOAD = 5,
  CMD_OPEN_DATE_SETTINGS = 6,
  CMD_OPEN_LOGIN = 7,
  // Safe Browsing Extended Reporting
  CMD_DO_REPORT = 8,
  CMD_DONT_REPORT = 9,
  CMD_OPEN_REPORTING_PRIVACY = 10,
  // Report a phishing error
  CMD_REPORT_PHISHING_ERROR = 11,
};

// Provides methods for handling commands from the user, which requires some
// embedder-specific abstraction.
class ControllerClient {
 public:
  ControllerClient();
  virtual ~ControllerClient();

  // Handle the user's reporting preferences.
  void SetReportingPreference(bool report);
  void OpenExtendedReportingPrivacyPolicy();

  MetricsHelper* metrics_helper() const;
  void set_metrics_helper(scoped_ptr<MetricsHelper> metrics_helper);

 protected:
  virtual void OpenUrlInCurrentTab(const GURL& url) = 0;

  virtual const std::string& GetApplicationLocale() = 0;
  virtual PrefService* GetPrefService() = 0;
  virtual const std::string GetExtendedReportingPrefName() = 0;

 private:
  scoped_ptr<MetricsHelper> metrics_helper_;

  DISALLOW_COPY_AND_ASSIGN(ControllerClient);
};

}  // namespace security_interstitials

#endif  // COMPONENTS_SECURITY_INTERSTITIALS_CORE_CONTROLLER_CLIENT_H_
