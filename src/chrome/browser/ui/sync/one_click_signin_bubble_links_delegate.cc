// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/sync/one_click_signin_bubble_links_delegate.h"

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/common/url_constants.h"

OneClickSigninBubbleLinksDelegate::OneClickSigninBubbleLinksDelegate(
    Browser* browser) : browser_(browser) {}

OneClickSigninBubbleLinksDelegate::~OneClickSigninBubbleLinksDelegate() {}

void OneClickSigninBubbleLinksDelegate::OnLearnMoreLinkClicked(
    bool is_dialog) {
  chrome::NavigateParams params(browser_,
                                GURL(chrome::kChromeSyncLearnMoreURL),
                                content::PAGE_TRANSITION_LINK);
  params.disposition = is_dialog ? NEW_WINDOW : NEW_FOREGROUND_TAB;
  chrome::Navigate(&params);
}

void OneClickSigninBubbleLinksDelegate::OnAdvancedLinkClicked() {
  chrome::NavigateParams params(browser_,
                                GURL(chrome::kChromeUISettingsURL),
                                content::PAGE_TRANSITION_LINK);
  params.disposition = CURRENT_TAB;
  chrome::Navigate(&params);
}
