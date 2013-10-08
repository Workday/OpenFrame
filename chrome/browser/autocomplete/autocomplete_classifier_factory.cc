// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/autocomplete/autocomplete_classifier_factory.h"

#include "chrome/browser/autocomplete/autocomplete_classifier.h"
#include "chrome/browser/extensions/extension_system_factory.h"
#include "chrome/browser/history/shortcuts_backend_factory.h"
#include "chrome/browser/profiles/incognito_helpers.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "components/browser_context_keyed_service/browser_context_dependency_manager.h"


// static
AutocompleteClassifier* AutocompleteClassifierFactory::GetForProfile(
    Profile* profile) {
  return static_cast<AutocompleteClassifier*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

// static
AutocompleteClassifierFactory* AutocompleteClassifierFactory::GetInstance() {
  return Singleton<AutocompleteClassifierFactory>::get();
}

// static
BrowserContextKeyedService* AutocompleteClassifierFactory::BuildInstanceFor(
    content::BrowserContext* profile) {
  return new AutocompleteClassifier(static_cast<Profile*>(profile));
}

AutocompleteClassifierFactory::AutocompleteClassifierFactory()
    : BrowserContextKeyedServiceFactory(
        "AutocompleteClassifier",
        BrowserContextDependencyManager::GetInstance()) {
  DependsOn(extensions::ExtensionSystemFactory::GetInstance());
  DependsOn(TemplateURLServiceFactory::GetInstance());
  // TODO(pkasting): Uncomment these once they exist.
  //   DependsOn(PrefServiceFactory::GetInstance());
  DependsOn(ShortcutsBackendFactory::GetInstance());
}

AutocompleteClassifierFactory::~AutocompleteClassifierFactory() {
}

content::BrowserContext* AutocompleteClassifierFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  return chrome::GetBrowserContextRedirectedInIncognito(context);
}

bool AutocompleteClassifierFactory::ServiceIsNULLWhileTesting() const {
  return true;
}

BrowserContextKeyedService*
AutocompleteClassifierFactory::BuildServiceInstanceFor(
    content::BrowserContext* profile) const {
  return BuildInstanceFor(static_cast<Profile*>(profile));
}
