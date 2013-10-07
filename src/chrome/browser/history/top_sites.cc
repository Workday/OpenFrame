// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/history/top_sites.h"

#include "base/metrics/field_trial.h"
#include "base/strings/string_util.h"
#include "chrome/browser/history/top_sites_impl.h"
#include "chrome/browser/history/top_sites_likely_impl.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"
#include "grit/theme_resources.h"

namespace history {

const TopSites::PrepopulatedPage kPrepopulatedPages[] = {
#if defined(OS_ANDROID)
    { IDS_MOBILE_WELCOME_URL, IDS_NEW_TAB_CHROME_WELCOME_PAGE_TITLE,
    IDR_PRODUCT_LOGO_16, IDR_NEWTAB_CHROME_WELCOME_PAGE_THUMBNAIL,
    SkColorSetRGB(0, 147, 60) }
#else
  { IDS_CHROME_WELCOME_URL, IDS_NEW_TAB_CHROME_WELCOME_PAGE_TITLE,
    IDR_PRODUCT_LOGO_16, IDR_NEWTAB_CHROME_WELCOME_PAGE_THUMBNAIL,
    SkColorSetRGB(0, 147, 60) },
#endif
#if !defined(OS_ANDROID)
  { IDS_WEBSTORE_URL, IDS_EXTENSION_WEB_STORE_TITLE,
    IDR_WEBSTORE_ICON_16, IDR_NEWTAB_WEBSTORE_THUMBNAIL,
    SkColorSetRGB(63, 132, 197) }
#endif
};

// static
TopSites* TopSites::Create(Profile* profile, const base::FilePath& db_name) {
  if (base::FieldTrialList::FindFullName("MostLikely") == "Likely_Client") {
    // Experimental group. Enabled through a command-line flag.
    TopSitesLikelyImpl* top_sites_likely_impl = new TopSitesLikelyImpl(profile);
    top_sites_likely_impl->Init(db_name);
    return top_sites_likely_impl;
  }
  TopSitesImpl* top_sites_impl = new TopSitesImpl(profile);
  top_sites_impl->Init(db_name);
  return top_sites_impl;
}

}  // namespace history
