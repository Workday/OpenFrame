// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/profiles/gaia_info_update_service_factory.h"

#include "chrome/browser/profiles/gaia_info_update_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "components/browser_context_keyed_service/browser_context_dependency_manager.h"
#include "components/user_prefs/pref_registry_syncable.h"

GAIAInfoUpdateServiceFactory::GAIAInfoUpdateServiceFactory()
    : BrowserContextKeyedServiceFactory(
        "GAIAInfoUpdateService",
        BrowserContextDependencyManager::GetInstance()) {
}

GAIAInfoUpdateServiceFactory::~GAIAInfoUpdateServiceFactory() {}

// static
GAIAInfoUpdateService* GAIAInfoUpdateServiceFactory::GetForProfile(
    Profile* profile) {
  return static_cast<GAIAInfoUpdateService*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

// static
GAIAInfoUpdateServiceFactory* GAIAInfoUpdateServiceFactory::GetInstance() {
  return Singleton<GAIAInfoUpdateServiceFactory>::get();
}

BrowserContextKeyedService*
GAIAInfoUpdateServiceFactory::BuildServiceInstanceFor(
    content::BrowserContext* context) const {
  Profile* profile = static_cast<Profile*>(context);
  if (!GAIAInfoUpdateService::ShouldUseGAIAProfileInfo(profile))
    return NULL;
  return new GAIAInfoUpdateService(profile);
}

void GAIAInfoUpdateServiceFactory::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* prefs) {
  prefs->RegisterInt64Pref(prefs::kProfileGAIAInfoUpdateTime,
                           0,
                           user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
  prefs->RegisterStringPref(prefs::kProfileGAIAInfoPictureURL,
                            std::string(),
                            user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
}

bool GAIAInfoUpdateServiceFactory::ServiceIsNULLWhileTesting() const {
  return true;
}
