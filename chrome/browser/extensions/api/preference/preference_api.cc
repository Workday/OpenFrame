// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/preference/preference_api.h"

#include <map>
#include <utility>

#include "base/lazy_instance.h"
#include "base/memory/singleton.h"
#include "base/prefs/pref_service.h"
#include "base/stl_util.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/api/preference/preference_api_constants.h"
#include "chrome/browser/extensions/api/preference/preference_helpers.h"
#include "chrome/browser/extensions/api/proxy/proxy_api.h"
#include "chrome/browser/extensions/extension_pref_value_map.h"
#include "chrome/browser/extensions/extension_pref_value_map_factory.h"
#include "chrome/browser/extensions/extension_prefs.h"
#include "chrome/browser/extensions/extension_prefs_factory.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/extensions/permissions/api_permission.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_source.h"
#include "extensions/browser/pref_names.h"
#include "extensions/common/error_utils.h"

namespace keys = extensions::preference_api_constants;
namespace helpers = extensions::preference_helpers;

using base::DictionaryValue;

namespace extensions {

namespace {

struct PrefMappingEntry {
  // Name of the preference referenced by the extension API JSON.
  const char* extension_pref;

  // Name of the preference in the PrefStores.
  const char* browser_pref;

  // Permission required to access this preference.
  // Use APIPermission::kInvalid for |permission| to express that no
  // permission is necessary.
  APIPermission::ID permission;
};

const char kOnPrefChangeFormat[] = "types.ChromeSetting.%s.onChange";
const char kConversionErrorMessage[] =
    "Internal error: Stored value for preference '*' cannot be converted "
    "properly.";

PrefMappingEntry kPrefMapping[] = {
  { "protectedContentEnabled",
    prefs::kEnableDRM,
    APIPermission::kPrivacy
  },
  { "alternateErrorPagesEnabled",
    prefs::kAlternateErrorPagesEnabled,
    APIPermission::kPrivacy
  },
  { "autofillEnabled",
    autofill::prefs::kAutofillEnabled,
    APIPermission::kPrivacy
  },
  { "hyperlinkAuditingEnabled",
    prefs::kEnableHyperlinkAuditing,
    APIPermission::kPrivacy
  },
  { "networkPredictionEnabled",
    prefs::kNetworkPredictionEnabled,
    APIPermission::kPrivacy
  },
  { "proxy",
    prefs::kProxy,
    APIPermission::kProxy
  },
  { "referrersEnabled",
    prefs::kEnableReferrers,
    APIPermission::kPrivacy
  },
  { "safeBrowsingEnabled",
    prefs::kSafeBrowsingEnabled,
    APIPermission::kPrivacy
  },
  { "searchSuggestEnabled",
    prefs::kSearchSuggestEnabled,
    APIPermission::kPrivacy
  },
  { "spellingServiceEnabled",
    prefs::kSpellCheckUseSpellingService,
    APIPermission::kPrivacy
  },
  { "thirdPartyCookiesAllowed",
    prefs::kBlockThirdPartyCookies,
    APIPermission::kPrivacy
  },
  { "translationServiceEnabled",
    prefs::kEnableTranslate,
    APIPermission::kPrivacy
  },
};

class IdentityPrefTransformer : public PrefTransformerInterface {
 public:
  virtual Value* ExtensionToBrowserPref(const Value* extension_pref,
                                        std::string* error,
                                        bool* bad_message) OVERRIDE {
    return extension_pref->DeepCopy();
  }

  virtual Value* BrowserToExtensionPref(const Value* browser_pref) OVERRIDE {
    return browser_pref->DeepCopy();
  }
};

class InvertBooleanTransformer : public PrefTransformerInterface {
 public:
  virtual Value* ExtensionToBrowserPref(const Value* extension_pref,
                                        std::string* error,
                                        bool* bad_message) OVERRIDE {
    return InvertBooleanValue(extension_pref);
  }

  virtual Value* BrowserToExtensionPref(const Value* browser_pref) OVERRIDE {
    return InvertBooleanValue(browser_pref);
  }

 private:
  static Value* InvertBooleanValue(const Value* value) {
    bool bool_value = false;
    bool result = value->GetAsBoolean(&bool_value);
    DCHECK(result);
    return Value::CreateBooleanValue(!bool_value);
  }
};

class PrefMapping {
 public:
  static PrefMapping* GetInstance() {
    return Singleton<PrefMapping>::get();
  }

