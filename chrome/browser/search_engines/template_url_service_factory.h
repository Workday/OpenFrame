// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SEARCH_ENGINES_TEMPLATE_URL_SERVICE_FACTORY_H_
#define CHROME_BROWSER_SEARCH_ENGINES_TEMPLATE_URL_SERVICE_FACTORY_H_

#include "base/memory/singleton.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service_factory.h"

class Profile;
class TemplateURLService;

// Singleton that owns all TemplateURLService and associates them with
// Profiles.
class TemplateURLServiceFactory : public BrowserContextKeyedServiceFactory {
 public:
  static TemplateURLService* GetForProfile(Profile* profile);

  static TemplateURLServiceFactory* GetInstance();

  static BrowserContextKeyedService* BuildInstanceFor(
      content::BrowserContext* profile);

 private:
  friend struct DefaultSingletonTraits<TemplateURLServiceFactory>;

  TemplateURLServiceFactory();
  virtual ~TemplateURLServiceFactory();

  // BrowserContextKeyedServiceFactory:
  virtual BrowserContextKeyedService* BuildServiceInstanceFor(
      content::BrowserContext* profile) const OVERRIDE;
  virtual void RegisterProfilePrefs(
      user_prefs::PrefRegistrySyncable* registry) OVERRIDE;
  virtual content::BrowserContext* GetBrowserContextToUse(
      content::BrowserContext* context) const OVERRIDE;
  virtual bool ServiceIsNULLWhileTesting() const OVERRIDE;
  virtual void BrowserContextShutdown(
      content::BrowserContext* profile) OVERRIDE;
  virtual void BrowserContextDestroyed(
      content::BrowserContext* profile) OVERRIDE;
};

#endif  // CHROME_BROWSER_SEARCH_ENGINES_TEMPLATE_URL_SERVICE_FACTORY_H_
