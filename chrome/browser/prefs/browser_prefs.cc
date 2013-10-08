// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/prefs/browser_prefs.h"

#include "apps/prefs.h"
#include "base/debug/trace_event.h"
#include "base/prefs/pref_registry_simple.h"
#include "base/prefs/pref_service.h"
#include "chrome/browser/about_flags.h"
#include "chrome/browser/accessibility/invert_bubble_prefs.h"
#include "chrome/browser/background/background_mode_manager.h"
#include "chrome/browser/bookmarks/bookmark_prompt_prefs.h"
#include "chrome/browser/bookmarks/bookmark_utils.h"
#include "chrome/browser/browser_process_impl.h"
#include "chrome/browser/browser_shutdown.h"
#include "chrome/browser/chrome_content_browser_client.h"
#include "chrome/browser/component_updater/recovery_component_installer.h"
#include "chrome/browser/content_settings/host_content_settings_map.h"
#include "chrome/browser/custom_handlers/protocol_handler_registry.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/extensions/api/commands/command_service.h"
#include "chrome/browser/extensions/api/tabs/tabs_api.h"
#include "chrome/browser/extensions/extension_prefs.h"
#include "chrome/browser/extensions/extension_web_ui.h"
#include "chrome/browser/external_protocol/external_protocol_handler.h"
#include "chrome/browser/first_run/first_run.h"
#include "chrome/browser/geolocation/geolocation_prefs.h"
#include "chrome/browser/google/google_url_tracker.h"
#include "chrome/browser/google/google_url_tracker_factory.h"
#include "chrome/browser/gpu/gl_string_manager.h"
#include "chrome/browser/gpu/gpu_mode_manager.h"
#include "chrome/browser/intranet_redirect_detector.h"
#include "chrome/browser/io_thread.h"
#include "chrome/browser/media/media_capture_devices_dispatcher.h"
#include "chrome/browser/media/media_stream_devices_controller.h"
#include "chrome/browser/metrics/metrics_log.h"
#include "chrome/browser/metrics/metrics_service.h"
#include "chrome/browser/metrics/variations/variations_service.h"
#include "chrome/browser/net/http_server_properties_manager.h"
#include "chrome/browser/net/net_pref_observer.h"
#include "chrome/browser/net/predictor.h"
#include "chrome/browser/net/pref_proxy_config_tracker_impl.h"
#include "chrome/browser/net/ssl_config_service_manager.h"
#include "chrome/browser/notifications/desktop_notification_service.h"
#include "chrome/browser/notifications/notification_prefs_manager.h"
#include "chrome/browser/password_manager/password_generation_manager.h"
#include "chrome/browser/password_manager/password_manager.h"
#include "chrome/browser/pepper_flash_settings_manager.h"
#include "chrome/browser/plugins/plugin_finder.h"
#include "chrome/browser/prefs/incognito_mode_prefs.h"
#include "chrome/browser/prefs/pref_service_syncable.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/printing/cloud_print/cloud_print_url.h"
#include "chrome/browser/printing/print_dialog_cloud.h"
#include "chrome/browser/profiles/chrome_version_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_impl.h"
#include "chrome/browser/profiles/profile_info_cache.h"
#include "chrome/browser/profiles/profiles_state.h"
#include "chrome/browser/renderer_host/pepper/device_id_fetcher.h"
#include "chrome/browser/renderer_host/web_cache_manager.h"
#include "chrome/browser/search/search.h"
#include "chrome/browser/search_engines/template_url_prepopulate_data.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "chrome/browser/signin/signin_promo.h"
#include "chrome/browser/sync/sync_prefs.h"
#include "chrome/browser/task_manager/task_manager.h"
#include "chrome/browser/translate/translate_prefs.h"
#include "chrome/browser/ui/alternate_error_tab_observer.h"
#include "chrome/browser/ui/app_list/app_list_service.h"
#include "chrome/browser/ui/browser_ui_prefs.h"
#include "chrome/browser/ui/network_profile_bubble.h"
#include "chrome/browser/ui/prefs/prefs_tab_helper.h"
#include "chrome/browser/ui/search_engines/keyword_editor_controller.h"
#include "chrome/browser/ui/startup/autolaunch_prompt.h"
#include "chrome/browser/ui/startup/default_browser_prompt.h"
#include "chrome/browser/ui/tabs/pinned_tab_codec.h"
#include "chrome/browser/ui/webui/extensions/extension_settings_handler.h"
#include "chrome/browser/ui/webui/flags_ui.h"
#include "chrome/browser/ui/webui/instant_ui.h"
#include "chrome/browser/ui/webui/ntp/new_tab_ui.h"
#include "chrome/browser/ui/webui/plugins_ui.h"
#include "chrome/browser/ui/webui/print_preview/sticky_settings.h"
#include "chrome/browser/ui/window_snapshot/window_snapshot.h"
#include "chrome/browser/upgrade_detector.h"
#include "chrome/browser/web_resource/promo_resource_service.h"
#include "chrome/common/metrics/caching_permuted_entropy_provider.h"
#include "chrome/common/pref_names.h"
#include "components/autofill/core/browser/autofill_manager.h"
#include "components/user_prefs/pref_registry_syncable.h"
#include "content/public/browser/render_process_host.h"

