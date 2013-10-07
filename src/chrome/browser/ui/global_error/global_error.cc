// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/global_error/global_error.h"

#include "base/logging.h"
#include "chrome/browser/ui/global_error/global_error_bubble_view_base.h"
#include "grit/theme_resources.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/image/image.h"

GlobalError::GlobalError()
    : has_shown_bubble_view_(false),
      bubble_view_(NULL) {
}

GlobalError::~GlobalError() {
}

GlobalError::Severity GlobalError::GetSeverity() {
  return SEVERITY_MEDIUM;
}

int GlobalError::MenuItemIconResourceID() {
  // If you change this make sure to also change the bubble icon and the wrench
  // icon color.
  return IDR_INPUT_ALERT_MENU;
}

bool GlobalError::HasShownBubbleView() {
  return has_shown_bubble_view_;
}

void GlobalError::ShowBubbleView(Browser* browser) {
  has_shown_bubble_view_ = true;
#if defined(OS_ANDROID)
  // http://crbug.com/136506
  NOTIMPLEMENTED() << "Chrome for Android doesn't support global errors";
#else
  bubble_view_ =
      GlobalErrorBubbleViewBase::ShowBubbleView(browser, AsWeakPtr());
#endif
}

GlobalErrorBubbleViewBase* GlobalError::GetBubbleView() {
  return bubble_view_;
}

gfx::Image GlobalError::GetBubbleViewIcon() {
  // If you change this make sure to also change the menu icon and the wrench
  // icon color.
  return ResourceBundle::GetSharedInstance().GetNativeImageNamed(
      IDR_INPUT_ALERT);
}

void GlobalError::BubbleViewDidClose(Browser* browser) {
  DCHECK(browser);
  bubble_view_ = NULL;
  OnBubbleViewDidClose(browser);
}
