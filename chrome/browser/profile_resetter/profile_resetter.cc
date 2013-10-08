// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/profile_resetter/profile_resetter.h"

#include "base/prefs/pref_service.h"
#include "chrome/browser/browsing_data/browsing_data_helper.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/content_settings/host_content_settings_map.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/extensions/management_policy.h"
#include "chrome/browser/google/google_url_tracker.h"
#include "chrome/browser/profile_resetter/brandcoded_default_settings.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_prepopulate_data.h"
#include "chrome/browser/search_engines/template_url_service.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_iterator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_source.h"

ProfileResetter::ProfileResetter(Profile* profile)
    : profile_(profile),
      template_url_service_(TemplateURLServiceFactory::GetForProfile(profile_)),
      pending_reset_flags_(0),
      cookies_remover_(NULL) {
  DCHECK(CalledOnValidThread());
  DCHECK(profile_);
  registrar_.Add(this, chrome::NOTIFICATION_TEMPLATE_URL_SERVICE_LOADED,
                 content::Source<TemplateURLService>(template_url_service_));
}

ProfileResetter::~ProfileResetter() {
  if (cookies_remover_)
    cookies_remover_->RemoveObserver(this);
}

void ProfileResetter::Reset(
    ProfileResetter::ResettableFlags resettable_flags,
    scoped_ptr<BrandcodedDefaultSettings> master_settings,
    const base::Closure& callback) {
  DCHECK(CalledOnValidThread());
  DCHECK(master_settings);

  master_settings_.swap(master_settings);

  // We should never be called with unknown flags.
  CHECK_EQ(static_cast<ResettableFlags>(0), resettable_flags & ~ALL);

  // We should never be called when a previous reset has not finished.
  CHECK_EQ(static_cast<ResettableFlags>(0), pending_reset_flags_);

  callback_ = callback;

  // These flags are set to false by the individual reset functions.
  pending_reset_flags_ = resettable_flags;

  struct {
    Resettable flag;
    void (ProfileResetter::*method)();
  } flag2Method [] = {
      { DEFAULT_SEARCH_ENGINE, &ProfileResetter::ResetDefaultSearchEngine },
      { HOMEPAGE, &ProfileResetter::ResetHomepage },
      { CONTENT_SETTINGS, &ProfileResetter::ResetContentSettings },
      { COOKIES_AND_SITE_DATA, &ProfileResetter::ResetCookiesAndSiteData },
      { EXTENSIONS, &ProfileResetter::ResetExtensions },
      { STARTUP_PAGES, &ProfileResetter::ResetStartupPages },
      { PINNED_TABS, &ProfileResetter::ResetPinnedTabs },
  };

  ResettableFlags reset_triggered_for_flags = 0;
  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(flag2Method); ++i) {
    if (resettable_flags & flag2Method[i].flag) {
      reset_triggered_for_flags |= flag2Method[i].flag;
      (this->*flag2Method[i].method)();
    }
  }

  DCHECK_EQ(resettable_flags, reset_triggered_for_flags);
}

bool ProfileResetter::IsActive() const {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  return pending_reset_flags_ != 0;
}

void ProfileResetter::MarkAsDone(Resettable resettable) {
  DCHECK(CalledOnValidThread());

  // Check that we are never called twice or unexpectedly.
  CHECK(pending_reset_flags_ & resettable);

  pending_reset_flags_ &= ~resettable;

  if (!pending_reset_flags_) {
    content::BrowserThread::PostTask(content::BrowserThread::UI, FROM_HERE,
                                     callback_);
    callback_.Reset();
    master_settings_.reset();
  }
}

void ProfileResetter::ResetDefaultSearchEngine() {
  DCHECK(CalledOnValidThread());
  DCHECK(template_url_service_);

  // If TemplateURLServiceFactory is ready we can clean it right now.
  // Otherwise, load it and continue from ProfileResetter::Observe.
  if (template_url_service_->loaded()) {
    PrefService* prefs = profile_->GetPrefs();
    DCHECK(prefs);
    TemplateURLPrepopulateData::ClearPrepopulatedEnginesInPrefs(profile_);
    scoped_ptr<ListValue> search_engines(
        master_settings_->GetSearchProviderOverrides());
    if (search_engines) {
      // This Chrome distribution channel provides a custom search engine. We
      // must reset to it.
      ListPrefUpdate update(prefs, prefs::kSearchProviderOverrides);
      update->Swap(search_engines.get());
    }

    template_url_service_->ResetURLs();

    // Reset Google search URL.
    prefs->ClearPref(prefs::kLastPromptedGoogleURL);
    const TemplateURL* default_search_provider =
        template_url_service_->GetDefaultSearchProvider();
    if (default_search_provider &&
        default_search_provider->url_ref().HasGoogleBaseURLs())
      GoogleURLTracker::RequestServerCheck(profile_, true);

    MarkAsDone(DEFAULT_SEARCH_ENGINE);
  } else {
    template_url_service_->Load();
  }
}

