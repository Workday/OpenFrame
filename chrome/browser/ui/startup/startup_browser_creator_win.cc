// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/startup/startup_browser_creator_win.h"

#include "base/logging.h"
#include "base/win/metro.h"
#include "chrome/browser/search_engines/util.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/startup/startup_browser_creator_impl.h"
#include "chrome/common/url_constants.h"
#include "win8/util/win8_util.h"

namespace chrome {

GURL GetURLToOpen(Profile* profile) {
  string16 params;
  base::win::MetroLaunchType launch_type =
      base::win::GetMetroLaunchParams(&params);
  if ((launch_type == base::win::METRO_PROTOCOL) ||
      (launch_type == base::win::METRO_LAUNCH))
    return GURL(params);
  return (launch_type == base::win::METRO_SEARCH) ?
      GetDefaultSearchURLForSearchTerms(profile, params) : GURL();
}

}  // namespace chrome

// static
bool StartupBrowserCreatorImpl::OpenStartupURLsInExistingBrowser(
    Profile* profile,
    const std::vector<GURL>& startup_urls) {
  if (!win8::IsSingleWindowMetroMode())
    return false;

  // We activate an existing browser window if we are opening just the new tab
  // page in metro mode.
  if (startup_urls.size() > 1)
    return false;

  if (startup_urls[0] != GURL(chrome::kChromeUINewTabURL))
    return false;

  Browser* browser = chrome::FindBrowserWithProfile(
      profile, chrome::HOST_DESKTOP_TYPE_NATIVE);

  if (!browser)
    return false;

  browser->window()->Show();
  return true;
}
