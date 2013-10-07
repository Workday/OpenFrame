// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/search_engines/template_url_fetcher_factory.h"

#include "chrome/browser/profiles/incognito_helpers.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_fetcher.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "components/browser_context_keyed_service/browser_context_dependency_manager.h"

// static
TemplateURLFetcher* TemplateURLFetcherFactory::GetForProfile(
    Profile* profile) {
  return static_cast<TemplateURLFetcher*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

// static
TemplateURLFetcherFactory* TemplateURLFetcherFactory::GetInstance() {
  return Singleton<TemplateURLFetcherFactory>::get();
}

// static
void TemplateURLFetcherFactory::ShutdownForProfile(Profile* profile) {
  TemplateURLFetcherFactory* factory = GetInstance();
  factory->BrowserContextShutdown(profile);
  factory->BrowserContextDestroyed(profile);
}

TemplateURLFetcherFactory::TemplateURLFetcherFactory()
    : BrowserContextKeyedServiceFactory(
        "TemplateURLFetcher",
        BrowserContextDependencyManager::GetInstance()) {
  DependsOn(TemplateURLServiceFactory::GetInstance());
}

TemplateURLFetcherFactory::~TemplateURLFetcherFactory() {
}

BrowserContextKeyedService* TemplateURLFetcherFactory::BuildServiceInstanceFor(
    content::BrowserContext* profile) const {
  return new TemplateURLFetcher(static_cast<Profile*>(profile));
}

content::BrowserContext* TemplateURLFetcherFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  return chrome::GetBrowserContextRedirectedInIncognito(context);
}
