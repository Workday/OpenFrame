// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/notifications/notification_permission_infobar_delegate.h"

#include "chrome/browser/android/android_theme_resources.h"
#include "chrome/browser/infobars/infobar_service.h"
#include "chrome/grit/generated_resources.h"
#include "components/infobars/core/infobar.h"
#include "components/url_formatter/elide_url.h"
#include "net/base/escape.h"
#include "ui/base/l10n/l10n_util.h"

// static
infobars::InfoBar* NotificationPermissionInfobarDelegate::Create(
    InfoBarService* infobar_service,
    const GURL& requesting_frame,
    const std::string& display_languages,
    const base::Callback<void(bool, bool)>& callback) {
  return infobar_service->AddInfoBar(infobar_service->CreateConfirmInfoBar(
      scoped_ptr<ConfirmInfoBarDelegate>(
          new NotificationPermissionInfobarDelegate(requesting_frame,
                                                    display_languages,
                                                    callback))));
}

NotificationPermissionInfobarDelegate::NotificationPermissionInfobarDelegate(
    const GURL& requesting_frame,
    const std::string& display_languages,
    const base::Callback<void(bool, bool)>& callback)
    : PermissionInfobarDelegate(requesting_frame,
                                CONTENT_SETTINGS_TYPE_NOTIFICATIONS,
                                callback),
      requesting_frame_(requesting_frame),
      display_languages_(display_languages) {}

NotificationPermissionInfobarDelegate::~NotificationPermissionInfobarDelegate()
    {}

int NotificationPermissionInfobarDelegate::GetIconId() const {
  return IDR_ANDROID_INFOBAR_NOTIFICATIONS;
}

base::string16 NotificationPermissionInfobarDelegate::GetMessageText() const {
  return l10n_util::GetStringFUTF16(
      IDS_NOTIFICATION_PERMISSIONS,
      url_formatter::FormatUrlForSecurityDisplay(requesting_frame_.GetOrigin(),
                                                 display_languages_));
}
