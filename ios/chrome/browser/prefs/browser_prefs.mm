// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ios/chrome/browser/prefs/browser_prefs.h"

#include "base/prefs/pref_service.h"
#include "components/autofill/core/browser/autofill_manager.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_prefs.h"
#include "components/dom_distiller/core/distilled_page_prefs.h"
#include "components/enhanced_bookmarks/bookmark_server_cluster_service.h"
#include "components/flags_ui/pref_service_flags_storage.h"
#include "components/gcm_driver/gcm_channel_status_syncer.h"
#include "components/network_time/network_time_tracker.h"
#include "components/omnibox/browser/zero_suggest_provider.h"
#include "components/password_manager/core/browser/password_manager.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/proxy_config/pref_proxy_config_tracker_impl.h"
#include "components/rappor/rappor_service.h"
#include "components/search_engines/template_url_prepopulate_data.h"
#include "components/signin/core/common/signin_pref_names.h"
#include "components/sync_driver/sync_prefs.h"
#include "components/translate/core/browser/translate_prefs.h"
#include "components/translate/core/common/translate_pref_names.h"
#include "components/variations/service/variations_service.h"
#include "components/web_resource/promo_resource_service.h"
#include "ios/chrome/browser/application_context_impl.h"
#include "ios/chrome/browser/browser_state/browser_state_info_cache.h"
#include "ios/chrome/browser/first_run/first_run.h"
#import "ios/chrome/browser/geolocation/omnibox_geolocation_local_state.h"
#import "ios/chrome/browser/memory/memory_debugger_manager.h"
#import "ios/chrome/browser/metrics/ios_chrome_metrics_service_client.h"
#include "ios/chrome/browser/net/http_server_properties_manager_factory.h"
#include "ios/chrome/browser/pref_names.h"
#include "ios/chrome/browser/signin/signin_manager_factory.h"
#include "ios/chrome/grit/ios_locale_settings.h"
#include "ios/public/provider/chrome/browser/chrome_browser_provider.h"
#include "ui/base/l10n/l10n_util.h"

namespace {

// TODO(crbug.com/525079): those preferences are not used on iOS but are
// required to be able to run unit_tests until componentization of
// chrome/browser/prefs is complete.
const char kURLsToRestoreOnStartup[] = "session.startup_urls";
const char kURLsToRestoreOnStartupOld[] = "session.urls_to_restore_on_startup";

}  // namespace

void RegisterLocalStatePrefs(PrefRegistrySimple* registry) {
  BrowserStateInfoCache::RegisterPrefs(registry);
  flags_ui::PrefServiceFlagsStorage::RegisterPrefs(registry);
  gcm::GCMChannelStatusSyncer::RegisterPrefs(registry);
  ios::SigninManagerFactory::RegisterPrefs(registry);
  IOSChromeMetricsServiceClient::RegisterPrefs(registry);
  network_time::NetworkTimeTracker::RegisterPrefs(registry);
  PrefProxyConfigTrackerImpl::RegisterPrefs(registry);
  rappor::RapporService::RegisterPrefs(registry);
  variations::VariationsService::RegisterPrefs(registry);
  web_resource::PromoResourceService::RegisterPrefs(registry);

  // Preferences related to the browser state manager.
  registry->RegisterStringPref(ios::prefs::kBrowserStateLastUsed,
                               std::string());
  registry->RegisterIntegerPref(ios::prefs::kBrowserStatesNumCreated, 1);
  registry->RegisterListPref(ios::prefs::kBrowserStatesLastActive);

  [OmniboxGeolocationLocalState registerLocalState:registry];
  [MemoryDebuggerManager registerLocalState:registry];

  data_reduction_proxy::RegisterPrefs(registry);

  // TODO(shreyasv): Remove this in M49 as almost all users would have the
  // "do not backup" bit set by then. crbug.com/489865.
  registry->RegisterBooleanPref(prefs::kOTRStashStatePathSystemBackupExcluded,
                                false);
  registry->RegisterBooleanPref(prefs::kBrowsingDataMigrationHasBeenPossible,
                                false);

  ApplicationContextImpl::RegisterPrefs(registry);

  ios::GetChromeBrowserProvider()->RegisterLocalState(registry);
}

