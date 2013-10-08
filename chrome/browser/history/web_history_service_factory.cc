// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/history/web_history_service_factory.h"

#include "base/command_line.h"
#include "chrome/browser/content_settings/cookie_settings.h"
#include "chrome/browser/history/web_history_service.h"
#include "chrome/browser/signin/profile_oauth2_token_service_factory.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "chrome/browser/sync/profile_sync_service_factory.h"
#include "chrome/common/chrome_switches.h"
#include "components/browser_context_keyed_service/browser_context_dependency_manager.h"

namespace {
// Returns true if the user is signed in and full history sync is enabled,
// and false otherwise.
bool IsHistorySyncEnabled(Profile* profile) {
  if (!CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kHistoryDisableFullHistorySync)) {
    ProfileSyncService* sync =
        ProfileSyncServiceFactory::GetInstance()->GetForProfile(profile);
    return sync &&
        sync->sync_initialized() &&
        sync->GetActiveDataTypes().Has(syncer::HISTORY_DELETE_DIRECTIVES);
  }
  return false;
}

}  // namespace

// static
WebHistoryServiceFactory* WebHistoryServiceFactory::GetInstance() {
  return Singleton<WebHistoryServiceFactory>::get();
}

// static
history::WebHistoryService* WebHistoryServiceFactory::GetForProfile(
      Profile* profile) {
  if (IsHistorySyncEnabled(profile)) {
    return static_cast<history::WebHistoryService*>(
        GetInstance()->GetServiceForBrowserContext(profile, true));
  }
  return NULL;
}

BrowserContextKeyedService* WebHistoryServiceFactory::BuildServiceInstanceFor(
    content::BrowserContext* context) const {
  Profile* profile = static_cast<Profile*>(context);

  // Ensure that the service is not instantiated or used if the user is not
  // signed into sync, or if web history is not enabled.
  return IsHistorySyncEnabled(profile) ?
      new history::WebHistoryService(profile) : NULL;
}

WebHistoryServiceFactory::WebHistoryServiceFactory()
    : BrowserContextKeyedServiceFactory(
        "WebHistoryServiceFactory",
        BrowserContextDependencyManager::GetInstance()) {
  DependsOn(CookieSettings::Factory::GetInstance());
  DependsOn(ProfileOAuth2TokenServiceFactory::GetInstance());
}

WebHistoryServiceFactory::~WebHistoryServiceFactory() {
}
