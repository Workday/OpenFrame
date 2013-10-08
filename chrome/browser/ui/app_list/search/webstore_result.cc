// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/app_list/search/webstore_result.h"

#include <vector>

#include "base/bind.h"
#include "base/memory/ref_counted.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/extensions/install_tracker.h"
#include "chrome/browser/extensions/install_tracker_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/app_list/app_list_controller_delegate.h"
#include "chrome/browser/ui/app_list/search/webstore_installer.h"
#include "chrome/browser/ui/app_list/search/webstore_result_icon_source.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/extensions/application_launch.h"
#include "chrome/common/extensions/extension.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

namespace app_list {

WebstoreResult::WebstoreResult(Profile* profile,
                               const std::string& app_id,
                               const std::string& localized_name,
                               const GURL& icon_url,
                               AppListControllerDelegate* controller)
    : profile_(profile),
      app_id_(app_id),
      localized_name_(localized_name),
      icon_url_(icon_url),
      weak_factory_(this),
      controller_(controller),
      install_tracker_(NULL) {
  set_id(extensions::Extension::GetBaseURLFromExtensionId(app_id_).spec());
  set_relevance(0.0);  // What is the right value to use?

  set_title(UTF8ToUTF16(localized_name_));
  SetDefaultDetails();

  UpdateActions();

  const int kIconSize = 32;
  icon_ = gfx::ImageSkia(
      new WebstoreResultIconSource(
          base::Bind(&WebstoreResult::OnIconLoaded,
                     weak_factory_.GetWeakPtr()),
          profile_->GetRequestContext(),
          icon_url_,
          kIconSize),
      gfx::Size(kIconSize, kIconSize));
  SetIcon(icon_);

  StartObservingInstall();
}

WebstoreResult::~WebstoreResult() {
  StopObservingInstall();
}

void WebstoreResult::Open(int event_flags) {
  const GURL store_url(extension_urls::GetWebstoreItemDetailURLPrefix() +
                       app_id_);
  chrome::NavigateParams params(profile_,
                                store_url,
                                content::PAGE_TRANSITION_LINK);
  params.disposition = ui::DispositionFromEventFlags(event_flags);
  chrome::Navigate(&params);
}

void WebstoreResult::InvokeAction(int action_index, int event_flags) {
  DCHECK_EQ(0, action_index);
  StartInstall();
}

scoped_ptr<ChromeSearchResult> WebstoreResult::Duplicate() {
  return scoped_ptr<ChromeSearchResult>(new WebstoreResult(
      profile_, app_id_, localized_name_, icon_url_, controller_)).Pass();
}

void WebstoreResult::UpdateActions() {
  Actions actions;

  const bool is_otr = profile_->IsOffTheRecord();
  const bool is_installed = !!extensions::ExtensionSystem::Get(profile_)->
      extension_service()->GetInstalledExtension(app_id_);

  if (!is_otr && !is_installed && !is_installing()) {
    actions.push_back(Action(
        l10n_util::GetStringUTF16(IDS_EXTENSION_INLINE_INSTALL_PROMPT_TITLE),
        base::string16()));
  }

  SetActions(actions);
}

void WebstoreResult::SetDefaultDetails() {
  const base::string16 details =
      l10n_util::GetStringUTF16(IDS_EXTENSION_WEB_STORE_TITLE);
  Tags details_tags;
  details_tags.push_back(Tag(SearchResult::Tag::DIM, 0, details.length()));

  set_details(details);
  set_details_tags(details_tags);
}

void WebstoreResult::OnIconLoaded() {
  // Remove the existing image reps since the icon data is loaded and they
  // need to be re-created.
  const std::vector<gfx::ImageSkiaRep>& image_reps = icon_.image_reps();
  for (size_t i = 0; i < image_reps.size(); ++i)
    icon_.RemoveRepresentation(image_reps[i].scale_factor());

  SetIcon(icon_);
}

void WebstoreResult::StartInstall() {
  SetPercentDownloaded(0);
  SetIsInstalling(true);

  scoped_refptr<WebstoreInstaller> installer =
      new WebstoreInstaller(
          app_id_,
          profile_,
          controller_->GetAppListWindow(),
          base::Bind(&WebstoreResult::InstallCallback,
                     weak_factory_.GetWeakPtr()));
  installer->BeginInstall();
}

void WebstoreResult::InstallCallback(bool success, const std::string& error) {
  if (!success) {
    LOG(ERROR) << "Failed to install app, error=" << error;
    SetIsInstalling(false);
    return;
  }

  // Success handling is continued in OnExtensionInstalled.
  SetPercentDownloaded(100);
}

void WebstoreResult::StartObservingInstall() {
  DCHECK(!install_tracker_);

  install_tracker_ = extensions::InstallTrackerFactory::GetForProfile(profile_);
  install_tracker_->AddObserver(this);
}

void WebstoreResult::StopObservingInstall() {
  if (install_tracker_)
    install_tracker_->RemoveObserver(this);

  install_tracker_ = NULL;
}

void WebstoreResult::OnBeginExtensionInstall(
    const std::string& extension_id,
    const std::string& extension_name,
    const gfx::ImageSkia& installing_icon,
    bool is_app,
    bool is_platform_app) {}

void WebstoreResult::OnDownloadProgress(const std::string& extension_id,
                                        int percent_downloaded) {
  if (extension_id != app_id_ || percent_downloaded < 0)
    return;

  SetPercentDownloaded(percent_downloaded);
}

void WebstoreResult::OnInstallFailure(const std::string& extension_id) {}

void WebstoreResult::OnExtensionInstalled(
    const extensions::Extension* extension) {
  if (extension->id() != app_id_)
    return;

  SetIsInstalling(false);
  UpdateActions();
  NotifyItemInstalled();
}

void WebstoreResult::OnExtensionLoaded(
    const extensions::Extension* extension) {}

void WebstoreResult::OnExtensionUnloaded(
    const extensions::Extension* extension) {}

void WebstoreResult::OnExtensionUninstalled(
    const extensions::Extension* extension) {}

void WebstoreResult::OnAppsReordered() {}

void WebstoreResult::OnAppInstalledToAppList(const std::string& extension_id) {}

void WebstoreResult::OnShutdown() {
  StopObservingInstall();
}

ChromeSearchResultType WebstoreResult::GetType() {
  return SEARCH_WEBSTORE_SEARCH_RESULT;
}

}  // namespace app_list
