// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/three_d_api_observer.h"

#include "base/metrics/histogram.h"
#include "chrome/browser/infobars/confirm_infobar_delegate.h"
#include "chrome/browser/infobars/infobar_service.h"
#include "chrome/browser/tab_contents/tab_util.h"
#include "content/public/browser/gpu_data_manager.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"


// ThreeDAPIInfoBarDelegate ---------------------------------------------------

class ThreeDAPIInfoBarDelegate : public ConfirmInfoBarDelegate {
 public:
  // Creates a 3D API infobar delegate and adds it to |infobar_service|.
  static void Create(InfoBarService* infobar_service,
                     const GURL& url,
                     content::ThreeDAPIType requester);

 private:
  enum DismissalHistogram {
    IGNORED,
    RELOADED,
    CLOSED_WITHOUT_ACTION,
    DISMISSAL_MAX
  };

  ThreeDAPIInfoBarDelegate(InfoBarService* owner,
                           const GURL& url,
                           content::ThreeDAPIType requester);
  virtual ~ThreeDAPIInfoBarDelegate();

  // ConfirmInfoBarDelegate:
  virtual bool EqualsDelegate(InfoBarDelegate* delegate) const OVERRIDE;
  virtual ThreeDAPIInfoBarDelegate* AsThreeDAPIInfoBarDelegate() OVERRIDE;
  virtual string16 GetMessageText() const OVERRIDE;
  virtual string16 GetButtonLabel(InfoBarButton button) const OVERRIDE;
  virtual bool Accept() OVERRIDE;
  virtual bool Cancel() OVERRIDE;
  virtual string16 GetLinkText() const OVERRIDE;
  virtual bool LinkClicked(WindowOpenDisposition disposition) OVERRIDE;

  GURL url_;
  content::ThreeDAPIType requester_;
  // Basically indicates whether the infobar was displayed at all, or
  // was a temporary instance thrown away by the InfobarService.
  mutable bool message_text_queried_;
  bool action_taken_;

  DISALLOW_COPY_AND_ASSIGN(ThreeDAPIInfoBarDelegate);
};

// static
void ThreeDAPIInfoBarDelegate::Create(InfoBarService* infobar_service,
                                      const GURL& url,
                                      content::ThreeDAPIType requester) {
  if (!infobar_service)
    return;  // NULL for apps.
  infobar_service->AddInfoBar(scoped_ptr<InfoBarDelegate>(
      new ThreeDAPIInfoBarDelegate(infobar_service, url, requester)));
}

ThreeDAPIInfoBarDelegate::ThreeDAPIInfoBarDelegate(
    InfoBarService* owner,
    const GURL& url,
    content::ThreeDAPIType requester)
    : ConfirmInfoBarDelegate(owner),
      url_(url),
      requester_(requester),
      message_text_queried_(false),
      action_taken_(false) {
}

ThreeDAPIInfoBarDelegate::~ThreeDAPIInfoBarDelegate() {
  if (message_text_queried_ && !action_taken_) {
    UMA_HISTOGRAM_ENUMERATION("GPU.ThreeDAPIInfoBarDismissal",
                              CLOSED_WITHOUT_ACTION, DISMISSAL_MAX);
  }
}

bool ThreeDAPIInfoBarDelegate::EqualsDelegate(InfoBarDelegate* delegate) const {
  // For the time being, if a given web page is actually using both
  // WebGL and Pepper 3D and both APIs are blocked, just leave the
  // first infobar up. If the user selects "try again", both APIs will
  // be unblocked and the web page reload will succeed.
  return (delegate->AsThreeDAPIInfoBarDelegate() != NULL);
}

ThreeDAPIInfoBarDelegate*
    ThreeDAPIInfoBarDelegate::AsThreeDAPIInfoBarDelegate() {
  return this;
}

string16 ThreeDAPIInfoBarDelegate::GetMessageText() const {
  message_text_queried_ = true;

  string16 api_name;
  switch (requester_) {
    case content::THREE_D_API_TYPE_WEBGL:
      api_name = l10n_util::GetStringUTF16(IDS_3D_APIS_WEBGL_NAME);
      break;
    case content::THREE_D_API_TYPE_PEPPER_3D:
      api_name = l10n_util::GetStringUTF16(IDS_3D_APIS_PEPPER_3D_NAME);
      break;
  }

  return l10n_util::GetStringFUTF16(IDS_3D_APIS_BLOCKED_TEXT,
                                    api_name);
}

string16 ThreeDAPIInfoBarDelegate::GetButtonLabel(
    InfoBarButton button) const {
  return l10n_util::GetStringUTF16((button == BUTTON_OK) ?
      IDS_3D_APIS_BLOCKED_OK_BUTTON_LABEL :
      IDS_3D_APIS_BLOCKED_TRY_AGAIN_BUTTON_LABEL);
}

bool ThreeDAPIInfoBarDelegate::Accept() {
  action_taken_ = true;
  UMA_HISTOGRAM_ENUMERATION("GPU.ThreeDAPIInfoBarDismissal", IGNORED,
                            DISMISSAL_MAX);
  return true;
}

bool ThreeDAPIInfoBarDelegate::Cancel() {
  action_taken_ = true;
  UMA_HISTOGRAM_ENUMERATION("GPU.ThreeDAPIInfoBarDismissal", RELOADED,
                            DISMISSAL_MAX);
  content::GpuDataManager::GetInstance()->UnblockDomainFrom3DAPIs(url_);
  web_contents()->GetController().Reload(true);
  return true;
}

string16 ThreeDAPIInfoBarDelegate::GetLinkText() const {
  return l10n_util::GetStringUTF16(IDS_LEARN_MORE);
}

bool ThreeDAPIInfoBarDelegate::LinkClicked(WindowOpenDisposition disposition) {
  web_contents()->OpenURL(content::OpenURLParams(
      GURL("https://support.google.com/chrome/?p=ib_webgl"),
      content::Referrer(),
      (disposition == CURRENT_TAB) ? NEW_FOREGROUND_TAB : disposition,
      content::PAGE_TRANSITION_LINK,
      false));
  return false;
}


// ThreeDAPIObserver ----------------------------------------------------------

ThreeDAPIObserver::ThreeDAPIObserver() {
  content::GpuDataManager::GetInstance()->AddObserver(this);
}

ThreeDAPIObserver::~ThreeDAPIObserver() {
  content::GpuDataManager::GetInstance()->RemoveObserver(this);
}

void ThreeDAPIObserver::DidBlock3DAPIs(const GURL& url,
                                       int render_process_id,
                                       int render_view_id,
                                       content::ThreeDAPIType requester) {
  content::WebContents* web_contents = tab_util::GetWebContentsByID(
      render_process_id, render_view_id);
  ThreeDAPIInfoBarDelegate::Create(
      InfoBarService::FromWebContents(web_contents), url, requester);
}