  bool FindBrowserPrefForExtensionPref(const std::string& extension_pref,
                                       std::string* browser_pref,
                                       APIPermission::ID* permission) {
    PrefMap::iterator it = mapping_.find(extension_pref);
    if (it != mapping_.end()) {
      *browser_pref = it->second.first;
      *permission = it->second.second;
      return true;
    }
    return false;
  }

  bool FindEventForBrowserPref(const std::string& browser_pref,
                               std::string* event_name,
                               APIPermission::ID* permission) {
    PrefMap::iterator it = event_mapping_.find(browser_pref);
    if (it != event_mapping_.end()) {
      *event_name = it->second.first;
      *permission = it->second.second;
      return true;
    }
    return false;
  }

  PrefTransformerInterface* FindTransformerForBrowserPref(
      const std::string& browser_pref) {
    std::map<std::string, PrefTransformerInterface*>::iterator it =
        transformers_.find(browser_pref);
    if (it != transformers_.end())
      return it->second;
    else
      return identity_transformer_.get();
  }

 private:
  friend struct DefaultSingletonTraits<PrefMapping>;

  PrefMapping() {
    identity_transformer_.reset(new IdentityPrefTransformer());
    for (size_t i = 0; i < arraysize(kPrefMapping); ++i) {
      mapping_[kPrefMapping[i].extension_pref] =
          std::make_pair(kPrefMapping[i].browser_pref,
                         kPrefMapping[i].permission);
      std::string event_name =
          base::StringPrintf(kOnPrefChangeFormat,
                             kPrefMapping[i].extension_pref);
      event_mapping_[kPrefMapping[i].browser_pref] =
          std::make_pair(event_name, kPrefMapping[i].permission);
    }
    DCHECK_EQ(arraysize(kPrefMapping), mapping_.size());
    DCHECK_EQ(arraysize(kPrefMapping), event_mapping_.size());
    RegisterPrefTransformer(prefs::kProxy, new ProxyPrefTransformer());
    RegisterPrefTransformer(prefs::kBlockThirdPartyCookies,
                            new InvertBooleanTransformer());
  }

  ~PrefMapping() {
    STLDeleteContainerPairSecondPointers(transformers_.begin(),
                                         transformers_.end());
  }

  void RegisterPrefTransformer(const std::string& browser_pref,
                               PrefTransformerInterface* transformer) {
    DCHECK_EQ(0u, transformers_.count(browser_pref)) <<
        "Trying to register pref transformer for " << browser_pref << " twice";
    transformers_[browser_pref] = transformer;
  }

  typedef std::map<std::string,
                   std::pair<std::string, APIPermission::ID> >
          PrefMap;

  // Mapping from extension pref keys to browser pref keys and permissions.
  PrefMap mapping_;

  // Mapping from browser pref keys to extension event names and permissions.
  PrefMap event_mapping_;

  // Mapping from browser pref keys to transformers.
  std::map<std::string, PrefTransformerInterface*> transformers_;

  scoped_ptr<PrefTransformerInterface> identity_transformer_;

