// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/history/history_service_factory.h"

#include "base/prefs/pref_service.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/history/history_service.h"
#include "chrome/browser/profiles/incognito_helpers.h"
#include "chrome/common/pref_names.h"
#include "components/browser_context_keyed_service/browser_context_dependency_manager.h"

// static
HistoryService* HistoryServiceFactory::GetForProfile(
    Profile* profile, Profile::ServiceAccessType sat) {
  // If saving history is disabled, only allow explicit access.
  if (profile->GetPrefs()->GetBoolean(prefs::kSavingBrowserHistoryDisabled) &&
      sat != Profile::EXPLICIT_ACCESS)
    return NULL;

  return static_cast<HistoryService*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

// static
HistoryService*
HistoryServiceFactory::GetForProfileIfExists(
    Profile* profile, Profile::ServiceAccessType sat) {
  // If saving history is disabled, only allow explicit access.
  if (profile->GetPrefs()->GetBoolean(prefs::kSavingBrowserHistoryDisabled) &&
      sat != Profile::EXPLICIT_ACCESS)
    return NULL;

  return static_cast<HistoryService*>(
      GetInstance()->GetServiceForBrowserContext(profile, false));
}

// static
HistoryService*
HistoryServiceFactory::GetForProfileWithoutCreating(Profile* profile) {
  return static_cast<HistoryService*>(
      GetInstance()->GetServiceForBrowserContext(profile, false));
}

// static
HistoryServiceFactory* HistoryServiceFactory::GetInstance() {
  return Singleton<HistoryServiceFactory>::get();
}

// static
void HistoryServiceFactory::ShutdownForProfile(Profile* profile) {
  HistoryServiceFactory* factory = GetInstance();
  factory->BrowserContextDestroyed(profile);
}

HistoryServiceFactory::HistoryServiceFactory()
    : BrowserContextKeyedServiceFactory(
          "HistoryService", BrowserContextDependencyManager::GetInstance()) {
  DependsOn(BookmarkModelFactory::GetInstance());
}

HistoryServiceFactory::~HistoryServiceFactory() {
}

BrowserContextKeyedService*
HistoryServiceFactory::BuildServiceInstanceFor(
    content::BrowserContext* context) const {
  Profile* profile = static_cast<Profile*>(context);
  HistoryService* history_service = new HistoryService(profile);
  if (!history_service->Init(profile->GetPath(),
                             BookmarkModelFactory::GetForProfile(profile))) {
    return NULL;
  }
  return history_service;
}

content::BrowserContext* HistoryServiceFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  return chrome::GetBrowserContextRedirectedInIncognito(context);
}

bool HistoryServiceFactory::ServiceIsNULLWhileTesting() const {
  return true;
}
