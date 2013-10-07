// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/app_list/apps_model_builder.h"

#include <algorithm>

#include "base/auto_reset.h"
#include "base/prefs/pref_service.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/extension_prefs.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_sorting.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/extensions/install_tracker.h"
#include "chrome/browser/extensions/install_tracker_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/app_list/extension_app_item.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_constants.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/notification_service.h"
#include "ui/gfx/image/image_skia.h"

using extensions::Extension;

namespace {

bool AppPrecedes(const ExtensionAppItem* app1, const ExtensionAppItem* app2) {
  const syncer::StringOrdinal& page1 = app1->GetPageOrdinal();
  const syncer::StringOrdinal& page2 = app2->GetPageOrdinal();
  if (page1.LessThan(page2))
    return true;

  if (page1.Equals(page2))
    return app1->GetAppLaunchOrdinal().LessThan(app2->GetAppLaunchOrdinal());

  return false;
}

bool ShouldDisplayInAppLauncher(Profile* profile,
                                scoped_refptr<const Extension> app) {
  // If it's the web store, check the policy.
  bool blocked_by_policy =
      (app->id() == extension_misc::kWebStoreAppId ||
       app->id() == extension_misc::kEnterpriseWebStoreAppId) &&
      profile->GetPrefs()->GetBoolean(prefs::kHideWebStoreIcon);
  return app->ShouldDisplayInAppLauncher() && !blocked_by_policy;
}

}  // namespace

AppsModelBuilder::AppsModelBuilder(Profile* profile,
                                   app_list::AppListModel::Apps* model,
                                   AppListControllerDelegate* controller)
    : profile_(profile),
      controller_(controller),
      model_(model),
      highlighted_app_pending_(false),
      ignore_changes_(false),
      tracker_(extensions::InstallTrackerFactory::GetForProfile(profile_)) {
  model_->AddObserver(this);
}

AppsModelBuilder::~AppsModelBuilder() {
  OnShutdown();
  model_->RemoveObserver(this);
}

void AppsModelBuilder::Build() {
  DCHECK(model_ && model_->item_count() == 0);

  PopulateApps();
  UpdateHighlight();

  // Start observing after model is built.
  tracker_->AddObserver(this);
}

void AppsModelBuilder::OnBeginExtensionInstall(
    const std::string& extension_id,
    const std::string& extension_name,
    const gfx::ImageSkia& installing_icon,
    bool is_app,
    bool is_platform_app) {
  if (!is_app)
    return;
  InsertApp(new ExtensionAppItem(profile_,
                                 extension_id,
                                 controller_,
                                 extension_name,
                                 installing_icon,
                                 is_platform_app));
  SetHighlightedApp(extension_id);
}

void AppsModelBuilder::OnDownloadProgress(const std::string& extension_id,
                                          int percent_downloaded) {
  int i = FindApp(extension_id);
  if (i == -1)
    return;
  GetAppAt(i)->SetPercentDownloaded(percent_downloaded);
}

void AppsModelBuilder::OnInstallFailure(const std::string& extension_id) {
  int i = FindApp(extension_id);
  if (i == -1)
    return;
  model_->DeleteAt(i);
}

void AppsModelBuilder::OnExtensionLoaded(const Extension* extension) {
  if (!extension->ShouldDisplayInAppLauncher())
    return;

  const int existing_index = FindApp(extension->id());
  if (existing_index != -1) {
    GetAppAt(existing_index)->Reload();
    return;
  }

  InsertApp(new ExtensionAppItem(profile_,
                                 extension->id(),
                                 controller_,
                                 "",
                                 gfx::ImageSkia(),
                                 extension->is_platform_app()));
  UpdateHighlight();
}

void AppsModelBuilder::OnExtensionUnloaded(const Extension* extension) {
  int index = FindApp(extension->id());
  if (index < 0)
    return;
  GetAppAt(index)->UpdateIcon();
}

void AppsModelBuilder::OnExtensionUninstalled(const Extension* extension) {
  int index = FindApp(extension->id());
  if (index < 0)
    return;
  model_->DeleteAt(index);
}

void AppsModelBuilder::OnAppsReordered() {
  ResortApps();
}

void AppsModelBuilder::OnAppInstalledToAppList(
    const std::string& extension_id) {
  SetHighlightedApp(extension_id);
}

void AppsModelBuilder::OnShutdown() {
  if (tracker_) {
    tracker_->RemoveObserver(this);
    tracker_ = NULL;
  }
}

void AppsModelBuilder::AddApps(const ExtensionSet* extensions, Apps* apps) {
  for (ExtensionSet::const_iterator app = extensions->begin();
       app != extensions->end(); ++app) {
    if (ShouldDisplayInAppLauncher(profile_, *app))
      apps->push_back(new ExtensionAppItem(profile_,
                                           (*app)->id(),
                                           controller_,
                                           "",
                                           gfx::ImageSkia(),
                                           (*app)->is_platform_app()));
  }
}

