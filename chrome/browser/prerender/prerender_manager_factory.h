// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PRERENDER_PRERENDER_MANAGER_FACTORY_H_
#define CHROME_BROWSER_PRERENDER_PRERENDER_MANAGER_FACTORY_H_

#include "base/compiler_specific.h"
#include "base/memory/singleton.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service_factory.h"

class Profile;

namespace prerender {

class PrerenderManager;

// Singleton that owns all PrerenderManagers and associates them with Profiles.
// Listens for the Profile's destruction notification and cleans up the
// associated PrerenderManager.
class PrerenderManagerFactory : public BrowserContextKeyedServiceFactory {
 public:
  // Returns the PrerenderManager for |profile|.
  static PrerenderManager* GetForProfile(Profile* profile);

  static PrerenderManagerFactory* GetInstance();

 private:
  friend struct DefaultSingletonTraits<PrerenderManagerFactory>;

  PrerenderManagerFactory();
  virtual ~PrerenderManagerFactory();

  // BrowserContextKeyedServiceFactory:
  virtual BrowserContextKeyedService* BuildServiceInstanceFor(
      content::BrowserContext* profile) const OVERRIDE;
  virtual content::BrowserContext* GetBrowserContextToUse(
      content::BrowserContext* context) const OVERRIDE;
};

}  // namespace prerender

#endif  // CHROME_BROWSER_PRERENDER_PRERENDER_MANAGER_FACTORY_H_