  DISALLOW_COPY_AND_ASSIGN(PrefMapping);
};

}  // namespace

PreferenceEventRouter::PreferenceEventRouter(Profile* profile)
    : profile_(profile) {
  registrar_.Init(profile_->GetPrefs());
  incognito_registrar_.Init(profile_->GetOffTheRecordPrefs());
  for (size_t i = 0; i < arraysize(kPrefMapping); ++i) {
    registrar_.Add(kPrefMapping[i].browser_pref,
                   base::Bind(&PreferenceEventRouter::OnPrefChanged,
                              base::Unretained(this),
                              registrar_.prefs()));
    incognito_registrar_.Add(kPrefMapping[i].browser_pref,
                             base::Bind(&PreferenceEventRouter::OnPrefChanged,
                                        base::Unretained(this),
                                        incognito_registrar_.prefs()));
  }
}

PreferenceEventRouter::~PreferenceEventRouter() { }

void PreferenceEventRouter::OnPrefChanged(PrefService* pref_service,
                                          const std::string& browser_pref) {
  bool incognito = (pref_service != profile_->GetPrefs());

  std::string event_name;
  APIPermission::ID permission = APIPermission::kInvalid;
  bool rv = PrefMapping::GetInstance()->FindEventForBrowserPref(
      browser_pref, &event_name, &permission);
  DCHECK(rv);

  base::ListValue args;
  DictionaryValue* dict = new DictionaryValue();
  args.Append(dict);
  const PrefService::Preference* pref =
      pref_service->FindPreference(browser_pref.c_str());
  CHECK(pref);
  PrefTransformerInterface* transformer =
      PrefMapping::GetInstance()->FindTransformerForBrowserPref(browser_pref);
  Value* transformed_value =
      transformer->BrowserToExtensionPref(pref->GetValue());
  if (!transformed_value) {
    LOG(ERROR) << ErrorUtils::FormatErrorMessage(kConversionErrorMessage,
                                                 pref->name());
    return;
  }

  dict->Set(keys::kValue, transformed_value);
  if (incognito) {
    ExtensionPrefs* ep = ExtensionPrefs::Get(profile_);
    dict->SetBoolean(keys::kIncognitoSpecific,
                     ep->HasIncognitoPrefValue(browser_pref));
  }

  helpers::DispatchEventToExtensions(profile_,
                                     event_name,
                                     &args,
                                     permission,
                                     incognito,
                                     browser_pref);
}

void PreferenceAPIBase::SetExtensionControlledPref(
    const std::string& extension_id,
    const std::string& pref_key,
    ExtensionPrefsScope scope,
    base::Value* value) {
#ifndef NDEBUG
  const PrefService::Preference* pref =
      extension_prefs()->pref_service()->FindPreference(pref_key.c_str());
  DCHECK(pref) << "Extension controlled preference key " << pref_key
               << " not registered.";
  DCHECK_EQ(pref->GetType(), value->GetType())
      << "Extension controlled preference " << pref_key << " has wrong type.";
#endif

  std::string scope_string;
  // ScopeToPrefName() returns false if the scope is not persisted.
  if (pref_names::ScopeToPrefName(scope, &scope_string)) {
    // Also store in persisted Preferences file to recover after a
    // browser restart.
    ExtensionPrefs::ScopedDictionaryUpdate update(extension_prefs(),
                                                  extension_id,
                                                  scope_string);
    DictionaryValue* preference = update.Get();
    if (!preference)
      preference = update.Create();
    preference->SetWithoutPathExpansion(pref_key, value->DeepCopy());
  }
  extension_pref_value_map()->SetExtensionPref(
      extension_id, pref_key, scope, value);
}

void PreferenceAPIBase::RemoveExtensionControlledPref(
    const std::string& extension_id,
    const std::string& pref_key,
    ExtensionPrefsScope scope) {
  DCHECK(extension_prefs()->pref_service()->FindPreference(pref_key.c_str()))
      << "Extension controlled preference key " << pref_key
      << " not registered.";

  std::string scope_string;
  if (pref_names::ScopeToPrefName(scope, &scope_string)) {
    ExtensionPrefs::ScopedDictionaryUpdate update(extension_prefs(),
                                                  extension_id,
                                                  scope_string);
    DictionaryValue* preference = update.Get();
    if (preference)
      preference->RemoveWithoutPathExpansion(pref_key, NULL);
  }
  extension_pref_value_map()->RemoveExtensionPref(
      extension_id, pref_key, scope);
}

bool PreferenceAPIBase::CanExtensionControlPref(
     const std::string& extension_id,
     const std::string& pref_key,
     bool incognito) {
  DCHECK(extension_prefs()->pref_service()->FindPreference(pref_key.c_str()))
      << "Extension controlled preference key " << pref_key
      << " not registered.";

  return extension_pref_value_map()->CanExtensionControlPref(
       extension_id, pref_key, incognito);
}

bool PreferenceAPIBase::DoesExtensionControlPref(
    const std::string& extension_id,
    const std::string& pref_key,
    bool* from_incognito) {
  DCHECK(extension_prefs()->pref_service()->FindPreference(pref_key.c_str()))
      << "Extension controlled preference key " << pref_key
      << " not registered.";

  return extension_pref_value_map()->DoesExtensionControlPref(
      extension_id, pref_key, from_incognito);
}

PreferenceAPI::PreferenceAPI(Profile* profile) : profile_(profile) {
  for (size_t i = 0; i < arraysize(kPrefMapping); ++i) {
    std::string event_name;
    APIPermission::ID permission = APIPermission::kInvalid;
    bool rv = PrefMapping::GetInstance()->FindEventForBrowserPref(
        kPrefMapping[i].browser_pref, &event_name, &permission);
    DCHECK(rv);
    ExtensionSystem::Get(profile_)->event_router()->RegisterObserver(
        this, event_name);
  }
  extension_prefs()->content_settings_store()->AddObserver(this);
}

PreferenceAPI::~PreferenceAPI() {
}

void PreferenceAPI::Shutdown() {
  ExtensionSystem::Get(profile_)->event_router()->UnregisterObserver(this);
  if (!extension_prefs()->extensions_disabled())
    ClearIncognitoSessionOnlyContentSettings();
  extension_prefs()->content_settings_store()->RemoveObserver(this);
}

static base::LazyInstance<ProfileKeyedAPIFactory<PreferenceAPI> >
g_factory = LAZY_INSTANCE_INITIALIZER;

// static
ProfileKeyedAPIFactory<PreferenceAPI>* PreferenceAPI::GetFactoryInstance() {
  return &g_factory.Get();
}

// static
PreferenceAPI* PreferenceAPI::Get(Profile* profile) {
  return ProfileKeyedAPIFactory<PreferenceAPI>::GetForProfile(profile);
}

void PreferenceAPI::OnListenerAdded(const EventListenerInfo& details) {
  preference_event_router_.reset(new PreferenceEventRouter(profile_));
  ExtensionSystem::Get(profile_)->event_router()->UnregisterObserver(this);
}

// static
void PreferenceAPI::LoadExtensionControlledPrefs(
    ExtensionPrefs* prefs,
    ExtensionPrefValueMap* value_map,
    const std::string& extension_id,
    ExtensionPrefsScope scope) {
  std::string scope_string;
  if (!pref_names::ScopeToPrefName(scope, &scope_string))
    return;
  std::string key = extension_id + "." + scope_string;

  const DictionaryValue* source_dict = prefs->pref_service()->
      GetDictionary(ExtensionPrefs::kExtensionsPref);
  const DictionaryValue* preferences = NULL;
  if (!source_dict->GetDictionary(key, &preferences))
    return;

  for (DictionaryValue::Iterator iter(*preferences);
       !iter.IsAtEnd(); iter.Advance()) {
    value_map->SetExtensionPref(
        extension_id, iter.key(), scope, iter.value().DeepCopy());
  }
}

// static
void PreferenceAPI::InitExtensionControlledPrefs(
    ExtensionPrefs* prefs,
    ExtensionPrefValueMap* value_map) {
  ExtensionIdList extension_ids;
  prefs->GetExtensions(&extension_ids);

  for (ExtensionIdList::iterator extension_id = extension_ids.begin();
       extension_id != extension_ids.end(); ++extension_id) {
    base::Time install_time = prefs->GetInstallTime(*extension_id);
    bool is_enabled = !prefs->IsExtensionDisabled(*extension_id);
    value_map->RegisterExtension(*extension_id, install_time, is_enabled);
    prefs->content_settings_store()->RegisterExtension(
        *extension_id, install_time, is_enabled);

    // Set regular extension controlled prefs.
    LoadExtensionControlledPrefs(prefs,
                                 value_map,
                                 *extension_id,
                                 kExtensionPrefsScopeRegular);
    // Set incognito extension controlled prefs.
    LoadExtensionControlledPrefs(prefs,
                                 value_map,
                                 *extension_id,
                                 kExtensionPrefsScopeIncognitoPersistent);
    // Set regular-only extension controlled prefs.
    LoadExtensionControlledPrefs(prefs,
                                 value_map,
                                 *extension_id,
                                 kExtensionPrefsScopeRegularOnly);

    // Set content settings.
    const base::ListValue* content_settings = NULL;
    if (prefs->ReadPrefAsList(*extension_id,
                              pref_names::kPrefContentSettings,
                              &content_settings)) {
      prefs->content_settings_store()->SetExtensionContentSettingFromList(
          *extension_id, content_settings, kExtensionPrefsScopeRegular);
    }
    if (prefs->ReadPrefAsList(*extension_id,
                              pref_names::kPrefIncognitoContentSettings,
                              &content_settings)) {
      prefs->content_settings_store()->SetExtensionContentSettingFromList(
          *extension_id,
          content_settings,
          kExtensionPrefsScopeIncognitoPersistent);
    }
  }
}

void PreferenceAPI::OnContentSettingChanged(const std::string& extension_id,
                                            bool incognito) {
  if (incognito) {
    extension_prefs()->UpdateExtensionPref(
        extension_id,
        pref_names::kPrefIncognitoContentSettings,
        extension_prefs()->content_settings_store()->GetSettingsForExtension(
            extension_id, kExtensionPrefsScopeIncognitoPersistent));
  } else {
    extension_prefs()->UpdateExtensionPref(
        extension_id,
        pref_names::kPrefContentSettings,
        extension_prefs()->content_settings_store()->GetSettingsForExtension(
            extension_id, kExtensionPrefsScopeRegular));
  }
}

void PreferenceAPI::ClearIncognitoSessionOnlyContentSettings() {
  ExtensionIdList extension_ids;
  extension_prefs()->GetExtensions(&extension_ids);
  for (ExtensionIdList::iterator extension_id = extension_ids.begin();
       extension_id != extension_ids.end(); ++extension_id) {
    extension_prefs()->content_settings_store()->
        ClearContentSettingsForExtension(
            *extension_id,
            kExtensionPrefsScopeIncognitoSessionOnly);
  }
}

ExtensionPrefs* PreferenceAPI::extension_prefs() {
  return ExtensionPrefs::Get(profile_);
}

ExtensionPrefValueMap* PreferenceAPI::extension_pref_value_map() {
  return ExtensionPrefValueMapFactory::GetForProfile(profile_);
}

template <>
void ProfileKeyedAPIFactory<PreferenceAPI>::DeclareFactoryDependencies() {
  DependsOn(ExtensionSystemFactory::GetInstance());
  DependsOn(ExtensionPrefsFactory::GetInstance());
  DependsOn(ExtensionPrefValueMapFactory::GetInstance());
}

PreferenceFunction::~PreferenceFunction() { }

bool PreferenceFunction::ValidateBrowserPref(
    const std::string& extension_pref_key,
    std::string* browser_pref_key) {
  APIPermission::ID permission = APIPermission::kInvalid;
  EXTENSION_FUNCTION_VALIDATE(
      PrefMapping::GetInstance()->FindBrowserPrefForExtensionPref(
          extension_pref_key, browser_pref_key, &permission));
  if (!GetExtension()->HasAPIPermission(permission)) {
    error_ = ErrorUtils::FormatErrorMessage(
        keys::kPermissionErrorMessage, extension_pref_key);
    return false;
  }
  return true;
}

GetPreferenceFunction::~GetPreferenceFunction() { }

bool GetPreferenceFunction::RunImpl() {
  std::string pref_key;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(0, &pref_key));
  DictionaryValue* details = NULL;
  EXTENSION_FUNCTION_VALIDATE(args_->GetDictionary(1, &details));

