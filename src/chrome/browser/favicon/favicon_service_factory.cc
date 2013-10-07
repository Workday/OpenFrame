// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/favicon/favicon_service_factory.h"

#include "base/memory/singleton.h"
#include "base/prefs/pref_service.h"
#include "chrome/browser/favicon/favicon_service.h"
#include "chrome/browser/history/history_service.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/common/pref_names.h"
#include "components/browser_context_keyed_service/browser_context_dependency_manager.h"

// static
FaviconService* FaviconServiceFactory::GetForProfile(
    Profile* profile, Profile::ServiceAccessType sat) {
  if (!profile->IsOffTheRecord()) {
    return static_cast<FaviconService*>(
        GetInstance()->GetServiceForBrowserContext(profile, true));
  } else if (sat == Profile::EXPLICIT_ACCESS) {
    // Profile must be OffTheRecord in this case.
    return static_cast<FaviconService*>(
        GetInstance()->GetServiceForBrowserContext(
            profile->GetOriginalProfile(), true));
  }

  // Profile is OffTheRecord without access.
  NOTREACHED() << "This profile is OffTheRecord";
  return NULL;
}

// static
FaviconServiceFactory* FaviconServiceFactory::GetInstance() {
  return Singleton<FaviconServiceFactory>::get();
}

FaviconServiceFactory::FaviconServiceFactory()
    : BrowserContextKeyedServiceFactory(
        "FaviconService",
        BrowserContextDependencyManager::GetInstance()) {
  DependsOn(HistoryServiceFactory::GetInstance());
}

FaviconServiceFactory::~FaviconServiceFactory() {}

BrowserContextKeyedService* FaviconServiceFactory::BuildServiceInstanceFor(
    content::BrowserContext* profile) const {
  HistoryService* history_service = HistoryServiceFactory::GetForProfile(
      static_cast<Profile*>(profile), Profile::EXPLICIT_ACCESS);
  return new FaviconService(history_service);
}

bool FaviconServiceFactory::ServiceIsNULLWhileTesting() const {
  return true;
}
