// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/startup/session_crashed_infobar_delegate.h"

#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search/search.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/dom_storage_context.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/storage_partition.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "ui/base/l10n/l10n_util.h"


// static
void SessionCrashedInfoBarDelegate::Create(Browser* browser) {
  // Assume that if the user is launching incognito they were previously running
  // incognito so that we have nothing to restore from.
  // Also, in ChromeBot tests, there might be a race.  This code appears to be
  // called during shutdown when there is no active WebContents.
  Profile* profile = browser->profile();
  content::WebContents* web_contents =
      browser->tab_strip_model()->GetActiveWebContents();
  if (profile->IsOffTheRecord() || !web_contents)
    return;

  InfoBarService* infobar_service =
      InfoBarService::FromWebContents(web_contents);
  infobar_service->AddInfoBar(scoped_ptr<InfoBarDelegate>(
      new SessionCrashedInfoBarDelegate(infobar_service, profile)));
}

SessionCrashedInfoBarDelegate::SessionCrashedInfoBarDelegate(
    InfoBarService* infobar_service,
    Profile* profile)
    : ConfirmInfoBarDelegate(infobar_service),
      accepted_(false),
      removed_notification_received_(false),
      profile_(profile) {
  // TODO(pkasting,marja): Once InfoBars own they delegates, this is not needed
  // any more. Then we can rely on delegates getting destroyed, and we can
  // initiate the session storage scavenging only in the destructor. (Currently,
  // info bars are leaked if they get closed while they're in background tabs.)
  registrar_.Add(this, chrome::NOTIFICATION_TAB_CONTENTS_INFOBAR_REMOVED,
                 content::NotificationService::AllSources());
}

SessionCrashedInfoBarDelegate::~SessionCrashedInfoBarDelegate() {
  // If the info bar wasn't accepted, it was either dismissed or expired. In
  // that case, session restore won't happen.
  if (!accepted_ && !removed_notification_received_) {
    content::BrowserContext::GetDefaultStoragePartition(profile_)->
        GetDOMStorageContext()->StartScavengingUnusedSessionStorage();
  }
}

int SessionCrashedInfoBarDelegate::GetIconID() const {
  return IDR_INFOBAR_RESTORE_SESSION;
}

string16 SessionCrashedInfoBarDelegate::GetMessageText() const {
  return l10n_util::GetStringUTF16(IDS_SESSION_CRASHED_VIEW_MESSAGE);
}

int SessionCrashedInfoBarDelegate::GetButtons() const {
  return BUTTON_OK;
}

string16 SessionCrashedInfoBarDelegate::GetButtonLabel(
    InfoBarButton button) const {
  DCHECK_EQ(BUTTON_OK, button);
  return l10n_util::GetStringUTF16(IDS_SESSION_CRASHED_VIEW_RESTORE_BUTTON);
}

bool SessionCrashedInfoBarDelegate::Accept() {
  uint32 behavior = 0;
  Browser* browser =
      chrome::FindBrowserWithWebContents(owner()->web_contents());
  if (browser->tab_strip_model()->count() == 1) {
    const content::WebContents* active_tab =
        browser->tab_strip_model()->GetWebContentsAt(0);
    if (active_tab->GetURL() == GURL(chrome::kChromeUINewTabURL) ||
        chrome::IsInstantNTP(active_tab)) {
      // There is only one tab and its the new tab page, make session restore
      // clobber it.
      behavior = SessionRestore::CLOBBER_CURRENT_TAB;
    }
  }
  SessionRestore::RestoreSession(browser->profile(), browser,
                                 browser->host_desktop_type(), behavior,
                                 std::vector<GURL>());
  accepted_ = true;
  return true;
}

void SessionCrashedInfoBarDelegate::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  DCHECK(type == chrome::NOTIFICATION_TAB_CONTENTS_INFOBAR_REMOVED);
  if (content::Details<std::pair<InfoBarDelegate*, bool> >(details)->first !=
      this)
    return;
  if (!accepted_) {
    content::BrowserContext::GetDefaultStoragePartition(profile_)->
        GetDOMStorageContext()->StartScavengingUnusedSessionStorage();
    removed_notification_received_ = true;
  }
}