  bool incognito = false;
  if (details->HasKey(keys::kIncognitoKey))
    EXTENSION_FUNCTION_VALIDATE(details->GetBoolean(keys::kIncognitoKey,
                                                    &incognito));

  // Check incognito access.
  if (incognito && !include_incognito()) {
    error_ = keys::kIncognitoErrorMessage;
    return false;
  }

  // Obtain pref.
  std::string browser_pref;
  if (!ValidateBrowserPref(pref_key, &browser_pref))
    return false;
  PrefService* prefs = incognito ? profile_->GetOffTheRecordPrefs()
                                 : profile_->GetPrefs();
  const PrefService::Preference* pref =
      prefs->FindPreference(browser_pref.c_str());
  CHECK(pref);

  scoped_ptr<DictionaryValue> result(new DictionaryValue);

  // Retrieve level of control.
  std::string level_of_control =
      helpers::GetLevelOfControl(profile_, extension_id(), browser_pref,
                                 incognito);
  result->SetString(keys::kLevelOfControl, level_of_control);

  // Retrieve pref value.
  PrefTransformerInterface* transformer =
      PrefMapping::GetInstance()->FindTransformerForBrowserPref(browser_pref);
  Value* transformed_value =
      transformer->BrowserToExtensionPref(pref->GetValue());
  if (!transformed_value) {
    LOG(ERROR) <<
        ErrorUtils::FormatErrorMessage(kConversionErrorMessage,
                                                pref->name());
    return false;
  }
  result->Set(keys::kValue, transformed_value);

