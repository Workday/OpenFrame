// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/preference/chrome_direct_setting_api.h"

#include "base/bind.h"
#include "base/containers/hash_tables.h"
#include "base/lazy_instance.h"
#include "base/prefs/pref_change_registrar.h"
#include "base/prefs/pref_service.h"
#include "base/strings/stringprintf.h"
#include "chrome/browser/extensions/api/preference/preference_api_constants.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/profiles/profile.h"

namespace extensions {
namespace chromedirectsetting {

const char kOnPrefChangeFormat[] =
    "types.private.ChromeDirectSetting.%s.onChange";

class PreferenceWhitelist {
 public:
  PreferenceWhitelist() {
    whitelist_.insert("googlegeolocationaccess.enabled");
  }

  ~PreferenceWhitelist() {}

  bool IsPreferenceOnWhitelist(const std::string& pref_key){
    return whitelist_.find(pref_key) != whitelist_.end();
  }

  void RegisterEventListeners(
      Profile* profile,
      EventRouter::Observer* observer) {
    for (base::hash_set<std::string>::iterator iter = whitelist_.begin();
         iter != whitelist_.end();
         iter++) {
      std::string event_name = base::StringPrintf(
          kOnPrefChangeFormat,
          (*iter).c_str());
      ExtensionSystem::Get(profile)->event_router()->RegisterObserver(
          observer,
          event_name);
    }
  }

  void RegisterPropertyListeners(
      Profile* profile,
      PrefChangeRegistrar* registrar,
      const base::Callback<void(const std::string&)>& callback) {
    for (base::hash_set<std::string>::iterator iter = whitelist_.begin();
         iter != whitelist_.end();
         iter++) {
      const char* pref_key = (*iter).c_str();
      std::string event_name = base::StringPrintf(
          kOnPrefChangeFormat,
          pref_key);
      registrar->Add(pref_key, callback);
    }
  }

 private:
  base::hash_set<std::string> whitelist_;

  DISALLOW_COPY_AND_ASSIGN(PreferenceWhitelist);
};

base::LazyInstance<PreferenceWhitelist> preference_whitelist =
    LAZY_INSTANCE_INITIALIZER;

static base::LazyInstance<ProfileKeyedAPIFactory<ChromeDirectSettingAPI> >
    g_factory = LAZY_INSTANCE_INITIALIZER;

ChromeDirectSettingAPI::ChromeDirectSettingAPI(Profile* profile)
    : profile_(profile) {
  preference_whitelist.Get().RegisterEventListeners(profile, this);
}

ChromeDirectSettingAPI::~ChromeDirectSettingAPI() {}

// BrowserContextKeyedService implementation.
void ChromeDirectSettingAPI::Shutdown() {}

// ProfileKeyedAPI implementation.
ProfileKeyedAPIFactory<ChromeDirectSettingAPI>*
    ChromeDirectSettingAPI::GetFactoryInstance() {
  return &g_factory.Get();
}

// EventRouter::Observer implementation.
void ChromeDirectSettingAPI::OnListenerAdded(const EventListenerInfo& details) {
  ExtensionSystem::Get(profile_)->event_router()->UnregisterObserver(this);
  registrar_.Init(profile_->GetPrefs());
  preference_whitelist.Get().RegisterPropertyListeners(
      profile_,
      &registrar_,
      base::Bind(&ChromeDirectSettingAPI::OnPrefChanged,
                 base::Unretained(this),
                 registrar_.prefs()));
}

bool ChromeDirectSettingAPI::IsPreferenceOnWhitelist(
    const std::string& pref_key) {
  return preference_whitelist.Get().IsPreferenceOnWhitelist(pref_key);
}

ChromeDirectSettingAPI* ChromeDirectSettingAPI::Get(Profile* profile) {
  return
      ProfileKeyedAPIFactory<ChromeDirectSettingAPI>::GetForProfile(profile);
}

// ProfileKeyedAPI implementation.
const char* ChromeDirectSettingAPI::service_name() {
  return "ChromeDirectSettingAPI";
}

void ChromeDirectSettingAPI::OnPrefChanged(
    PrefService* pref_service, const std::string& pref_key) {
  std::string event_name = base::StringPrintf(kOnPrefChangeFormat,
                                              pref_key.c_str());
  EventRouter* router = ExtensionSystem::Get(profile_)->event_router();
  if (router && router->HasEventListener(event_name)) {
    const PrefService::Preference* preference =
        profile_->GetPrefs()->FindPreference(pref_key.c_str());
    const base::Value* value = preference->GetValue();

    scoped_ptr<DictionaryValue> result(new DictionaryValue);
    result->Set(preference_api_constants::kValue, value->DeepCopy());
    base::ListValue args;
    args.Append(result.release());

    ExtensionService* extension_service =
        ExtensionSystem::Get(profile_)->extension_service();
    const ExtensionSet* extensions = extension_service->extensions();
    for (ExtensionSet::const_iterator it = extensions->begin();
         it != extensions->end(); ++it) {
      if ((*it)->location() == Manifest::COMPONENT) {
        std::string extension_id = (*it)->id();
        if (router->ExtensionHasEventListener(extension_id, event_name)) {
          scoped_ptr<base::ListValue> args_copy(args.DeepCopy());
          scoped_ptr<Event> event(new Event(event_name, args_copy.Pass()));
          router->DispatchEventToExtension(extension_id, event.Pass());
        }
      }
    }
  }
}

}  // namespace chromedirectsetting
}  // namespace extensions