#if defined(ENABLE_AUTOFILL_DIALOG)
#include "chrome/browser/ui/autofill/autofill_dialog_controller.h"
#endif

#if defined(ENABLE_CONFIGURATION_POLICY)
#include "chrome/browser/policy/browser_policy_connector.h"
#include "chrome/browser/policy/policy_statistics_collector.h"
#include "chrome/browser/policy/url_blacklist_manager.h"
#endif

#if defined(ENABLE_MANAGED_USERS)
#include "chrome/browser/managed_mode/managed_user_service.h"
#include "chrome/browser/managed_mode/managed_user_sync_service.h"
#endif

#if defined(OS_MACOSX)
#include "chrome/browser/ui/cocoa/confirm_quit.h"
#include "chrome/browser/ui/cocoa/extensions/browser_actions_controller_prefs.h"
#endif

#if defined(TOOLKIT_VIEWS)
#include "chrome/browser/ui/browser_view_prefs.h"
#include "chrome/browser/ui/tabs/tab_strip_layout_type_prefs.h"
#endif

#if defined(TOOLKIT_GTK)
#include "chrome/browser/ui/gtk/browser_window_gtk.h"
#endif

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/app_mode/kiosk_app_manager.h"
#include "chrome/browser/chromeos/audio/audio_devices_pref_handler_impl.h"
#include "chrome/browser/chromeos/customization_document.h"
#include "chrome/browser/chromeos/display/display_preferences.h"
#include "chrome/browser/chromeos/login/default_pinned_apps_field_trial.h"
#include "chrome/browser/chromeos/login/login_utils.h"
#include "chrome/browser/chromeos/login/startup_utils.h"
#include "chrome/browser/chromeos/login/user_image_manager.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/login/wallpaper_manager.h"
#include "chrome/browser/chromeos/net/proxy_config_handler.h"
#include "chrome/browser/chromeos/policy/auto_enrollment_client.h"
#include "chrome/browser/chromeos/policy/device_cloud_policy_manager_chromeos.h"
#include "chrome/browser/chromeos/policy/device_status_collector.h"
#include "chrome/browser/chromeos/power/power_prefs.h"
#include "chrome/browser/chromeos/preferences.h"
#include "chrome/browser/chromeos/settings/device_oauth2_token_service.h"
#include "chrome/browser/chromeos/settings/device_settings_cache.h"
#include "chrome/browser/chromeos/status/data_promo_notification.h"
#include "chrome/browser/chromeos/system/automatic_reboot_manager.h"
#include "chrome/browser/extensions/api/enterprise_platform_keys_private/enterprise_platform_keys_private_api.h"
#include "chrome/browser/ui/webui/chromeos/login/signin_screen_handler.h"
#else
#include "chrome/browser/extensions/default_apps.h"
#endif

#if defined(USE_ASH)
#include "chrome/browser/ui/ash/chrome_launcher_prefs.h"
#endif

