// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/chrome_pages.h"

#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "chrome/browser/download/download_shelf.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/signin_manager.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/singleton_tabs.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/webui/options/content_settings_handler.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/user_metrics.h"
#include "google_apis/gaia/gaia_urls.h"
#include "net/base/url_util.h"
#include "url/gurl.h"

#if defined(OS_WIN)
#include "chrome/browser/enumerate_modules_model_win.h"
#endif

using content::UserMetricsAction;

namespace chrome {
namespace {

const char kHashMark[] = "#";

void OpenBookmarkManagerWithHash(Browser* browser,
                                 const std::string& action,
                                 int64 node_id) {
  content::RecordAction(UserMetricsAction("ShowBookmarkManager"));
  content::RecordAction(UserMetricsAction("ShowBookmarks"));
  NavigateParams params(GetSingletonTabNavigateParams(
      browser,
      GURL(kChromeUIBookmarksURL).Resolve(
          base::StringPrintf("/#%s%s", action.c_str(),
              base::Int64ToString(node_id).c_str()))));
  params.path_behavior = NavigateParams::IGNORE_AND_NAVIGATE;
  ShowSingletonTabOverwritingNTP(browser, params);
}

void NavigateToSingletonTab(Browser* browser, const GURL& url) {
  NavigateParams params(GetSingletonTabNavigateParams(browser, url));
  params.path_behavior = NavigateParams::IGNORE_AND_NAVIGATE;
  ShowSingletonTabOverwritingNTP(browser, params);
}

}  // namespace

void ShowBookmarkManager(Browser* browser) {
  content::RecordAction(UserMetricsAction("ShowBookmarkManager"));
  content::RecordAction(UserMetricsAction("ShowBookmarks"));
  ShowSingletonTabOverwritingNTP(
      browser,
      GetSingletonTabNavigateParams(browser, GURL(kChromeUIBookmarksURL)));
}

void ShowBookmarkManagerForNode(Browser* browser, int64 node_id) {
  OpenBookmarkManagerWithHash(browser, std::string(), node_id);
}

void ShowHistory(Browser* browser) {
  content::RecordAction(UserMetricsAction("ShowHistory"));
  NavigateParams params(
      GetSingletonTabNavigateParams(browser, GURL(kChromeUIHistoryURL)));
  params.path_behavior = NavigateParams::IGNORE_AND_NAVIGATE;
  ShowSingletonTabOverwritingNTP(browser, params);
}

void ShowDownloads(Browser* browser) {
  content::RecordAction(UserMetricsAction("ShowDownloads"));
  if (browser->window()) {
    DownloadShelf* shelf = browser->window()->GetDownloadShelf();
    // The downloads page is always shown in response to a user action.
    if (shelf->IsShowing())
      shelf->Close(DownloadShelf::USER_ACTION);
  }
  ShowSingletonTabOverwritingNTP(
      browser,
      GetSingletonTabNavigateParams(browser, GURL(kChromeUIDownloadsURL)));
}

void ShowExtensions(Browser* browser,
                    const std::string& extension_to_highlight) {
  content::RecordAction(UserMetricsAction("ShowExtensions"));
  NavigateParams params(
      GetSingletonTabNavigateParams(browser, GURL(kChromeUIExtensionsURL)));
  params.path_behavior = NavigateParams::IGNORE_AND_NAVIGATE;
  if (!extension_to_highlight.empty()) {
    GURL::Replacements replacements;
    std::string query("id=");
    query += extension_to_highlight;
    replacements.SetQueryStr(query);
    params.url = params.url.ReplaceComponents(replacements);
  }
  ShowSingletonTabOverwritingNTP(browser, params);
}

void ShowConflicts(Browser* browser) {
#if defined(OS_WIN)
  EnumerateModulesModel* model = EnumerateModulesModel::GetInstance();
  if (model->modules_to_notify_about() > 0) {
    GURL help_center_url = model->GetFirstNotableConflict();
    if (help_center_url.is_valid()) {
      EnumerateModulesModel::RecordLearnMoreStat(true);
      ShowSingletonTab(browser, help_center_url);
      model->AcknowledgeConflictNotification();
      return;
    }
  }
#endif

  content::RecordAction(UserMetricsAction("AboutConflicts"));
  ShowSingletonTab(browser, GURL(kChromeUIConflictsURL));
}

void ShowHelp(Browser* browser, HelpSource source) {
  content::RecordAction(UserMetricsAction("ShowHelpTab"));
  GURL url;
  switch (source) {
    case HELP_SOURCE_KEYBOARD:
      url = GURL(kChromeHelpViaKeyboardURL);
      break;
    case HELP_SOURCE_MENU:
      url = GURL(kChromeHelpViaMenuURL);
      break;
    case HELP_SOURCE_WEBUI:
      url = GURL(kChromeHelpViaWebUIURL);
      break;
    default:
      NOTREACHED() << "Unhandled help source " << source;
  }
  ShowSingletonTab(browser, url);
}

void ShowPolicy(Browser* browser) {
  ShowSingletonTab(browser, GURL(kChromeUIPolicyURL));
}

void ShowSlow(Browser* browser) {
#if defined(OS_CHROMEOS)
  ShowSingletonTab(browser, GURL(kChromeUISlowURL));
#endif
}

void ShowSettings(Browser* browser) {
  content::RecordAction(UserMetricsAction("ShowOptions"));
  ShowSettingsSubPage(browser, std::string());
}

void ShowSettingsSubPage(Browser* browser, const std::string& sub_page) {
  std::string url = std::string(kChromeUISettingsURL) + sub_page;
#if defined(OS_CHROMEOS)
  if (sub_page.find(kInternetOptionsSubPage, 0) != std::string::npos) {
    std::string::size_type loc = sub_page.find("?", 0);
    std::string network_page = loc != std::string::npos ?
        sub_page.substr(loc) : std::string();
    url = std::string(kChromeUISettingsURL) + network_page;
  }
#endif
  NavigateParams params(GetSingletonTabNavigateParams(browser, GURL(url)));
  params.path_behavior = NavigateParams::IGNORE_AND_NAVIGATE;
  ShowSingletonTabOverwritingNTP(browser, params);
}

void ShowContentSettings(Browser* browser,
                         ContentSettingsType content_settings_type) {
  ShowSettingsSubPage(
      browser,
      kContentSettingsExceptionsSubPage + std::string(kHashMark) +
      options::ContentSettingsHandler::ContentSettingsTypeToGroupName(
          content_settings_type));
}

void ShowClearBrowsingDataDialog(Browser* browser) {
  content::RecordAction(UserMetricsAction("ClearBrowsingData_ShowDlg"));
  ShowSettingsSubPage(browser, kClearBrowserDataSubPage);
}

void ShowPasswordManager(Browser* browser) {
  content::RecordAction(UserMetricsAction("Options_ShowPasswordManager"));
  ShowSettingsSubPage(browser, kPasswordManagerSubPage);
}

void ShowImportDialog(Browser* browser) {
  content::RecordAction(UserMetricsAction("Import_ShowDlg"));
  ShowSettingsSubPage(browser, kImportDataSubPage);
}

void ShowAboutChrome(Browser* browser) {
  content::RecordAction(UserMetricsAction("AboutChrome"));
  NavigateParams params(
      GetSingletonTabNavigateParams(browser, GURL(kChromeUIUberURL)));
  params.path_behavior = NavigateParams::IGNORE_AND_NAVIGATE;
  ShowSingletonTabOverwritingNTP(browser, params);
}

void ShowSearchEngineSettings(Browser* browser) {
  content::RecordAction(UserMetricsAction("EditSearchEngines"));
  ShowSettingsSubPage(browser, kSearchEnginesSubPage);
}

void ShowBrowserSignin(Browser* browser, signin::Source source) {
  Profile* original_profile = browser->profile()->GetOriginalProfile();
  SigninManagerBase* manager =
      SigninManagerFactory::GetForProfile(original_profile);
  DCHECK(manager->IsSigninAllowed());
  // If we're signed in, just show settings.
  if (!manager->GetAuthenticatedUsername().empty()) {
    ShowSettings(browser);
  } else {
    // If the browser's profile is an incognito profile, make sure to use
    // a browser window from the original profile.  The user cannot sign in
    // from an incognito window.
    if (browser->profile()->IsOffTheRecord()) {
      browser =
          chrome::FindOrCreateTabbedBrowser(original_profile,
                                            chrome::HOST_DESKTOP_TYPE_NATIVE);
    }

    NavigateToSingletonTab(browser,
                           GURL(signin::GetPromoURL(source, false)));
    DCHECK_GT(browser->tab_strip_model()->count(), 0);
  }
}

void ShowGaiaSignin(Browser* browser,
                    const std::string& service,
                    const GURL& continue_url) {
  GURL url(GaiaUrls::GetInstance()->service_login_url());
  url = net::AppendQueryParameter(url, "service", service);
  if (continue_url.is_valid())
    url = net::AppendQueryParameter(url, "continue", continue_url.spec());
  NavigateToSingletonTab(browser, url);
}

}  // namespace chrome
