// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/search/instant_page.h"

#include "apps/app_launcher.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/history/most_visited_tiles_experiment.h"
#include "chrome/browser/history/top_sites.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search/instant_service.h"
#include "chrome/browser/search/instant_service_factory.h"
#include "chrome/browser/search/search.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/search/instant_tab.h"
#include "chrome/browser/ui/search/search_model.h"
#include "chrome/browser/ui/search/search_tab_helper.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_utils.h"
#include "chrome/common/render_messages.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/frame_navigate_params.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/font.h"

InstantPage::Delegate::~Delegate() {
}

InstantPage::~InstantPage() {
  if (contents())
    SearchTabHelper::FromWebContents(contents())->model()->RemoveObserver(this);

  // |profile_| may be NULL during unit tests.
  if (profile_) {
    InstantService* instant_service =
        InstantServiceFactory::GetForProfile(profile_);
    instant_service->RemoveObserver(this);
  }
}

bool InstantPage::supports_instant() const {
  return contents() ?
      SearchTabHelper::FromWebContents(contents())->SupportsInstant() : false;
}

const std::string& InstantPage::instant_url() const {
  return instant_url_;
}

bool InstantPage::IsLocal() const {
  return contents() &&
      contents()->GetURL() == GURL(chrome::kChromeSearchLocalNtpUrl);
}

void InstantPage::InitializeFonts() {
#if defined(OS_MACOSX)
  // This value should be kept in sync with OmniboxViewMac::GetFieldFont.
  const gfx::Font& omnibox_font =
      ui::ResourceBundle::GetSharedInstance().GetFont(
          ui::ResourceBundle::MediumFont).DeriveFont(1);
#else
  const gfx::Font& omnibox_font =
      ui::ResourceBundle::GetSharedInstance().GetFont(
          ui::ResourceBundle::MediumFont);
#endif
  sender()->SetFontInformation(UTF8ToUTF16(omnibox_font.GetFontName()),
                               omnibox_font.GetFontSize());
}

void InstantPage::InitializePromos() {
  sender()->SetPromoInformation(apps::IsAppLauncherEnabled());
}

InstantPage::InstantPage(Delegate* delegate, const std::string& instant_url,
                         Profile* profile, bool is_incognito)
    : profile_(profile),
      delegate_(delegate),
      ipc_sender_(InstantIPCSender::Create(is_incognito)),
      instant_url_(instant_url),
      is_incognito_(is_incognito) {
  // |profile_| may be NULL during unit tests.
  if (profile_) {
    InstantService* instant_service =
        InstantServiceFactory::GetForProfile(profile_);
    instant_service->AddObserver(this);
  }
}

void InstantPage::SetContents(content::WebContents* web_contents) {
  ClearContents();

  if (!web_contents)
    return;

  sender()->SetContents(web_contents);
  Observe(web_contents);
  SearchModel* model = SearchTabHelper::FromWebContents(contents())->model();
  model->AddObserver(this);

  // Already know whether the page supports instant.
  if (model->instant_support() != INSTANT_SUPPORT_UNKNOWN)
    InstantSupportDetermined(model->instant_support() == INSTANT_SUPPORT_YES);
}

bool InstantPage::ShouldProcessAboutToNavigateMainFrame() {
  return false;
}

bool InstantPage::ShouldProcessFocusOmnibox() {
  return false;
}

bool InstantPage::ShouldProcessNavigateToURL() {
  return false;
}

bool InstantPage::ShouldProcessPasteIntoOmnibox() {
  return false;
}

bool InstantPage::ShouldProcessDeleteMostVisitedItem() {
  return false;
}

bool InstantPage::ShouldProcessUndoMostVisitedDeletion() {
  return false;
}

bool InstantPage::ShouldProcessUndoAllMostVisitedDeletions() {
  return false;
}

bool InstantPage::OnMessageReceived(const IPC::Message& message) {
  if (is_incognito_)
    return false;

  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(InstantPage, message)
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_FocusOmnibox, OnFocusOmnibox)
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_SearchBoxNavigate,
                        OnSearchBoxNavigate);
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_PasteAndOpenDropdown,
                        OnSearchBoxPaste);
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_CountMouseover, OnCountMouseover);
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_SearchBoxDeleteMostVisitedItem,
                        OnDeleteMostVisitedItem);
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_SearchBoxUndoMostVisitedDeletion,
                        OnUndoMostVisitedDeletion);
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_SearchBoxUndoAllMostVisitedDeletions,
                        OnUndoAllMostVisitedDeletions);
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void InstantPage::DidCommitProvisionalLoadForFrame(
    int64 /* frame_id */,
    bool is_main_frame,
    const GURL& url,
    content::PageTransition /* transition_type */,
    content::RenderViewHost* /* render_view_host */) {
  if (is_main_frame && ShouldProcessAboutToNavigateMainFrame())
    delegate_->InstantPageAboutToNavigateMainFrame(contents(), url);
}

void InstantPage::DidNavigateMainFrame(
    const content::LoadCommittedDetails& details,
    const content::FrameNavigateParams& /* params */) {
  // A 204 can be sent by the search provider as a lightweight signal
  // to fall back to the local page, and we obviously want to fall back
  // if we get any response code that indicates an error.
  if (details.http_status_code == 204 || details.http_status_code >= 400)
    delegate_->InstantPageLoadFailed(contents());
}

