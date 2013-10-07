// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SPELLCHECKER_SPELLCHECK_FACTORY_H_
#define CHROME_BROWSER_SPELLCHECKER_SPELLCHECK_FACTORY_H_

#include "base/basictypes.h"
#include "base/memory/singleton.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service_factory.h"

class SpellcheckService;
class Profile;

// Entry into the SpellCheck system.
//
// Internally, this owns all SpellcheckService objects.
class SpellcheckServiceFactory : public BrowserContextKeyedServiceFactory {
 public:
  // Returns the spell check host. This will create the SpellcheckService if it
  // does not already exist.
  static SpellcheckService* GetForProfile(Profile* profile);

  static SpellcheckService* GetForRenderProcessId(int render_process_id);

  // Returns the spell check host. This can return NULL.
  static SpellcheckService* GetForProfileWithoutCreating(Profile* profile);

  static SpellcheckServiceFactory* GetInstance();

 private:
  friend struct DefaultSingletonTraits<SpellcheckServiceFactory>;

  SpellcheckServiceFactory();
  virtual ~SpellcheckServiceFactory();

  // BrowserContextKeyedServiceFactory:
  virtual BrowserContextKeyedService* BuildServiceInstanceFor(
      content::BrowserContext* context) const OVERRIDE;
  virtual void RegisterProfilePrefs(
      user_prefs::PrefRegistrySyncable* registry) OVERRIDE;
  virtual content::BrowserContext* GetBrowserContextToUse(
      content::BrowserContext* context) const OVERRIDE;
  virtual bool ServiceIsNULLWhileTesting() const OVERRIDE;

  DISALLOW_COPY_AND_ASSIGN(SpellcheckServiceFactory);
};

#endif  // CHROME_BROWSER_SPELLCHECKER_SPELLCHECK_FACTORY_H_
