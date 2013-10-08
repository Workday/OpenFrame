// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_system_factory.h"

#include "chrome/browser/extensions/extension_prefs_factory.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/policy/profile_policy_connector_factory.h"
#include "chrome/browser/profiles/incognito_helpers.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/global_error/global_error_service_factory.h"
#include "components/browser_context_keyed_service/browser_context_dependency_manager.h"

namespace extensions {

// ExtensionSystemSharedFactory

// static
ExtensionSystemImpl::Shared*
ExtensionSystemSharedFactory::GetForProfile(Profile* profile) {
  return static_cast<ExtensionSystemImpl::Shared*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

// static
ExtensionSystemSharedFactory* ExtensionSystemSharedFactory::GetInstance() {
  return Singleton<ExtensionSystemSharedFactory>::get();
}

ExtensionSystemSharedFactory::ExtensionSystemSharedFactory()
    : BrowserContextKeyedServiceFactory(
        "ExtensionSystemShared",
        BrowserContextDependencyManager::GetInstance()) {
  DependsOn(ExtensionPrefsFactory::GetInstance());
  DependsOn(GlobalErrorServiceFactory::GetInstance());
#if defined(ENABLE_THEMES)
  DependsOn(ThemeServiceFactory::GetInstance());
#endif
  DependsOn(policy::ProfilePolicyConnectorFactory::GetInstance());
}

ExtensionSystemSharedFactory::~ExtensionSystemSharedFactory() {
}

BrowserContextKeyedService*
ExtensionSystemSharedFactory::BuildServiceInstanceFor(
    content::BrowserContext* profile) const {
  return new ExtensionSystemImpl::Shared(static_cast<Profile*>(profile));
}

content::BrowserContext* ExtensionSystemSharedFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  return chrome::GetBrowserContextRedirectedInIncognito(context);
}

// ExtensionSystemFactory

// static
ExtensionSystem* ExtensionSystemFactory::GetForProfile(Profile* profile) {
  return static_cast<ExtensionSystem*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

// static
ExtensionSystemFactory* ExtensionSystemFactory::GetInstance() {
  return Singleton<ExtensionSystemFactory>::get();
}

ExtensionSystemFactory::ExtensionSystemFactory()
    : BrowserContextKeyedServiceFactory(
        "ExtensionSystem",
        BrowserContextDependencyManager::GetInstance()) {
  DependsOn(ExtensionSystemSharedFactory::GetInstance());
}

ExtensionSystemFactory::~ExtensionSystemFactory() {
}

BrowserContextKeyedService* ExtensionSystemFactory::BuildServiceInstanceFor(
    content::BrowserContext* profile) const {
  return new ExtensionSystemImpl(static_cast<Profile*>(profile));
}

content::BrowserContext* ExtensionSystemFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  return chrome::GetBrowserContextOwnInstanceInIncognito(context);
}

bool ExtensionSystemFactory::ServiceIsCreatedWithBrowserContext() const {
  return true;
}

}  // namespace extensions
