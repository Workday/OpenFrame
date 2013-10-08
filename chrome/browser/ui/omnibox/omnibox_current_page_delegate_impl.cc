// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/omnibox/omnibox_current_page_delegate_impl.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/autocomplete/autocomplete_match.h"
#include "chrome/browser/extensions/api/omnibox/omnibox_api.h"
#include "chrome/browser/predictors/autocomplete_action_predictor.h"
#include "chrome/browser/predictors/autocomplete_action_predictor_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search/search.h"
#include "chrome/browser/search_engines/template_url_service.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/sessions/session_tab_helper.h"
#include "chrome/browser/ui/omnibox/omnibox_edit_controller.h"
#include "chrome/browser/ui/search/search_tab_helper.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

OmniboxCurrentPageDelegateImpl::OmniboxCurrentPageDelegateImpl(
    OmniboxEditController* controller,
    Profile* profile)
    : controller_(controller),
      profile_(profile) {}

OmniboxCurrentPageDelegateImpl::~OmniboxCurrentPageDelegateImpl() {}

bool OmniboxCurrentPageDelegateImpl::CurrentPageExists() const {
  return (controller_->GetWebContents() != NULL);
}

const GURL& OmniboxCurrentPageDelegateImpl::GetURL() const {
  return controller_->GetWebContents()->GetURL();
}

bool OmniboxCurrentPageDelegateImpl::IsInstantNTP() const {
  return chrome::IsInstantNTP(controller_->GetWebContents());
}

bool OmniboxCurrentPageDelegateImpl::IsSearchResultsPage() const {
  Profile* profile = Profile::FromBrowserContext(
      controller_->GetWebContents()->GetBrowserContext());
  return TemplateURLServiceFactory::GetForProfile(profile)->
      IsSearchResultsPageFromDefaultSearchProvider(GetURL());
}

bool OmniboxCurrentPageDelegateImpl::IsLoading() const {
  return controller_->GetWebContents()->IsLoading();
}

content::NavigationController&
    OmniboxCurrentPageDelegateImpl::GetNavigationController() const {
  return controller_->GetWebContents()->GetController();
}

const SessionID& OmniboxCurrentPageDelegateImpl::GetSessionID() const {
  return SessionTabHelper::FromWebContents(
      controller_->GetWebContents())->session_id();
}

bool OmniboxCurrentPageDelegateImpl::ProcessExtensionKeyword(
    TemplateURL* template_url,
    const AutocompleteMatch& match,
    WindowOpenDisposition disposition) {
  if (!template_url->IsExtensionKeyword())
    return false;

  // Strip the keyword + leading space off the input.
  size_t prefix_length = match.keyword.length() + 1;
  extensions::ExtensionOmniboxEventRouter::OnInputEntered(
      controller_->GetWebContents(),
      template_url->GetExtensionId(),
      UTF16ToUTF8(match.fill_into_edit.substr(prefix_length)),
      disposition);

  return true;
}

void OmniboxCurrentPageDelegateImpl::NotifySearchTabHelper(
    bool user_input_in_progress,
    bool cancelling) {
  if (!controller_->GetWebContents())
    return;
  SearchTabHelper::FromWebContents(
      controller_->GetWebContents())->OmniboxEditModelChanged(
          user_input_in_progress, cancelling);
}

void OmniboxCurrentPageDelegateImpl::DoPrerender(
    const AutocompleteMatch& match) {
  content::WebContents* web_contents = controller_->GetWebContents();
  gfx::Rect container_bounds;
  web_contents->GetView()->GetContainerBounds(&container_bounds);
  predictors::AutocompleteActionPredictorFactory::GetForProfile(profile_)->
      StartPrerendering(
          match.destination_url,
          web_contents->GetController().GetSessionStorageNamespaceMap(),
          container_bounds.size());
}
