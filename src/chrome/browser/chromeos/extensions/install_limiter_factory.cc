// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/extensions/install_limiter_factory.h"

#include "chrome/browser/chromeos/extensions/install_limiter.h"
#include "chrome/browser/extensions/extension_system_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/browser_context_keyed_service/browser_context_dependency_manager.h"

namespace extensions {

// static
InstallLimiter* InstallLimiterFactory::GetForProfile(Profile* profile) {
  return static_cast<InstallLimiter*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

// static
InstallLimiterFactory* InstallLimiterFactory::GetInstance() {
  return Singleton<InstallLimiterFactory>::get();
}

InstallLimiterFactory::InstallLimiterFactory()
    : BrowserContextKeyedServiceFactory(
        "InstallLimiter",
        BrowserContextDependencyManager::GetInstance()) {
  DependsOn(ExtensionSystemFactory::GetInstance());
}

InstallLimiterFactory::~InstallLimiterFactory() {
}

BrowserContextKeyedService* InstallLimiterFactory::BuildServiceInstanceFor(
    content::BrowserContext* profile) const {
  return new InstallLimiter();
}

}  // namespace extensions
