// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_NTP_NTP_RESOURCE_CACHE_FACTORY_H_
#define CHROME_BROWSER_UI_WEBUI_NTP_NTP_RESOURCE_CACHE_FACTORY_H_

#include "base/memory/singleton.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service_factory.h"

class NTPResourceCache;
class Profile;

// Singleton that owns the NTPResourceCaches used by the NTP and associates them
// with Profiles. Listens for the Profile's destruction notification and cleans
// up the associated ThemeService.
class NTPResourceCacheFactory : public BrowserContextKeyedServiceFactory {
 public:
  static NTPResourceCache* GetForProfile(Profile* profile);

  static NTPResourceCacheFactory* GetInstance();

 private:
  friend struct DefaultSingletonTraits<NTPResourceCacheFactory>;

  NTPResourceCacheFactory();
  virtual ~NTPResourceCacheFactory();

  // BrowserContextKeyedServiceFactory:
  virtual BrowserContextKeyedService* BuildServiceInstanceFor(
      content::BrowserContext* profile) const OVERRIDE;
  virtual content::BrowserContext* GetBrowserContextToUse(
      content::BrowserContext* context) const OVERRIDE;
};

#endif  // CHROME_BROWSER_UI_WEBUI_NTP_NTP_RESOURCE_CACHE_FACTORY_H_