void InstantPage::DidFailProvisionalLoad(
    int64 /* frame_id */,
    bool is_main_frame,
    const GURL& /* validated_url */,
    int /* error_code */,
    const string16& /* error_description */,
    content::RenderViewHost* /* render_view_host */) {
  if (is_main_frame)
    delegate_->InstantPageLoadFailed(contents());
}

void InstantPage::ThemeInfoChanged(const ThemeBackgroundInfo& theme_info) {
  sender()->SendThemeBackgroundInfo(theme_info);
}

void InstantPage::MostVisitedItemsChanged(
    const std::vector<InstantMostVisitedItem>& items) {
  std::vector<InstantMostVisitedItem> items_copy(items);
  MaybeRemoveMostVisitedItems(&items_copy);

  sender()->SendMostVisitedItems(items_copy);

  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_INSTANT_SENT_MOST_VISITED_ITEMS,
      content::Source<InstantPage>(this),
      content::NotificationService::NoDetails());
}

void InstantPage::ModelChanged(const SearchModel::State& old_state,
                               const SearchModel::State& new_state) {
  if (old_state.instant_support != new_state.instant_support)
    InstantSupportDetermined(new_state.instant_support == INSTANT_SUPPORT_YES);
}

void InstantPage::InstantSupportDetermined(bool supports_instant) {
  delegate_->InstantSupportDetermined(contents(), supports_instant);

  // If the page doesn't support Instant, stop listening to it.
  if (!supports_instant)
    ClearContents();
}

void InstantPage::OnFocusOmnibox(int page_id, OmniboxFocusState state) {
  if (!contents()->IsActiveEntry(page_id))
    return;

  SearchTabHelper::FromWebContents(contents())->InstantSupportChanged(true);
  if (!ShouldProcessFocusOmnibox())
    return;

  delegate_->FocusOmnibox(contents(), state);
}

void InstantPage::OnSearchBoxNavigate(int page_id,
                                      const GURL& url,
                                      content::PageTransition transition,
                                      WindowOpenDisposition disposition,
                                      bool is_search_type) {
  if (!contents()->IsActiveEntry(page_id))
    return;

  SearchTabHelper::FromWebContents(contents())->InstantSupportChanged(true);
  if (!ShouldProcessNavigateToURL())
    return;

  delegate_->NavigateToURL(
      contents(), url, transition, disposition, is_search_type);
}

void InstantPage::OnSearchBoxPaste(int page_id, const string16& text) {
  if (!contents()->IsActiveEntry(page_id))
    return;

  SearchTabHelper::FromWebContents(contents())->InstantSupportChanged(true);
  if (!ShouldProcessPasteIntoOmnibox())
    return;

  delegate_->PasteIntoOmnibox(contents(), text);
}

void InstantPage::OnCountMouseover(int page_id) {
  if (!contents()->IsActiveEntry(page_id))
    return;

  InstantTab::CountMouseover(contents());
}

void InstantPage::OnDeleteMostVisitedItem(int page_id, const GURL& url) {
  if (!contents()->IsActiveEntry(page_id))
    return;

  SearchTabHelper::FromWebContents(contents())->InstantSupportChanged(true);
  if (!ShouldProcessDeleteMostVisitedItem())
    return;

  delegate_->DeleteMostVisitedItem(url);
}

void InstantPage::OnUndoMostVisitedDeletion(int page_id, const GURL& url) {
  if (!contents()->IsActiveEntry(page_id))
    return;

  SearchTabHelper::FromWebContents(contents())->InstantSupportChanged(true);
  if (!ShouldProcessUndoMostVisitedDeletion())
    return;

  delegate_->UndoMostVisitedDeletion(url);
}

void InstantPage::OnUndoAllMostVisitedDeletions(int page_id) {
  if (!contents()->IsActiveEntry(page_id))
    return;

  SearchTabHelper::FromWebContents(contents())->InstantSupportChanged(true);
  if (!ShouldProcessUndoAllMostVisitedDeletions())
    return;

  delegate_->UndoAllMostVisitedDeletions();
}

void InstantPage::ClearContents() {
  if (contents())
    SearchTabHelper::FromWebContents(contents())->model()->RemoveObserver(this);

  sender()->SetContents(NULL);
  Observe(NULL);
}

void InstantPage::MaybeRemoveMostVisitedItems(
    std::vector<InstantMostVisitedItem>* items) {
// The code below uses APIs not available on Android and the experiment should
// not run there.
#if !defined(OS_ANDROID)
  if (!history::MostVisitedTilesExperiment::IsDontShowOpenURLsEnabled())
    return;

  Browser* browser = chrome::FindBrowserWithProfile(profile_,
                                                    chrome::GetActiveDesktop());
  if (!browser)
    return;

  TabStripModel* tab_strip_model = browser->tab_strip_model();
  history::TopSites* top_sites = profile_->GetTopSites();
  if (!tab_strip_model || !top_sites) {
    NOTREACHED();
    return;
  }

  std::set<std::string> open_urls;
  chrome::GetOpenUrls(*tab_strip_model, *top_sites, &open_urls);
  history::MostVisitedTilesExperiment::RemoveItemsMatchingOpenTabs(
      open_urls, items);

#endif
}