#if defined(OS_ANDROID)
#include "chrome/browser/ui/webui/ntp/android/promo_handler.h"
#else
#include "chrome/browser/ui/autofill/generated_credit_card_bubble_controller.h"
#endif

#if defined(ENABLE_PLUGIN_INSTALLATION)
#include "chrome/browser/plugins/plugins_resource_service.h"
#endif

namespace {

enum MigratedPreferences {
  NO_PREFS = 0,
  DNS_PREFS = 1 << 0,
  WINDOWS_PREFS = 1 << 1,
  GOOGLE_URL_TRACKER_PREFS = 1 << 2,
};

// A previous feature (see
// chrome/browser/protector/protected_prefs_watcher.cc in source
// control history) used this string as a prefix for various prefs it
// registered. We keep it here for now to clear out those old prefs in
// MigrateUserPrefs.
const char kBackupPref[] = "backup";

// Chrome To Mobile has been removed; this pref will be cleared from user data.
const char kChromeToMobilePref[] = "chrome_to_mobile.device_list";

#if !defined(OS_ANDROID)
// The sync promo error message preference has been removed; this pref will
// be cleared from user data.
const char kSyncPromoErrorMessage[] = "sync_promo.error_message";
#endif

}  // namespace

namespace chrome {

void RegisterLocalState(PrefRegistrySimple* registry) {
  // Prefs in Local State.
  registry->RegisterIntegerPref(prefs::kMultipleProfilePrefMigration, 0);

  // Please keep this list alphabetized.
  AppListService::RegisterPrefs(registry);
  apps::RegisterPrefs(registry);
  browser_shutdown::RegisterPrefs(registry);
  BrowserProcessImpl::RegisterPrefs(registry);
  RegisterScreenshotPrefs(registry);
  ExternalProtocolHandler::RegisterPrefs(registry);
  FlagsUI::RegisterPrefs(registry);
  geolocation::RegisterPrefs(registry);
  GLStringManager::RegisterPrefs(registry);
  GpuModeManager::RegisterPrefs(registry);
  IntranetRedirectDetector::RegisterPrefs(registry);
  IOThread::RegisterPrefs(registry);
  KeywordEditorController::RegisterPrefs(registry);
  MetricsLog::RegisterPrefs(registry);
  MetricsService::RegisterPrefs(registry);
  metrics::CachingPermutedEntropyProvider::RegisterPrefs(registry);
  PrefProxyConfigTrackerImpl::RegisterPrefs(registry);
  ProfileInfoCache::RegisterPrefs(registry);
  profiles::RegisterPrefs(registry);
  PromoResourceService::RegisterPrefs(registry);
  RegisterPrefsForRecoveryComponent(registry);
  SigninManagerFactory::RegisterPrefs(registry);
  SSLConfigServiceManager::RegisterPrefs(registry);
  UpgradeDetector::RegisterPrefs(registry);
  WebCacheManager::RegisterPrefs(registry);
  chrome_variations::VariationsService::RegisterPrefs(registry);

#if defined(ENABLE_PLUGINS)
  PluginFinder::RegisterPrefs(registry);
#endif

#if defined(ENABLE_PLUGIN_INSTALLATION)
  PluginsResourceService::RegisterPrefs(registry);
#endif

#if defined(ENABLE_CONFIGURATION_POLICY)
  policy::BrowserPolicyConnector::RegisterPrefs(registry);
  policy::PolicyStatisticsCollector::RegisterPrefs(registry);
#endif

#if defined(ENABLE_NOTIFICATIONS)
  NotificationPrefsManager::RegisterPrefs(registry);
#endif

#if defined(ENABLE_TASK_MANAGER)
  TaskManager::RegisterPrefs(registry);
#endif  // defined(ENABLE_TASK_MANAGER)

#if defined(TOOLKIT_VIEWS)
  RegisterBrowserViewPrefs(registry);
  RegisterTabStripLayoutTypePrefs(registry);
#endif

#if !defined(OS_ANDROID)
  BackgroundModeManager::RegisterPrefs(registry);
  RegisterBrowserPrefs(registry);
  RegisterDefaultBrowserPromptPrefs(registry);
#endif

#if defined(OS_CHROMEOS)
  chromeos::AudioDevicesPrefHandlerImpl::RegisterPrefs(registry);
  chromeos::DataPromoNotification::RegisterPrefs(registry);
  chromeos::DeviceOAuth2TokenService::RegisterPrefs(registry);
  chromeos::device_settings_cache::RegisterPrefs(registry);
  chromeos::default_pinned_apps_field_trial::RegisterPrefs(registry);
  chromeos::language_prefs::RegisterPrefs(registry);
  chromeos::KioskAppManager::RegisterPrefs(registry);
  chromeos::LoginUtils::RegisterPrefs(registry);
  chromeos::Preferences::RegisterPrefs(registry);
  chromeos::proxy_config::RegisterPrefs(registry);
  chromeos::RegisterDisplayLocalStatePrefs(registry);
  chromeos::ServicesCustomizationDocument::RegisterPrefs(registry);
  chromeos::SigninScreenHandler::RegisterPrefs(registry);
  chromeos::system::AutomaticRebootManager::RegisterPrefs(registry);
  chromeos::UserImageManager::RegisterPrefs(registry);
  chromeos::UserManager::RegisterPrefs(registry);
  chromeos::WallpaperManager::RegisterPrefs(registry);
  chromeos::StartupUtils::RegisterPrefs(registry);
  policy::AutoEnrollmentClient::RegisterPrefs(registry);
  policy::DeviceCloudPolicyManagerChromeOS::RegisterPrefs(registry);
  policy::DeviceStatusCollector::RegisterPrefs(registry);
#endif

#if defined(OS_MACOSX)
  confirm_quit::RegisterLocalState(registry);
#endif
}

// Register prefs applicable to all profiles.
void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  TRACE_EVENT0("browser", "chrome::RegisterUserPrefs");
  // User prefs. Please keep this list alphabetized.
  AlternateErrorPageTabObserver::RegisterProfilePrefs(registry);
  apps::RegisterProfilePrefs(registry);
  autofill::AutofillManager::RegisterProfilePrefs(registry);
#if !defined(OS_ANDROID)
  autofill::GeneratedCreditCardBubbleController::RegisterUserPrefs(registry);
#endif
  BookmarkPromptPrefs::RegisterProfilePrefs(registry);
  bookmark_utils::RegisterProfilePrefs(registry);
  browser_sync::SyncPrefs::RegisterProfilePrefs(registry);
  ChromeContentBrowserClient::RegisterProfilePrefs(registry);
  ChromeVersionService::RegisterProfilePrefs(registry);
  chrome_browser_net::HttpServerPropertiesManager::RegisterProfilePrefs(
      registry);
  chrome_browser_net::Predictor::RegisterProfilePrefs(registry);
  DownloadPrefs::RegisterProfilePrefs(registry);
  extensions::ExtensionPrefs::RegisterProfilePrefs(registry);
  ExtensionWebUI::RegisterProfilePrefs(registry);
  HostContentSettingsMap::RegisterProfilePrefs(registry);
  IncognitoModePrefs::RegisterProfilePrefs(registry);
  InstantUI::RegisterProfilePrefs(registry);
  MediaCaptureDevicesDispatcher::RegisterProfilePrefs(registry);
  MediaStreamDevicesController::RegisterProfilePrefs(registry);
  NetPrefObserver::RegisterProfilePrefs(registry);
  NewTabUI::RegisterProfilePrefs(registry);
  PasswordGenerationManager::RegisterProfilePrefs(registry);
  PasswordManager::RegisterProfilePrefs(registry);
  PrefProxyConfigTrackerImpl::RegisterProfilePrefs(registry);
  PrefsTabHelper::RegisterProfilePrefs(registry);
  Profile::RegisterProfilePrefs(registry);
  ProfileImpl::RegisterProfilePrefs(registry);
  PromoResourceService::RegisterProfilePrefs(registry);
  ProtocolHandlerRegistry::RegisterProfilePrefs(registry);
  RegisterBrowserUserPrefs(registry);
  SessionStartupPref::RegisterProfilePrefs(registry);
  TemplateURLPrepopulateData::RegisterProfilePrefs(registry);
  TranslatePrefs::RegisterProfilePrefs(registry);

#if defined(ENABLE_AUTOFILL_DIALOG)
  autofill::AutofillDialogController::RegisterProfilePrefs(registry);
#endif

#if defined(ENABLE_CONFIGURATION_POLICY)
  policy::URLBlacklistManager::RegisterProfilePrefs(registry);
#endif

#if defined(ENABLE_MANAGED_USERS)
  ManagedUserService::RegisterProfilePrefs(registry);
  ManagedUserSyncService::RegisterProfilePrefs(registry);
#endif

#if defined(ENABLE_NOTIFICATIONS)
  DesktopNotificationService::RegisterProfilePrefs(registry);
#endif

#if defined(TOOLKIT_VIEWS)
  RegisterInvertBubbleUserPrefs(registry);
#elif defined(TOOLKIT_GTK)
  BrowserWindowGtk::RegisterProfilePrefs(registry);
#endif

#if defined(ENABLE_FULL_PRINTING)
  print_dialog_cloud::RegisterProfilePrefs(registry);
  printing::StickySettings::RegisterProfilePrefs(registry);
  CloudPrintURL::RegisterProfilePrefs(registry);
#endif

#if defined(OS_ANDROID)
  PromoHandler::RegisterProfilePrefs(registry);
#endif

#if defined(USE_ASH)
  ash::RegisterChromeLauncherUserPrefs(registry);
#endif

#if !defined(OS_ANDROID)
  DeviceIDFetcher::RegisterProfilePrefs(registry);
  DevToolsWindow::RegisterProfilePrefs(registry);
  extensions::CommandService::RegisterProfilePrefs(registry);
  extensions::ExtensionSettingsHandler::RegisterProfilePrefs(registry);
  extensions::TabsCaptureVisibleTabFunction::RegisterProfilePrefs(registry);
  first_run::RegisterProfilePrefs(registry);
  PepperFlashSettingsManager::RegisterProfilePrefs(registry);
  PinnedTabCodec::RegisterProfilePrefs(registry);
  PluginsUI::RegisterProfilePrefs(registry);
  RegisterAutolaunchUserPrefs(registry);
  signin::RegisterProfilePrefs(registry);
#endif

#if !defined(OS_ANDROID) && !defined(OS_CHROMEOS)
  default_apps::RegisterProfilePrefs(registry);
#endif

#if defined(OS_CHROMEOS)
  chromeos::Preferences::RegisterProfilePrefs(registry);
  chromeos::proxy_config::RegisterProfilePrefs(registry);
  extensions::EnterprisePlatformKeysPrivateChallengeUserKeyFunction::
      RegisterProfilePrefs(registry);
  FlagsUI::RegisterProfilePrefs(registry);
#endif

#if defined(OS_WIN)
  NetworkProfileBubble::RegisterProfilePrefs(registry);
#endif

#if defined(OS_MACOSX)
  RegisterBrowserActionsControllerProfilePrefs(registry);
#endif

