// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/blocked_content/popup_blocker_tab_helper.h"

#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/content_settings/host_content_settings_map.h"
#include "chrome/browser/content_settings/tab_specific_content_settings.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/render_messages.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_view.h"
#include "third_party/WebKit/public/web/WebWindowFeatures.h"

using WebKit::WebWindowFeatures;

DEFINE_WEB_CONTENTS_USER_DATA_KEY(PopupBlockerTabHelper);

struct PopupBlockerTabHelper::BlockedRequest {
  BlockedRequest(const chrome::NavigateParams& params,
                 const WebWindowFeatures& window_features)
      : params(params), window_features(window_features) {}

  chrome::NavigateParams params;
  WebWindowFeatures window_features;
};

PopupBlockerTabHelper::PopupBlockerTabHelper(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents) {
}

PopupBlockerTabHelper::~PopupBlockerTabHelper() {
}

void PopupBlockerTabHelper::DidNavigateMainFrame(
    const content::LoadCommittedDetails& details,
    const content::FrameNavigateParams& params) {
  // Clear all page actions, blocked content notifications and browser actions
  // for this tab, unless this is an in-page navigation.
  if (details.is_in_page)
    return;

  // Close blocked popups.
  if (!blocked_popups_.IsEmpty()) {
    blocked_popups_.Clear();
    PopupNotificationVisibilityChanged(false);
  }
}

void PopupBlockerTabHelper::PopupNotificationVisibilityChanged(
    bool visible) {
  if (!web_contents()->IsBeingDestroyed()) {
    TabSpecificContentSettings::FromWebContents(web_contents())->
        SetPopupsBlocked(visible);
  }
}

bool PopupBlockerTabHelper::MaybeBlockPopup(
    const chrome::NavigateParams& params,
    const WebWindowFeatures& window_features) {
  // A page can't spawn popups (or do anything else, either) until its load
  // commits, so when we reach here, the popup was spawned by the
  // NavigationController's last committed entry, not the active entry.  For
  // example, if a page opens a popup in an onunload() handler, then the active
  // entry is the page to be loaded as we navigate away from the unloading
  // page.  For this reason, we can't use GetURL() to get the opener URL,
  // because it returns the active entry.
  content::NavigationEntry* entry =
      web_contents()->GetController().GetLastCommittedEntry();
  GURL creator = entry ? entry->GetVirtualURL() : GURL();
  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());

  if (creator.is_valid() &&
      profile->GetHostContentSettingsMap()->GetContentSetting(
          creator, creator, CONTENT_SETTINGS_TYPE_POPUPS, std::string()) ==
          CONTENT_SETTING_ALLOW) {
    return false;
  } else {
    blocked_popups_.Add(new BlockedRequest(params, window_features));
    TabSpecificContentSettings::FromWebContents(web_contents())->
        OnContentBlocked(CONTENT_SETTINGS_TYPE_POPUPS, std::string());
    return true;
  }
}

void PopupBlockerTabHelper::AddBlockedPopup(
    const GURL& target_url,
    const content::Referrer& referrer,
    WindowOpenDisposition disposition,
    const WebWindowFeatures& features,
    bool user_gesture,
    bool opener_suppressed) {
  chrome::NavigateParams nav_params(
      Profile::FromBrowserContext(web_contents()->GetBrowserContext()),
      target_url,
      content::PAGE_TRANSITION_LINK);
  nav_params.referrer = referrer;
  nav_params.source_contents = web_contents();
  nav_params.is_renderer_initiated = true;
  nav_params.tabstrip_add_types = TabStripModel::ADD_ACTIVE;
  nav_params.window_action = chrome::NavigateParams::SHOW_WINDOW;
  nav_params.user_gesture = user_gesture;
  nav_params.should_set_opener = !opener_suppressed;
  web_contents()->GetView()->GetContainerBounds(&nav_params.window_bounds);
  if (features.xSet)
    nav_params.window_bounds.set_x(features.x);
  if (features.ySet)
    nav_params.window_bounds.set_y(features.y);
  if (features.widthSet)
    nav_params.window_bounds.set_width(features.width);
  if (features.heightSet)
    nav_params.window_bounds.set_height(features.height);

  // Compare RenderViewImpl::show().
  if (!user_gesture && disposition != NEW_BACKGROUND_TAB)
    nav_params.disposition = NEW_POPUP;
  else
    nav_params.disposition = disposition;

  blocked_popups_.Add(new BlockedRequest(nav_params, features));
  TabSpecificContentSettings::FromWebContents(web_contents())->
      OnContentBlocked(CONTENT_SETTINGS_TYPE_POPUPS, std::string());
}

void PopupBlockerTabHelper::ShowBlockedPopup(int32 id) {
  BlockedRequest* popup = blocked_popups_.Lookup(id);
  if (!popup)
    return;
  chrome::Navigate(&popup->params);
  if (popup->params.target_contents) {
    popup->params.target_contents->Send(new ChromeViewMsg_SetWindowFeatures(
        popup->params.target_contents->GetRoutingID(), popup->window_features));
  }
  blocked_popups_.Remove(id);
  if (blocked_popups_.IsEmpty())
    PopupNotificationVisibilityChanged(false);
}

size_t PopupBlockerTabHelper::GetBlockedPopupsCount() const {
  return blocked_popups_.size();
}

std::map<int32, GURL> PopupBlockerTabHelper::GetBlockedPopupRequests() {
  std::map<int32, GURL> result;
  for (IDMap<BlockedRequest, IDMapOwnPointer>::const_iterator iter(
           &blocked_popups_);
       !iter.IsAtEnd();
       iter.Advance()) {
    result[iter.GetCurrentKey()] = iter.GetCurrentValue()->params.url;
  }
  return result;
}
