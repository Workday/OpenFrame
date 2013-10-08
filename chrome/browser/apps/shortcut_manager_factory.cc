// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/apps/shortcut_manager_factory.h"

#include "chrome/browser/apps/shortcut_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "components/browser_context_keyed_service/browser_context_dependency_manager.h"

// static
AppShortcutManager* AppShortcutManagerFactory::GetForProfile(Profile* profile) {
  return static_cast<AppShortcutManager*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

AppShortcutManagerFactory* AppShortcutManagerFactory::GetInstance() {
  return Singleton<AppShortcutManagerFactory>::get();
}

AppShortcutManagerFactory::AppShortcutManagerFactory()
    : BrowserContextKeyedServiceFactory(
        "AppShortcutManager",
        BrowserContextDependencyManager::GetInstance()) {
}

AppShortcutManagerFactory::~AppShortcutManagerFactory() {
}

BrowserContextKeyedService* AppShortcutManagerFactory::BuildServiceInstanceFor(
    content::BrowserContext* profile) const {
  return new AppShortcutManager(static_cast<Profile*>(profile));
}

bool AppShortcutManagerFactory::ServiceIsCreatedWithBrowserContext() const {
  return true;
}