  // Prefs registered only for migration (clearing or moving to a new
  // key) go here.
  registry->RegisterDictionaryPref(
      kBackupPref,
      new DictionaryValue(),
      user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
  registry->RegisterListPref(
      kChromeToMobilePref,
      user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
#if !defined(OS_ANDROID)
  registry->RegisterStringPref(
      kSyncPromoErrorMessage,
      std::string(),
      user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
#endif
}

void RegisterUserProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  RegisterProfilePrefs(registry);

#if defined(OS_CHROMEOS)
  chromeos::PowerPrefs::RegisterUserProfilePrefs(registry);
#endif
}

#if defined(OS_CHROMEOS)
void RegisterLoginProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  RegisterProfilePrefs(registry);

  chromeos::PowerPrefs::RegisterLoginProfilePrefs(registry);
}
#endif

void MigrateUserPrefs(Profile* profile) {
  PrefService* prefs = profile->GetPrefs();

  // Cleanup prefs from now-removed protector feature.
  prefs->ClearPref(kBackupPref);

  // Cleanup prefs from now-removed Chrome To Mobile feature.
  prefs->ClearPref(kChromeToMobilePref);

#if !defined(OS_ANDROID)
  // Cleanup now-removed sync promo error message preference.
  // TODO(fdoray): Remove this when it's safe to do so (crbug.com/268442).
  prefs->ClearPref(kSyncPromoErrorMessage);
#endif

  PrefsTabHelper::MigrateUserPrefs(prefs);
  PromoResourceService::MigrateUserPrefs(prefs);
  TranslatePrefs::MigrateUserPrefs(prefs);
}

void MigrateBrowserPrefs(Profile* profile, PrefService* local_state) {
  // Copy pref values which have been migrated to user_prefs from local_state,
  // or remove them from local_state outright, if copying is not required.
  int current_version =
      local_state->GetInteger(prefs::kMultipleProfilePrefMigration);
  PrefRegistrySimple* registry = static_cast<PrefRegistrySimple*>(
      local_state->DeprecatedGetPrefRegistry());

  if (!(current_version & DNS_PREFS)) {
    registry->RegisterListPref(prefs::kDnsStartupPrefetchList);
    local_state->ClearPref(prefs::kDnsStartupPrefetchList);

    registry->RegisterListPref(prefs::kDnsHostReferralList);
    local_state->ClearPref(prefs::kDnsHostReferralList);

    current_version |= DNS_PREFS;
    local_state->SetInteger(prefs::kMultipleProfilePrefMigration,
                            current_version);
  }

  PrefService* user_prefs = profile->GetPrefs();
  if (!(current_version & WINDOWS_PREFS)) {
    registry->RegisterIntegerPref(prefs::kDevToolsHSplitLocation, -1);
    if (local_state->HasPrefPath(prefs::kDevToolsHSplitLocation)) {
      user_prefs->SetInteger(
          prefs::kDevToolsHSplitLocation,
          local_state->GetInteger(prefs::kDevToolsHSplitLocation));
    }
    local_state->ClearPref(prefs::kDevToolsHSplitLocation);

    registry->RegisterDictionaryPref(prefs::kBrowserWindowPlacement);
    if (local_state->HasPrefPath(prefs::kBrowserWindowPlacement)) {
      const PrefService::Preference* pref =
          local_state->FindPreference(prefs::kBrowserWindowPlacement);
      DCHECK(pref);
      user_prefs->Set(prefs::kBrowserWindowPlacement,
                      *(pref->GetValue()));
    }
    local_state->ClearPref(prefs::kBrowserWindowPlacement);

    current_version |= WINDOWS_PREFS;
    local_state->SetInteger(prefs::kMultipleProfilePrefMigration,
                            current_version);
  }

  if (!(current_version & GOOGLE_URL_TRACKER_PREFS)) {
    GoogleURLTrackerFactory::GetInstance()->RegisterUserPrefsOnBrowserContext(
        profile);
    registry->RegisterStringPref(prefs::kLastKnownGoogleURL,
                                 GoogleURLTracker::kDefaultGoogleHomepage);
    if (local_state->HasPrefPath(prefs::kLastKnownGoogleURL)) {
      user_prefs->SetString(prefs::kLastKnownGoogleURL,
                            local_state->GetString(prefs::kLastKnownGoogleURL));
    }
    local_state->ClearPref(prefs::kLastKnownGoogleURL);

    registry->RegisterStringPref(prefs::kLastPromptedGoogleURL,
                                 std::string());
    if (local_state->HasPrefPath(prefs::kLastPromptedGoogleURL)) {
      user_prefs->SetString(
          prefs::kLastPromptedGoogleURL,
          local_state->GetString(prefs::kLastPromptedGoogleURL));
    }
    local_state->ClearPref(prefs::kLastPromptedGoogleURL);

    current_version |= GOOGLE_URL_TRACKER_PREFS;
    local_state->SetInteger(prefs::kMultipleProfilePrefMigration,
                            current_version);
  }
}

}  // namespace chrome