void AppsModelBuilder::PopulateApps() {
  ExtensionService* service =
      extensions::ExtensionSystem::Get(profile_)->extension_service();
  if (!service)
    return;

  Apps apps;
  AddApps(service->extensions(), &apps);
  AddApps(service->disabled_extensions(), &apps);
  AddApps(service->terminated_extensions(), &apps);

  if (apps.empty())
    return;

  service->extension_prefs()->extension_sorting()->FixNTPOrdinalCollisions();
  std::sort(apps.begin(), apps.end(), &AppPrecedes);

  for (size_t i = 0; i < apps.size(); ++i)
    model_->Add(apps[i]);
}

void AppsModelBuilder::ResortApps() {
  // Scan app items in |model_| and put the apps that do not have valid ordinals
  // into |invalid_ordinal_apps|. This is needed to handle uninstalling a
  // terminated app case, where there is no unload notification and uninstall
  // notification comes in after the app's ordinals are cleared.
  // See http://crbug.com/256749.
  Apps apps;
  Apps invalid_ordinal_apps;
  for (size_t i = 0; i < model_->item_count(); ++i) {
    ExtensionAppItem* app = GetAppAt(i);
    if (app->GetPageOrdinal().IsValid() && app->GetAppLaunchOrdinal().IsValid())
      apps.push_back(app);
    else
      invalid_ordinal_apps.push_back(app);
  }

  std::sort(apps.begin(), apps.end(), &AppPrecedes);
  apps.insert(
      apps.end(), invalid_ordinal_apps.begin(), invalid_ordinal_apps.end());

  base::AutoReset<bool> auto_reset(&ignore_changes_, true);

  // Adjusts the order of apps as needed in |model_| based on |apps|.
  for (size_t i = 0; i < apps.size(); ++i) {
    ExtensionAppItem* app_item = apps[i];

    // Finds |app_item| in remaining unsorted part in |model_|.
    for (size_t j = i; j < model_->item_count(); ++j) {
      if (GetAppAt(j) == app_item) {
        model_->Move(j, i);
        break;
      }
    }
  }
}

void AppsModelBuilder::InsertApp(ExtensionAppItem* app) {
  DCHECK(model_);

  size_t start = 0;
  size_t end = model_->item_count();
  while (start < end) {
    size_t mid = (start + end) / 2;

    if (AppPrecedes(GetAppAt(mid), app))
      start = mid + 1;
    else
      end = mid;
  }
  model_->AddAt(start, app);
}

int AppsModelBuilder::FindApp(const std::string& app_id) {
  DCHECK(model_);

  for (size_t i = 0; i < model_->item_count(); ++i) {
    if (GetAppAt(i)->extension_id() == app_id)
      return i;
  }

  return -1;
}

void AppsModelBuilder::SetHighlightedApp(const std::string& extension_id) {
  if (extension_id == highlight_app_id_)
    return;
  ExtensionAppItem* old_app = GetApp(highlight_app_id_);
  if (old_app)
    old_app->SetHighlighted(false);
  highlight_app_id_ = extension_id;
  ExtensionAppItem* new_app = GetApp(highlight_app_id_);
  highlighted_app_pending_ = !new_app;
  if (new_app)
    new_app->SetHighlighted(true);
}

ExtensionAppItem* AppsModelBuilder::GetApp(
    const std::string& extension_id) {
  DCHECK(model_);
  if (extension_id.empty())
    return NULL;

  int index = FindApp(extension_id);
  if (index == -1)
    return NULL;
  return GetAppAt(index);
}

void AppsModelBuilder::UpdateHighlight() {
  DCHECK(model_);
  if (!highlighted_app_pending_ || highlight_app_id_.empty())
    return;

  int index = FindApp(highlight_app_id_);
  if (index == -1)
    return;

  model_->GetItemAt(index)->SetHighlighted(true);
  highlighted_app_pending_ = false;
}

ExtensionAppItem* AppsModelBuilder::GetAppAt(size_t index) {
  DCHECK_LT(index, model_->item_count());
  ChromeAppListItem* item =
      static_cast<ChromeAppListItem*>(model_->GetItemAt(index));
  DCHECK_EQ(item->type(), ChromeAppListItem::TYPE_APP);

  return static_cast<ExtensionAppItem*>(item);
}

void AppsModelBuilder::ListItemsAdded(size_t start, size_t count) {
}

void AppsModelBuilder::ListItemsRemoved(size_t start, size_t count) {
}

void AppsModelBuilder::ListItemMoved(size_t index, size_t target_index) {
  if (ignore_changes_)
    return;

  ExtensionAppItem* prev = target_index > 0 ? GetAppAt(target_index - 1) : NULL;
  ExtensionAppItem* next = target_index + 1 < model_->item_count() ?
      GetAppAt(target_index + 1) : NULL;
  GetAppAt(target_index)->Move(prev, next);
}

void AppsModelBuilder::ListItemsChanged(size_t start, size_t count) {
  NOTREACHED();
}