void ProfileResetter::ResetHomepage() {
  DCHECK(CalledOnValidThread());
  PrefService* prefs = profile_->GetPrefs();
  DCHECK(prefs);
  std::string homepage;
  bool homepage_is_ntp, show_home_button;

  if (master_settings_->GetHomepage(&homepage))
    prefs->SetString(prefs::kHomePage, homepage);
  else
    prefs->ClearPref(prefs::kHomePage);

  if (master_settings_->GetHomepageIsNewTab(&homepage_is_ntp))
    prefs->SetBoolean(prefs::kHomePageIsNewTabPage, homepage_is_ntp);
  else
    prefs->ClearPref(prefs::kHomePageIsNewTabPage);

  if (master_settings_->GetShowHomeButton(&show_home_button))
    prefs->SetBoolean(prefs::kShowHomeButton, show_home_button);
  else
    prefs->ClearPref(prefs::kShowHomeButton);
  MarkAsDone(HOMEPAGE);
}

void ProfileResetter::ResetContentSettings() {
  DCHECK(CalledOnValidThread());
  PrefService* prefs = profile_->GetPrefs();
  HostContentSettingsMap* map = profile_->GetHostContentSettingsMap();

  for (int type = 0; type < CONTENT_SETTINGS_NUM_TYPES; ++type) {
    map->ClearSettingsForOneType(static_cast<ContentSettingsType>(type));
    if (HostContentSettingsMap::IsSettingAllowedForType(
            prefs,
            CONTENT_SETTING_DEFAULT,
            static_cast<ContentSettingsType>(type)))
      map->SetDefaultContentSetting(static_cast<ContentSettingsType>(type),
                                    CONTENT_SETTING_DEFAULT);
  }
  MarkAsDone(CONTENT_SETTINGS);
}

void ProfileResetter::ResetCookiesAndSiteData() {
  DCHECK(CalledOnValidThread());
  DCHECK(!cookies_remover_);

  cookies_remover_ = BrowsingDataRemover::CreateForUnboundedRange(profile_);
  cookies_remover_->AddObserver(this);
  int remove_mask = BrowsingDataRemover::REMOVE_SITE_DATA |
                    BrowsingDataRemover::REMOVE_CACHE;
  PrefService* prefs = profile_->GetPrefs();
  DCHECK(prefs);
  // Don't try to clear LSO data if it's not supported.
  if (!prefs->GetBoolean(prefs::kClearPluginLSODataEnabled))
    remove_mask &= ~BrowsingDataRemover::REMOVE_PLUGIN_DATA;
  cookies_remover_->Remove(remove_mask, BrowsingDataHelper::UNPROTECTED_WEB);
}

void ProfileResetter::ResetExtensions() {
  DCHECK(CalledOnValidThread());

  std::vector<std::string> brandcode_extensions;
  master_settings_->GetExtensions(&brandcode_extensions);

  ExtensionService* extension_service = profile_->GetExtensionService();
  DCHECK(extension_service);
  extension_service->DisableUserExtensions(brandcode_extensions);

  MarkAsDone(EXTENSIONS);
}

void ProfileResetter::ResetStartupPages() {
  DCHECK(CalledOnValidThread());
  PrefService* prefs = profile_->GetPrefs();
  DCHECK(prefs);
  scoped_ptr<ListValue> url_list(master_settings_->GetUrlsToRestoreOnStartup());
  if (url_list)
    ListPrefUpdate(prefs, prefs::kURLsToRestoreOnStartup)->Swap(url_list.get());
  else
    prefs->ClearPref(prefs::kURLsToRestoreOnStartup);

  int restore_on_startup;
  if (master_settings_->GetRestoreOnStartup(&restore_on_startup))
    prefs->SetInteger(prefs::kRestoreOnStartup, restore_on_startup);
  else
    prefs->ClearPref(prefs::kRestoreOnStartup);

  prefs->SetBoolean(prefs::kRestoreOnStartupMigrated, true);
  MarkAsDone(STARTUP_PAGES);
}

void ProfileResetter::ResetPinnedTabs() {
  // Unpin all the tabs.
  for (chrome::BrowserIterator it; !it.done(); it.Next()) {
    if (it->is_type_tabbed() && it->profile() == profile_) {
      TabStripModel* tab_model = it->tab_strip_model();
      // Here we assume that indexof(any mini tab) < indexof(any normal tab).
      // If we unpin the tab, it can be moved to the right. Thus traversing in
      // reverse direction is correct.
      for (int i = tab_model->count() - 1; i >= 0; --i) {
        if (tab_model->IsTabPinned(i) && !tab_model->IsAppTab(i))
          tab_model->SetTabPinned(i, false);
      }
    }
  }
  MarkAsDone(PINNED_TABS);
}

void ProfileResetter::Observe(int type,
                              const content::NotificationSource& source,
                              const content::NotificationDetails& details) {
  DCHECK(CalledOnValidThread());
  // TemplateURLService has loaded. If we need to clean search engines, it's
  // time to go on.
  if (pending_reset_flags_ & DEFAULT_SEARCH_ENGINE)
    ResetDefaultSearchEngine();
}

void ProfileResetter::OnBrowsingDataRemoverDone() {
  cookies_remover_ = NULL;
  MarkAsDone(COOKIES_AND_SITE_DATA);
}