void RegisterBrowserStatePrefs(user_prefs::PrefRegistrySyncable* registry) {
  autofill::AutofillManager::RegisterProfilePrefs(registry);
  dom_distiller::DistilledPagePrefs::RegisterProfilePrefs(registry);
  enhanced_bookmarks::BookmarkServerClusterService::RegisterPrefs(registry);
  FirstRun::RegisterProfilePrefs(registry);
  gcm::GCMChannelStatusSyncer::RegisterProfilePrefs(registry);
  HostContentSettingsMap::RegisterProfilePrefs(registry);
  HttpServerPropertiesManagerFactory::RegisterProfilePrefs(registry);
  password_manager::PasswordManager::RegisterProfilePrefs(registry);
  PrefProxyConfigTrackerImpl::RegisterProfilePrefs(registry);
  sync_driver::SyncPrefs::RegisterProfilePrefs(registry);
  TemplateURLPrepopulateData::RegisterProfilePrefs(registry);
  translate::TranslatePrefs::RegisterProfilePrefs(registry);
  variations::VariationsService::RegisterProfilePrefs(registry);
  web_resource::PromoResourceService::RegisterProfilePrefs(registry);
  ZeroSuggestProvider::RegisterProfilePrefs(registry);

  registry->RegisterBooleanPref(
      ios::prefs::kEnableDoNotTrack, false,
      user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
  registry->RegisterBooleanPref(
      prefs::kEnableTranslate, true,
      user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
  registry->RegisterStringPref(
      ios::prefs::kAcceptLanguages,
      l10n_util::GetStringUTF8(IDS_IOS_ACCEPT_LANGUAGES));
  registry->RegisterStringPref(
      ios::prefs::kDefaultCharset,
      l10n_util::GetStringUTF8(IDS_IOS_DEFAULT_ENCODING),
      user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
  registry->RegisterInt64Pref(prefs::kRateThisAppDialogLastShownTime, 0,
                              user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
  registry->RegisterBooleanPref(
      prefs::kNetworkPredictionEnabled, true,
      user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
  registry->RegisterBooleanPref(
      prefs::kNetworkPredictionWifiOnly, true,
      user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
  registry->RegisterStringPref(prefs::kContextualSearchEnabled, std::string(),
                               user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
  registry->RegisterBooleanPref(
      ios::prefs::kSearchSuggestEnabled, true,
      user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
  registry->RegisterBooleanPref(prefs::kSigninAllowed, true);
  registry->RegisterBooleanPref(ios::prefs::kSavingBrowserHistoryDisabled,
                                false);
  registry->RegisterBooleanPref(ios::prefs::kAllowDeletingBrowserHistory, true);
  registry->RegisterIntegerPref(ios::prefs::kNtpShownPage, 1 << 10);
  // This comes from components/bookmarks/core/browser/bookmark_model.h
  // Defaults to 3, which is the id of bookmarkModel_->mobile_node()
  registry->RegisterInt64Pref(prefs::kNtpShownBookmarksFolder, 3);

  // TODO(crbug.com/525079): those preferences are not used on iOS but are
  // required to be able to run unit_tests until componentization of
  // chrome/browser/prefs is complete.
  registry->RegisterListPref(kURLsToRestoreOnStartup,
                             user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
  registry->RegisterListPref(kURLsToRestoreOnStartupOld);

  ios::GetChromeBrowserProvider()->RegisterProfilePrefs(registry);
}

// This method should be periodically pruned of year+ old migrations.
void MigrateObsoleteLocalStatePrefs(PrefService* prefs) {}

// This method should be periodically pruned of year+ old migrations.
void MigrateObsoleteBrowserStatePrefs(PrefService* prefs) {
  // Added 07/2014.
  translate::TranslatePrefs::MigrateUserPrefs(prefs,
                                              ios::prefs::kAcceptLanguages);

  // Added 08/2015.
  prefs->ClearPref(::prefs::kSigninSharedAuthenticationUserId);
}
