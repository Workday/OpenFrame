// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/ntp/app_resource_cache_factory.h"

#include "chrome/browser/profiles/incognito_helpers.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/webui/ntp/ntp_resource_cache.h"
#include "components/browser_context_keyed_service/browser_context_dependency_manager.h"

// static
NTPResourceCache* AppResourceCacheFactory::GetForProfile(Profile* profile) {
  return static_cast<NTPResourceCache*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

// static
AppResourceCacheFactory* AppResourceCacheFactory::GetInstance() {
  return Singleton<AppResourceCacheFactory>::get();
}

AppResourceCacheFactory::AppResourceCacheFactory()
    : BrowserContextKeyedServiceFactory(
        "AppResourceCache",
        BrowserContextDependencyManager::GetInstance()) {
#if defined(ENABLE_THEMES)
  DependsOn(ThemeServiceFactory::GetInstance());
#endif
}

AppResourceCacheFactory::~AppResourceCacheFactory() {}

BrowserContextKeyedService* AppResourceCacheFactory::BuildServiceInstanceFor(
    content::BrowserContext* profile) const {
  return new NTPResourceCache(static_cast<Profile*>(profile));
}

content::BrowserContext* AppResourceCacheFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  return chrome::GetBrowserContextRedirectedInIncognito(context);
}
