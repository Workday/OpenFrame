// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_AUTOFILL_PERSONAL_DATA_MANAGER_FACTORY_H_
#define CHROME_BROWSER_AUTOFILL_PERSONAL_DATA_MANAGER_FACTORY_H_

#include "base/compiler_specific.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service_factory.h"

template <typename T> struct DefaultSingletonTraits;
class Profile;

namespace autofill {

class PersonalDataManager;

// A wrapper of PersonalDataManager so we can use it as a profiled keyed
// service. This should only be subclassed in tests, e.g. to provide a mock
// PersonalDataManager.
class PersonalDataManagerService : public BrowserContextKeyedService {
 public:
  virtual PersonalDataManager* GetPersonalDataManager() = 0;
};

// Singleton that owns all PersonalDataManagers and associates them with
// Profiles.
// Listens for the Profile's destruction notification and cleans up the
// associated PersonalDataManager.
class PersonalDataManagerFactory : public BrowserContextKeyedServiceFactory {
 public:
  // Returns the PersonalDataManager for |profile|, creating it if it is not
  // yet created.
  static PersonalDataManager* GetForProfile(Profile* profile);

  static PersonalDataManagerFactory* GetInstance();

 private:
  friend struct DefaultSingletonTraits<PersonalDataManagerFactory>;

  PersonalDataManagerFactory();
  virtual ~PersonalDataManagerFactory();

  // BrowserContextKeyedServiceFactory:
  virtual BrowserContextKeyedService* BuildServiceInstanceFor(
      content::BrowserContext* profile) const OVERRIDE;
  virtual content::BrowserContext* GetBrowserContextToUse(
      content::BrowserContext* context) const OVERRIDE;
};

}  // namespace autofill

#endif  // CHROME_BROWSER_AUTOFILL_PERSONAL_DATA_MANAGER_FACTORY_H_
