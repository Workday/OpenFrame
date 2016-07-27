// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/permissions/permission_infobar_delegate.h"

#include "chrome/browser/permissions/permission_uma_util.h"
#include "chrome/grit/generated_resources.h"
#include "components/infobars/core/infobar.h"
#include "ui/base/l10n/l10n_util.h"

PermissionInfobarDelegate::~PermissionInfobarDelegate() {
  if (!action_taken_)
    PermissionUmaUtil::PermissionIgnored(type_, requesting_origin_);
}

PermissionInfobarDelegate::PermissionInfobarDelegate(
    const GURL& requesting_origin,
    ContentSettingsType type,
    const base::Callback<void(bool, bool)>& callback)
    : requesting_origin_(requesting_origin),
      action_taken_(false),
      type_(type),
      callback_(callback) {
}

infobars::InfoBarDelegate::Type
PermissionInfobarDelegate::GetInfoBarType() const {
  return PAGE_ACTION_TYPE;
}

void PermissionInfobarDelegate::InfoBarDismissed() {
  SetPermission(false, false);
}

PermissionInfobarDelegate*
PermissionInfobarDelegate::AsPermissionInfobarDelegate() {
  return this;
}

base::string16 PermissionInfobarDelegate::GetButtonLabel(
    InfoBarButton button) const {
  return l10n_util::GetStringUTF16((button == BUTTON_OK) ?
      IDS_PERMISSION_ALLOW : IDS_PERMISSION_DENY);
}

bool PermissionInfobarDelegate::Accept() {
  SetPermission(true, true);
  return true;
}

bool PermissionInfobarDelegate::Cancel() {
  SetPermission(true, false);
  return true;
}

void PermissionInfobarDelegate::SetPermission(bool update_content_setting,
                                              bool allowed) {
  action_taken_ = true;
  callback_.Run(update_content_setting, allowed);
}
