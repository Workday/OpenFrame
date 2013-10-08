// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media/media_stream_infobar_delegate.h"

#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/google/google_util.h"
#include "chrome/browser/infobars/infobar_service.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/web_contents.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "url/gurl.h"

namespace {

enum DevicePermissionActions {
  kAllowHttps = 0,
  kAllowHttp,
  kDeny,
  kCancel,
  kPermissionActionsMax  // Must always be last!
};

}  // namespace

MediaStreamInfoBarDelegate::~MediaStreamInfoBarDelegate() {
}

// static
bool MediaStreamInfoBarDelegate::Create(
    content::WebContents* web_contents,
    const content::MediaStreamRequest& request,
    const content::MediaResponseCallback& callback) {
  scoped_ptr<MediaStreamDevicesController> controller(
      new MediaStreamDevicesController(web_contents, request, callback));
  if (controller->DismissInfoBarAndTakeActionOnSettings())
    return false;

  InfoBarService* infobar_service =
      InfoBarService::FromWebContents(web_contents);
  scoped_ptr<InfoBarDelegate> infobar(
      new MediaStreamInfoBarDelegate(infobar_service, controller.Pass()));
  for (size_t i = 0; i < infobar_service->infobar_count(); ++i) {
    InfoBarDelegate* old_infobar =
        infobar_service->infobar_at(i)->AsMediaStreamInfoBarDelegate();
    if (old_infobar) {
      infobar_service->ReplaceInfoBar(old_infobar, infobar.Pass());
      return true;
    }
  }
  infobar_service->AddInfoBar(infobar.Pass());
  return true;
}

MediaStreamInfoBarDelegate::MediaStreamInfoBarDelegate(
    InfoBarService* infobar_service,
    scoped_ptr<MediaStreamDevicesController> controller)
    : ConfirmInfoBarDelegate(infobar_service),
      controller_(controller.Pass()) {
  DCHECK(controller_.get());
  DCHECK(controller_->has_audio() || controller_->has_video());
}

void MediaStreamInfoBarDelegate::InfoBarDismissed() {
  // Deny the request if the infobar was closed with the 'x' button, since
  // we don't want WebRTC to be waiting for an answer that will never come.
  UMA_HISTOGRAM_ENUMERATION("Media.DevicePermissionActions",
                            kCancel, kPermissionActionsMax);
  controller_->Deny(false);
}

int MediaStreamInfoBarDelegate::GetIconID() const {
  return controller_->has_video() ?
      IDR_INFOBAR_MEDIA_STREAM_CAMERA : IDR_INFOBAR_MEDIA_STREAM_MIC;
}

InfoBarDelegate::Type MediaStreamInfoBarDelegate::GetInfoBarType() const {
  return PAGE_ACTION_TYPE;
}

MediaStreamInfoBarDelegate*
    MediaStreamInfoBarDelegate::AsMediaStreamInfoBarDelegate() {
  return this;
}

string16 MediaStreamInfoBarDelegate::GetMessageText() const {
  int message_id = IDS_MEDIA_CAPTURE_AUDIO_AND_VIDEO;
  if (!controller_->has_audio())
    message_id = IDS_MEDIA_CAPTURE_VIDEO_ONLY;
  else if (!controller_->has_video())
    message_id = IDS_MEDIA_CAPTURE_AUDIO_ONLY;
  return l10n_util::GetStringFUTF16(
      message_id, UTF8ToUTF16(controller_->GetSecurityOriginSpec()));
}

string16 MediaStreamInfoBarDelegate::GetButtonLabel(
    InfoBarButton button) const {
  return l10n_util::GetStringUTF16((button == BUTTON_OK) ?
      IDS_MEDIA_CAPTURE_ALLOW : IDS_MEDIA_CAPTURE_DENY);
}

bool MediaStreamInfoBarDelegate::Accept() {
  GURL origin(controller_->GetSecurityOriginSpec());
  if (origin.SchemeIsSecure()) {
    UMA_HISTOGRAM_ENUMERATION("Media.DevicePermissionActions",
                              kAllowHttps, kPermissionActionsMax);
  } else {
    UMA_HISTOGRAM_ENUMERATION("Media.DevicePermissionActions",
                              kAllowHttp, kPermissionActionsMax);
  }
  controller_->Accept(true);
  return true;
}

bool MediaStreamInfoBarDelegate::Cancel() {
  UMA_HISTOGRAM_ENUMERATION("Media.DevicePermissionActions",
                            kDeny, kPermissionActionsMax);
  controller_->Deny(true);
  return true;
}

string16 MediaStreamInfoBarDelegate::GetLinkText() const {
  return l10n_util::GetStringUTF16(IDS_LEARN_MORE);
}

bool MediaStreamInfoBarDelegate::LinkClicked(
    WindowOpenDisposition disposition) {
  web_contents()->OpenURL(content::OpenURLParams(
      google_util::AppendGoogleLocaleParam(
          GURL(chrome::kMediaAccessLearnMoreUrl)),
      content::Referrer(),
      (disposition == CURRENT_TAB) ? NEW_FOREGROUND_TAB : disposition,
      content::PAGE_TRANSITION_LINK,
      false));

  return false;  // Do not dismiss the info bar.
}
