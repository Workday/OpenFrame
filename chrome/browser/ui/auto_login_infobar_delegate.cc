// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/auto_login_infobar_delegate.h"

#include "base/bind.h"
#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "base/metrics/histogram.h"
#include "base/prefs/pref_service.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/google/google_util.h"
#include "chrome/browser/infobars/infobar_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/ubertoken_fetcher.h"
#include "chrome/browser/ui/sync/sync_promo_ui.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "google_apis/gaia/gaia_constants.h"
#include "google_apis/gaia/gaia_urls.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "net/base/escape.h"
#include "net/url_request/url_request.h"
#include "ui/base/l10n/l10n_util.h"

#if defined(OS_ANDROID)
#include "chrome/browser/ui/auto_login_infobar_delegate_android.h"
#endif

using content::NavigationController;
using content::NotificationSource;
using content::NotificationDetails;


// AutoLoginRedirector --------------------------------------------------------

namespace {

// This class is created by the AutoLoginInfoBarDelegate when the user wishes to
// auto-login.  It holds context information needed while re-issuing service
// tokens using the TokenService, gets the browser cookies with the TokenAuth
// API, and finally redirects the user to the correct page.
class AutoLoginRedirector : public UbertokenConsumer,
                            public content::NotificationObserver {
 public:
  AutoLoginRedirector(NavigationController* navigation_controller,
                      const std::string& args);
  virtual ~AutoLoginRedirector();

 private:
  // Overriden from UbertokenConsumer:
  virtual void OnUbertokenSuccess(const std::string& token) OVERRIDE;
  virtual void OnUbertokenFailure(const GoogleServiceAuthError& error) OVERRIDE;

  // Implementation of content::NotificationObserver
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // Redirect tab to MergeSession URL, logging the user in and navigating
  // to the desired page.
  void RedirectToMergeSession(const std::string& token);

  NavigationController* navigation_controller_;
  const std::string args_;
  scoped_ptr<UbertokenFetcher> ubertoken_fetcher_;

  // For listening to NavigationController destruction.
  content::NotificationRegistrar registrar_;

  DISALLOW_COPY_AND_ASSIGN(AutoLoginRedirector);
};

AutoLoginRedirector::AutoLoginRedirector(
    NavigationController* navigation_controller,
    const std::string& args)
    : navigation_controller_(navigation_controller),
      args_(args) {
  ubertoken_fetcher_.reset(new UbertokenFetcher(
      Profile::FromBrowserContext(navigation_controller_->GetBrowserContext()),
      this));
  registrar_.Add(this,
                 content::NOTIFICATION_WEB_CONTENTS_DESTROYED,
                 content::Source<content::WebContents>(
                     navigation_controller_->GetWebContents()));
  ubertoken_fetcher_->StartFetchingToken();
}

AutoLoginRedirector::~AutoLoginRedirector() {
}

void AutoLoginRedirector::Observe(int type,
                                  const NotificationSource& source,
                                  const NotificationDetails& details) {
  DCHECK(type == content::NOTIFICATION_WEB_CONTENTS_DESTROYED);
  // The WebContents that started this has been destroyed. The request must be
  // cancelled and this object must be deleted.
  ubertoken_fetcher_.reset();
  base::MessageLoop::current()->DeleteSoon(FROM_HERE, this);
}

void AutoLoginRedirector::OnUbertokenSuccess(const std::string& token) {
  RedirectToMergeSession(token);
  base::MessageLoop::current()->DeleteSoon(FROM_HERE, this);
}

void AutoLoginRedirector::OnUbertokenFailure(
    const GoogleServiceAuthError& error) {
  LOG(WARNING) << "AutoLoginRedirector: token request failed";
  base::MessageLoop::current()->DeleteSoon(FROM_HERE, this);
}

void AutoLoginRedirector::RedirectToMergeSession(const std::string& token) {
  // TODO(rogerta): what is the correct page transition?
  navigation_controller_->LoadURL(
      GURL(GaiaUrls::GetInstance()->merge_session_url() +
          "?source=chrome&uberauth=" + token + "&" + args_),
      content::Referrer(), content::PAGE_TRANSITION_AUTO_BOOKMARK,
      std::string());
}

}  // namespace