  // Retrieve incognito status.
  if (incognito) {
    ExtensionPrefs* ep = ExtensionPrefs::Get(profile_);
    result->SetBoolean(keys::kIncognitoSpecific,
                       ep->HasIncognitoPrefValue(browser_pref));
  }

  SetResult(result.release());
  return true;
}

SetPreferenceFunction::~SetPreferenceFunction() { }

bool SetPreferenceFunction::RunImpl() {
  std::string pref_key;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(0, &pref_key));
  DictionaryValue* details = NULL;
  EXTENSION_FUNCTION_VALIDATE(args_->GetDictionary(1, &details));

  Value* value = NULL;
  EXTENSION_FUNCTION_VALIDATE(details->Get(keys::kValue, &value));

  ExtensionPrefsScope scope = kExtensionPrefsScopeRegular;
  if (details->HasKey(keys::kScopeKey)) {
    std::string scope_str;
    EXTENSION_FUNCTION_VALIDATE(
        details->GetString(keys::kScopeKey, &scope_str));

    EXTENSION_FUNCTION_VALIDATE(helpers::StringToScope(scope_str, &scope));
  }

  // Check incognito scope.
  bool incognito =
      (scope == kExtensionPrefsScopeIncognitoPersistent ||
       scope == kExtensionPrefsScopeIncognitoSessionOnly);
  if (incognito) {
    // Regular profiles can't access incognito unless include_incognito is true.
    if (!profile()->IsOffTheRecord() && !include_incognito()) {
      error_ = keys::kIncognitoErrorMessage;
      return false;
    }
  } else {
    // Incognito profiles can't access regular mode ever, they only exist in
    // split mode.
    if (profile()->IsOffTheRecord()) {
      error_ = "Can't modify regular settings from an incognito context.";
      return false;
    }
  }

  if (scope == kExtensionPrefsScopeIncognitoSessionOnly &&
      !profile_->HasOffTheRecordProfile()) {
    error_ = keys::kIncognitoSessionOnlyErrorMessage;
    return false;
  }

  // Obtain pref.
  std::string browser_pref;
  if (!ValidateBrowserPref(pref_key, &browser_pref))
    return false;
  ExtensionPrefs* prefs = ExtensionPrefs::Get(profile_);
  const PrefService::Preference* pref =
      prefs->pref_service()->FindPreference(browser_pref.c_str());
  CHECK(pref);

  // Validate new value.
  EXTENSION_FUNCTION_VALIDATE(value->GetType() == pref->GetType());
  PrefTransformerInterface* transformer =
      PrefMapping::GetInstance()->FindTransformerForBrowserPref(browser_pref);
  std::string error;
  bool bad_message = false;
  scoped_ptr<Value> browser_pref_value(
      transformer->ExtensionToBrowserPref(value, &error, &bad_message));
  if (!browser_pref_value) {
    error_ = error;
    bad_message_ = bad_message;
    return false;
  }

  // Validate also that the stored value can be converted back by the
  // transformer.
  scoped_ptr<Value> extensionPrefValue(
      transformer->BrowserToExtensionPref(browser_pref_value.get()));
  if (!extensionPrefValue) {
    error_ =  ErrorUtils::FormatErrorMessage(kConversionErrorMessage,
                                                      pref->name());
    bad_message_ = true;
    return false;
  }

  PreferenceAPI::Get(profile())->SetExtensionControlledPref(
      extension_id(),
      browser_pref,
      scope,
      browser_pref_value.release());
  return true;
}

