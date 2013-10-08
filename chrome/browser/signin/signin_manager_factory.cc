// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/signin/signin_manager_factory.h"

#include "base/prefs/pref_registry_simple.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/signin/chrome_signin_manager_delegate.h"
#include "chrome/browser/signin/signin_manager.h"
#include "chrome/browser/signin/token_service_factory.h"
#include "chrome/common/pref_names.h"
#include "components/browser_context_keyed_service/browser_context_dependency_manager.h"
#include "components/user_prefs/pref_registry_syncable.h"

SigninManagerFactory::SigninManagerFactory()
    : BrowserContextKeyedServiceFactory(
        "SigninManager",
        BrowserContextDependencyManager::GetInstance()) {
  DependsOn(TokenServiceFactory::GetInstance());
}

SigninManagerFactory::~SigninManagerFactory() {}

#if defined(OS_CHROMEOS)
// static
SigninManagerBase* SigninManagerFactory::GetForProfileIfExists(
    Profile* profile) {
  return static_cast<SigninManagerBase*>(
      GetInstance()->GetServiceForBrowserContext(profile, false));
}

// static
SigninManagerBase* SigninManagerFactory::GetForProfile(
    Profile* profile) {
  return static_cast<SigninManagerBase*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

#else
// static
SigninManager* SigninManagerFactory::GetForProfile(Profile* profile) {
  return static_cast<SigninManager*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

// static
SigninManager* SigninManagerFactory::GetForProfileIfExists(Profile* profile) {
  return static_cast<SigninManager*>(
      GetInstance()->GetServiceForBrowserContext(profile, false));
}
#endif

// static
SigninManagerFactory* SigninManagerFactory::GetInstance() {
  return Singleton<SigninManagerFactory>::get();
}

void SigninManagerFactory::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterStringPref(
      prefs::kGoogleServicesLastUsername,
      std::string(),
      user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
  registry->RegisterStringPref(
      prefs::kGoogleServicesUsername,
      std::string(),
      user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
  registry->RegisterBooleanPref(
      prefs::kAutologinEnabled,
      true,
      user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
  registry->RegisterBooleanPref(
      prefs::kReverseAutologinEnabled,
      true,
      user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
  registry->RegisterListPref(prefs::kReverseAutologinRejectedEmailList,
                             new ListValue,
                             user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
}

// static
void SigninManagerFactory::RegisterPrefs(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(prefs::kGoogleServicesUsernamePattern,
                               std::string());
}

BrowserContextKeyedService* SigninManagerFactory::BuildServiceInstanceFor(
    content::BrowserContext* context) const {
  SigninManagerBase* service = NULL;
  Profile* profile = static_cast<Profile*>(context);
#if defined(OS_CHROMEOS)
  service = new SigninManagerBase();
#else
  service = new SigninManager(
      scoped_ptr<SigninManagerDelegate>(
          new ChromeSigninManagerDelegate(profile)));
#endif
  service->Initialize(profile, g_browser_process->local_state());
  return service;
}
