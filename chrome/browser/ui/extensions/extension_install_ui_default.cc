// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/extensions/extension_install_ui_default.h"

#include "apps/app_launcher.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/extension_install_prompt.h"
#include "chrome/browser/extensions/theme_installed_infobar_delegate.h"
#include "chrome/browser/infobars/confirm_infobar_delegate.h"
#include "chrome/browser/infobars/infobar_service.h"
#include "chrome/browser/prefs/incognito_mode_prefs.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search/search.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/app_list/app_list_service.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_dialogs.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/host_desktop.h"
#include "chrome/browser/ui/simple_message_box.h"
#include "chrome/browser/ui/singleton_tabs.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/web_contents.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"

#if defined(USE_ASH)
#include "ash/shell.h"
#endif

using content::BrowserThread;
using content::WebContents;
using extensions::Extension;

namespace {


// Helpers --------------------------------------------------------------------

bool disable_failure_ui_for_tests = false;

Browser* FindOrCreateVisibleBrowser(Profile* profile) {
  // TODO(mpcomplete): remove this workaround for http://crbug.com/244246
  // after fixing http://crbug.com/38676.
  if (!IncognitoModePrefs::CanOpenBrowser(profile))
    return NULL;
  Browser* browser =
      chrome::FindOrCreateTabbedBrowser(profile, chrome::GetActiveDesktop());
  if (browser->tab_strip_model()->count() == 0)
    chrome::AddBlankTabAt(browser, -1, true);
  browser->window()->Show();
  return browser;
}

void ShowExtensionInstalledBubble(const extensions::Extension* extension,
                                  Profile* profile,
                                  const SkBitmap& icon) {
  Browser* browser = FindOrCreateVisibleBrowser(profile);
  if (browser)
    chrome::ShowExtensionInstalledBubble(extension, browser, icon);
}


// ErrorInfoBarDelegate -------------------------------------------------------

// Helper class to put up an infobar when installation fails.
class ErrorInfoBarDelegate : public ConfirmInfoBarDelegate {
 public:
  // Creates an error infobar delegate and adds it to |infobar_service|.
  static void Create(InfoBarService* infobar_service,
                     const extensions::CrxInstallerError& error);

 private:
  ErrorInfoBarDelegate(InfoBarService* infobar_service,
                       const extensions::CrxInstallerError& error);
  virtual ~ErrorInfoBarDelegate();

  // ConfirmInfoBarDelegate:
  virtual string16 GetMessageText() const OVERRIDE;
  virtual int GetButtons() const OVERRIDE;
  virtual string16 GetLinkText() const OVERRIDE;
  virtual bool LinkClicked(WindowOpenDisposition disposition) OVERRIDE;

  extensions::CrxInstallerError error_;

  DISALLOW_COPY_AND_ASSIGN(ErrorInfoBarDelegate);
};

// static
void ErrorInfoBarDelegate::Create(InfoBarService* infobar_service,
                                  const extensions::CrxInstallerError& error) {
  infobar_service->AddInfoBar(scoped_ptr<InfoBarDelegate>(
      new ErrorInfoBarDelegate(infobar_service, error)));
}

ErrorInfoBarDelegate::ErrorInfoBarDelegate(
    InfoBarService* infobar_service,
    const extensions::CrxInstallerError& error)
    : ConfirmInfoBarDelegate(infobar_service),
      error_(error) {
}

ErrorInfoBarDelegate::~ErrorInfoBarDelegate() {
}

string16 ErrorInfoBarDelegate::GetMessageText() const {
  return error_.message();
}

int ErrorInfoBarDelegate::GetButtons() const {
  return BUTTON_OK;
}

string16 ErrorInfoBarDelegate::GetLinkText() const {
  return (error_.type() == extensions::CrxInstallerError::ERROR_OFF_STORE) ?
      l10n_util::GetStringUTF16(IDS_LEARN_MORE) : string16();
}

bool ErrorInfoBarDelegate::LinkClicked(WindowOpenDisposition disposition) {
  web_contents()->OpenURL(content::OpenURLParams(
      GURL("http://support.google.com/chrome_webstore/?p=crx_warning"),
      content::Referrer(),
      (disposition == CURRENT_TAB) ? NEW_FOREGROUND_TAB : disposition,
      content::PAGE_TRANSITION_LINK, false));
  return false;
}

}  // namespace


// ExtensionInstallUI ---------------------------------------------------------

// static
ExtensionInstallUI* ExtensionInstallUI::Create(Profile* profile) {
  return new ExtensionInstallUIDefault(profile);
}

// static
void ExtensionInstallUI::OpenAppInstalledUI(Profile* profile,
                                            const std::string& app_id) {
#if defined(OS_CHROMEOS)
  AppListService::Get()->ShowForProfile(profile);

  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_APP_INSTALLED_TO_APPLIST,
      content::Source<Profile>(profile),
      content::Details<const std::string>(&app_id));
