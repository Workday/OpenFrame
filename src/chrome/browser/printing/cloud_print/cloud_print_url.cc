// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/printing/cloud_print/cloud_print_url.h"

#include "base/command_line.h"
#include "base/logging.h"
#include "base/prefs/pref_service.h"
#include "base/strings/stringprintf.h"
#include "chrome/browser/google/google_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "components/user_prefs/pref_registry_syncable.h"
#include "google_apis/gaia/gaia_urls.h"
#include "net/base/escape.h"
#include "url/gurl.h"

// Url must not be matched by "urls" section of
// cloud_print_app/manifest.json. If it's matched, print driver dialog will
// open sign-in page in separate window.
const char kDefaultCloudPrintServiceURL[] = "https://www.google.com/cloudprint";

const char kLearnMoreURL[] =
    "https://www.google.com/support/cloudprint";
const char kTestPageURL[] =
    "http://www.google.com/landing/cloudprint/enable.html?print=true";

// static
void CloudPrintURL::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterStringPref(
      prefs::kCloudPrintServiceURL,
      kDefaultCloudPrintServiceURL,
      user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
  std::string url = GaiaUrls::GetInstance()->service_login_url();
  url.append("?service=cloudprint&sarp=1&continue=");
  url.append(net::EscapeQueryParamValue(kDefaultCloudPrintServiceURL, false));
  registry->RegisterStringPref(
      prefs::kCloudPrintSigninURL,
      url,
      user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
}

// Returns the root service URL for the cloud print service.  The default is to
// point at the Google Cloud Print service.  This can be overridden by the
// command line or by the user preferences.
GURL CloudPrintURL::GetCloudPrintServiceURL() {
  DCHECK(profile_);

  const CommandLine& command_line = *CommandLine::ForCurrentProcess();
  GURL cloud_print_service_url = GURL(command_line.GetSwitchValueASCII(
      switches::kCloudPrintServiceURL));
  if (cloud_print_service_url.is_empty()) {
    cloud_print_service_url = GURL(
        profile_->GetPrefs()->GetString(prefs::kCloudPrintServiceURL));
  }
  return cloud_print_service_url;
}

GURL CloudPrintURL::GetCloudPrintSigninURL() {
  DCHECK(profile_);

  GURL cloud_print_signin_url = GURL(
      profile_->GetPrefs()->GetString(prefs::kCloudPrintSigninURL));
  return google_util::AppendGoogleLocaleParam(cloud_print_signin_url);
}

GURL CloudPrintURL::GetCloudPrintServiceDialogURL() {
  GURL cloud_print_service_url = GetCloudPrintServiceURL();
  std::string path(cloud_print_service_url.path() + "/client/dialog.html");
  GURL::Replacements replacements;
  replacements.SetPathStr(path);
  GURL cloud_print_dialog_url = cloud_print_service_url.ReplaceComponents(
      replacements);
  return google_util::AppendGoogleLocaleParam(cloud_print_dialog_url);
}

GURL CloudPrintURL::GetCloudPrintServiceManageURL() {
  GURL cloud_print_service_url = GetCloudPrintServiceURL();
  std::string path(cloud_print_service_url.path() + "/manage.html");
  GURL::Replacements replacements;
  replacements.SetPathStr(path);
  GURL cloud_print_manage_url = cloud_print_service_url.ReplaceComponents(
      replacements);
  return cloud_print_manage_url;
}

GURL CloudPrintURL::GetCloudPrintServiceEnableURL(
    const std::string& proxy_id) {
  GURL cloud_print_service_url = GetCloudPrintServiceURL();
  std::string path(cloud_print_service_url.path() +
      "/enable_chrome_connector/enable.html");
  GURL::Replacements replacements;
  replacements.SetPathStr(path);
  std::string query = base::StringPrintf("proxy=%s", proxy_id.c_str());
  replacements.SetQueryStr(query);
  GURL cloud_print_enable_url = cloud_print_service_url.ReplaceComponents(
      replacements);
  return cloud_print_enable_url;
}

GURL CloudPrintURL::GetCloudPrintLearnMoreURL() {
  GURL cloud_print_learn_more_url(kLearnMoreURL);
  return cloud_print_learn_more_url;
}

GURL CloudPrintURL::GetCloudPrintTestPageURL() {
  GURL cloud_print_learn_more_url(kTestPageURL);
  return cloud_print_learn_more_url;
}