ClearPreferenceFunction::~ClearPreferenceFunction() { }

bool ClearPreferenceFunction::RunImpl() {
  std::string pref_key;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(0, &pref_key));
  DictionaryValue* details = NULL;
  EXTENSION_FUNCTION_VALIDATE(args_->GetDictionary(1, &details));

  ExtensionPrefsScope scope = kExtensionPrefsScopeRegular;
  if (details->HasKey(keys::kScopeKey)) {
    std::string scope_str;
    EXTENSION_FUNCTION_VALIDATE(
        details->GetString(keys::kScopeKey, &scope_str));

    EXTENSION_FUNCTION_VALIDATE(helpers::StringToScope(scope_str, &scope));
  }

  // Check incognito scope.
  bool incognito =
      (scope == kExtensionPrefsScopeIncognitoPersistent ||
       scope == kExtensionPrefsScopeIncognitoSessionOnly);
  if (incognito) {
    // We don't check incognito permissions here, as an extension should be
    // always allowed to clear its own settings.
  } else {
    // Incognito profiles can't access regular mode ever, they only exist in
    // split mode.
    if (profile()->IsOffTheRecord()) {
      error_ = "Can't modify regular settings from an incognito context.";
      return false;
    }
  }

  std::string browser_pref;
  if (!ValidateBrowserPref(pref_key, &browser_pref))
    return false;

  PreferenceAPI::Get(profile())->RemoveExtensionControlledPref(
      extension_id(), browser_pref, scope);
  return true;
}

}  // namespace extensions
