// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/app_lifetime_monitor_factory.h"

#include "apps/app_lifetime_monitor.h"
#include "apps/shell_window_registry.h"
#include "chrome/browser/profiles/incognito_helpers.h"
#include "chrome/browser/profiles/profile.h"
#include "components/browser_context_keyed_service/browser_context_dependency_manager.h"

namespace apps {

// static
AppLifetimeMonitor* AppLifetimeMonitorFactory::GetForProfile(Profile* profile) {
  return static_cast<AppLifetimeMonitor*>(
      GetInstance()->GetServiceForBrowserContext(profile, false));
}

AppLifetimeMonitorFactory* AppLifetimeMonitorFactory::GetInstance() {
  return Singleton<AppLifetimeMonitorFactory>::get();
}

AppLifetimeMonitorFactory::AppLifetimeMonitorFactory()
    : BrowserContextKeyedServiceFactory(
        "AppLifetimeMonitor",
        BrowserContextDependencyManager::GetInstance()) {
  DependsOn(ShellWindowRegistry::Factory::GetInstance());
}

AppLifetimeMonitorFactory::~AppLifetimeMonitorFactory() {}

BrowserContextKeyedService* AppLifetimeMonitorFactory::BuildServiceInstanceFor(
    content::BrowserContext* profile) const {
  return new AppLifetimeMonitor(static_cast<Profile*>(profile));
}

bool AppLifetimeMonitorFactory::ServiceIsCreatedWithBrowserContext() const {
  return true;
}

content::BrowserContext* AppLifetimeMonitorFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  return chrome::GetBrowserContextRedirectedInIncognito(context);
}

}  // namespace apps