// AutoLoginInfoBarDelegate ---------------------------------------------------

// static
void AutoLoginInfoBarDelegate::Create(InfoBarService* infobar_service,
                                      const Params& params) {
  infobar_service->AddInfoBar(scoped_ptr<InfoBarDelegate>(
#if defined(OS_ANDROID)
      new AutoLoginInfoBarDelegateAndroid(infobar_service, params)
#else
      new AutoLoginInfoBarDelegate(infobar_service, params)
#endif
      ));
}

AutoLoginInfoBarDelegate::AutoLoginInfoBarDelegate(
    InfoBarService* owner,
    const Params& params)
    : ConfirmInfoBarDelegate(owner),
      params_(params),
      button_pressed_(false) {
  RecordHistogramAction(SHOWN);
  registrar_.Add(this, chrome::NOTIFICATION_GOOGLE_SIGNED_OUT,
                 content::Source<Profile>(Profile::FromBrowserContext(
                     web_contents()->GetBrowserContext())));
}

AutoLoginInfoBarDelegate::~AutoLoginInfoBarDelegate() {
  if (!button_pressed_)
    RecordHistogramAction(IGNORED);
}

void AutoLoginInfoBarDelegate::InfoBarDismissed() {
  RecordHistogramAction(DISMISSED);
  button_pressed_ = true;
}

int AutoLoginInfoBarDelegate::GetIconID() const {
  return IDR_INFOBAR_AUTOLOGIN;
}

InfoBarDelegate::Type AutoLoginInfoBarDelegate::GetInfoBarType() const {
  return PAGE_ACTION_TYPE;
}

AutoLoginInfoBarDelegate*
    AutoLoginInfoBarDelegate::AsAutoLoginInfoBarDelegate() {
  return this;
}

string16 AutoLoginInfoBarDelegate::GetMessageText() const {
  return l10n_util::GetStringFUTF16(IDS_AUTOLOGIN_INFOBAR_MESSAGE,
                                    UTF8ToUTF16(params_.username));
}

string16 AutoLoginInfoBarDelegate::GetButtonLabel(
    InfoBarButton button) const {
  return l10n_util::GetStringUTF16((button == BUTTON_OK) ?
      IDS_AUTOLOGIN_INFOBAR_OK_BUTTON : IDS_AUTOLOGIN_INFOBAR_CANCEL_BUTTON);
}

bool AutoLoginInfoBarDelegate::Accept() {
  // AutoLoginRedirector deletes itself.
  new AutoLoginRedirector(&web_contents()->GetController(),
                          params_.header.args);
  RecordHistogramAction(ACCEPTED);
  button_pressed_ = true;
  return true;
}

bool AutoLoginInfoBarDelegate::Cancel() {
  PrefService* pref_service = Profile::FromBrowserContext(
      web_contents()->GetBrowserContext())->GetPrefs();
  pref_service->SetBoolean(prefs::kAutologinEnabled, false);
  RecordHistogramAction(REJECTED);
  button_pressed_ = true;
  return true;
}

void AutoLoginInfoBarDelegate::Observe(int type,
                                       const NotificationSource& source,
                                       const NotificationDetails& details) {
  DCHECK_EQ(chrome::NOTIFICATION_GOOGLE_SIGNED_OUT, type);
  // owner() can be NULL when InfoBarService removes us. See
  // |InfoBarDelegate::clear_owner|.
  if (owner())
    owner()->RemoveInfoBar(this);
}

void AutoLoginInfoBarDelegate::RecordHistogramAction(Actions action) {
  UMA_HISTOGRAM_ENUMERATION("AutoLogin.Regular", action,
                            HISTOGRAM_BOUNDING_VALUE);
}