#else
  Browser* browser = FindOrCreateVisibleBrowser(profile);
  if (browser) {
    GURL url(chrome::IsInstantExtendedAPIEnabled() ?
             chrome::kChromeUIAppsURL : chrome::kChromeUINewTabURL);
    chrome::NavigateParams params(
        chrome::GetSingletonTabNavigateParams(browser, url));
    chrome::Navigate(&params);

    content::NotificationService::current()->Notify(
        chrome::NOTIFICATION_APP_INSTALLED_TO_NTP,
        content::Source<WebContents>(params.target_contents),
        content::Details<const std::string>(&app_id));
  }
#endif
}

// static
void ExtensionInstallUI::DisableFailureUIForTests() {
  disable_failure_ui_for_tests = true;
}

// static
ExtensionInstallPrompt* ExtensionInstallUI::CreateInstallPromptWithBrowser(
    Browser* browser) {
  content::WebContents* web_contents = NULL;
  if (browser)
    web_contents = browser->tab_strip_model()->GetActiveWebContents();
  return new ExtensionInstallPrompt(web_contents);
}

// static
ExtensionInstallPrompt* ExtensionInstallUI::CreateInstallPromptWithProfile(
    Profile* profile) {
  Browser* browser = chrome::FindLastActiveWithProfile(profile,
      chrome::GetActiveDesktop());
  return CreateInstallPromptWithBrowser(browser);
}


// ExtensionInstallUIDefault --------------------------------------------------

ExtensionInstallUIDefault::ExtensionInstallUIDefault(Profile* profile)
    : skip_post_install_ui_(false),
      previous_using_native_theme_(false),
      use_app_installed_bubble_(false) {
  profile_ = profile;

  // |profile_| can be NULL during tests.
  if (profile_) {
    // Remember the current theme in case the user presses undo.
    const Extension* previous_theme =
        ThemeServiceFactory::GetThemeForProfile(profile);
    if (previous_theme)
      previous_theme_id_ = previous_theme->id();
    previous_using_native_theme_ =
        ThemeServiceFactory::GetForProfile(profile)->UsingNativeTheme();
  }
}

ExtensionInstallUIDefault::~ExtensionInstallUIDefault() {
}

void ExtensionInstallUIDefault::OnInstallSuccess(const Extension* extension,
                                                 SkBitmap* icon) {
  if (skip_post_install_ui_)
    return;

  if (!profile_) {
    // TODO(zelidrag): Figure out what exact conditions cause crash
    // http://crbug.com/159437 and write browser test to cover it.
    NOTREACHED();
    return;
  }

  if (extension->is_theme()) {
    ThemeInstalledInfoBarDelegate::Create(
        extension, profile_, previous_theme_id_, previous_using_native_theme_);
    return;
  }

  // Extensions aren't enabled by default in incognito so we confirm
  // the install in a normal window.
  Profile* current_profile = profile_->GetOriginalProfile();
  if (extension->is_app()) {
    bool use_bubble = false;

#if defined(TOOLKIT_VIEWS)  || defined(OS_MACOSX)
    CommandLine* cmdline = CommandLine::ForCurrentProcess();
    use_bubble = (use_app_installed_bubble_ ||
                  cmdline->HasSwitch(switches::kAppsNewInstallBubble));
#endif

    if (apps::IsAppLauncherEnabled()) {
      AppListService::Get()->ShowForProfile(current_profile);

      content::NotificationService::current()->Notify(
          chrome::NOTIFICATION_APP_INSTALLED_TO_APPLIST,
          content::Source<Profile>(current_profile),
          content::Details<const std::string>(&extension->id()));
      return;
    }

    if (use_bubble) {
      ShowExtensionInstalledBubble(extension, current_profile, *icon);
      return;
    }

    ExtensionInstallUI::OpenAppInstalledUI(current_profile, extension->id());
    return;
  }

  ShowExtensionInstalledBubble(extension, current_profile, *icon);
}

void ExtensionInstallUIDefault::OnInstallFailure(
    const extensions::CrxInstallerError& error) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (disable_failure_ui_for_tests || skip_post_install_ui_)
    return;

  Browser* browser = chrome::FindLastActiveWithProfile(profile_,
      chrome::GetActiveDesktop());
  if (!browser)  // Can be NULL in unittests.
    return;
  WebContents* web_contents =
      browser->tab_strip_model()->GetActiveWebContents();
  if (!web_contents)
    return;
  ErrorInfoBarDelegate::Create(InfoBarService::FromWebContents(web_contents),
                               error);
}

void ExtensionInstallUIDefault::SetSkipPostInstallUI(bool skip_ui) {
  skip_post_install_ui_ = skip_ui;
}

void ExtensionInstallUIDefault::SetUseAppInstalledBubble(bool use_bubble) {
  use_app_installed_bubble_ = use_bubble;
}
