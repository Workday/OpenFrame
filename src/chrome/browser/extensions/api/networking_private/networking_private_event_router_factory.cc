// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/networking_private/networking_private_event_router_factory.h"

#include "chrome/browser/extensions/api/networking_private/networking_private_event_router.h"
#include "chrome/browser/extensions/extension_system_factory.h"
#include "chrome/browser/profiles/incognito_helpers.h"
#include "chrome/browser/profiles/profile.h"
#include "components/browser_context_keyed_service/browser_context_dependency_manager.h"

namespace chromeos {

// static
NetworkingPrivateEventRouter*
NetworkingPrivateEventRouterFactory::GetForProfile(Profile* profile) {
  return static_cast<NetworkingPrivateEventRouter*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

// static
NetworkingPrivateEventRouterFactory*
NetworkingPrivateEventRouterFactory::GetInstance() {
  return Singleton<NetworkingPrivateEventRouterFactory>::get();
}

NetworkingPrivateEventRouterFactory::NetworkingPrivateEventRouterFactory()
    : BrowserContextKeyedServiceFactory(
          "NetworkingPrivateEventRouter",
          BrowserContextDependencyManager::GetInstance()) {
  DependsOn(extensions::ExtensionSystemFactory::GetInstance());
}

NetworkingPrivateEventRouterFactory::~NetworkingPrivateEventRouterFactory() {
}

BrowserContextKeyedService*
NetworkingPrivateEventRouterFactory::BuildServiceInstanceFor(
    content::BrowserContext* profile) const {
#if defined(OS_CHROMEOS)
  return new NetworkingPrivateEventRouter(static_cast<Profile*>(profile));
#else  // OS_CHROMEOS
  return NULL;
#endif  // OS_CHROMEOS
}

content::BrowserContext*
NetworkingPrivateEventRouterFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  return chrome::GetBrowserContextOwnInstanceInIncognito(context);
}

bool NetworkingPrivateEventRouterFactory::
ServiceIsCreatedWithBrowserContext() const {
  return true;
}

bool NetworkingPrivateEventRouterFactory::ServiceIsNULLWhileTesting() const {
  return true;
}

}  // namespace chromeos
