// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/idle/idle_manager_factory.h"

#include "chrome/browser/extensions/api/idle/idle_manager.h"
#include "chrome/browser/extensions/extension_system_factory.h"
#include "chrome/browser/profiles/incognito_helpers.h"
#include "chrome/browser/profiles/profile.h"
#include "components/browser_context_keyed_service/browser_context_dependency_manager.h"

namespace extensions {

// static
IdleManager* IdleManagerFactory::GetForProfile(
    Profile* profile) {
  return static_cast<IdleManager*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

// static
IdleManagerFactory* IdleManagerFactory::GetInstance() {
  return Singleton<IdleManagerFactory>::get();
}

IdleManagerFactory::IdleManagerFactory()
    : BrowserContextKeyedServiceFactory(
        "IdleManager",
        BrowserContextDependencyManager::GetInstance()) {
  DependsOn(ExtensionSystemFactory::GetInstance());
}

IdleManagerFactory::~IdleManagerFactory() {
}

BrowserContextKeyedService* IdleManagerFactory::BuildServiceInstanceFor(
    content::BrowserContext* profile) const {
  IdleManager* idle_manager = new IdleManager(static_cast<Profile*>(profile));
  idle_manager->Init();
  return idle_manager;
}

content::BrowserContext* IdleManagerFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  return chrome::GetBrowserContextRedirectedInIncognito(context);
}

bool IdleManagerFactory::ServiceIsCreatedWithBrowserContext() const {
  return true;
}

bool IdleManagerFactory::ServiceIsNULLWhileTesting() const {
  return true;
}

}  // namespace extensions
