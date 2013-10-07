// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/policy_loader_mac.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/callback.h"
#include "base/file_util.h"
#include "base/mac/foundation_util.h"
#include "base/mac/scoped_cftyperef.h"
#include "base/path_service.h"
#include "base/platform_file.h"
#include "base/strings/sys_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/policy/external_data_fetcher.h"
#include "chrome/browser/policy/policy_bundle.h"
#include "chrome/browser/policy/policy_domain_descriptor.h"
#include "chrome/browser/policy/policy_load_status.h"
#include "chrome/browser/policy/policy_map.h"
#include "chrome/browser/policy/preferences_mac.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/policy/policy_schema.h"
#include "policy/policy_constants.h"

using base::mac::CFCast;
using base::ScopedCFTypeRef;

namespace policy {

namespace {

base::FilePath GetManagedPolicyPath() {
  // This constructs the path to the plist file in which Mac OS X stores the
  // managed preference for the application. This is undocumented and therefore
  // fragile, but if it doesn't work out, AsyncPolicyLoader has a task that
  // polls periodically in order to reload managed preferences later even if we
  // missed the change.
  base::FilePath path;
  if (!PathService::Get(chrome::DIR_MANAGED_PREFS, &path))
    return base::FilePath();

  CFBundleRef bundle(CFBundleGetMainBundle());
  if (!bundle)
    return base::FilePath();

  CFStringRef bundle_id = CFBundleGetIdentifier(bundle);
  if (!bundle_id)
    return base::FilePath();

  return path.Append(base::SysCFStringRefToUTF8(bundle_id) + ".plist");
}

// Callback function for CFDictionaryApplyFunction. |key| and |value| are an
// entry of the CFDictionary that should be converted into an equivalent entry
// in the DictionaryValue in |context|.
void DictionaryEntryToValue(const void* key, const void* value, void* context) {
  if (CFStringRef cf_key = CFCast<CFStringRef>(key)) {
    base::Value* converted =
        PolicyLoaderMac::CreateValueFromProperty(
            static_cast<CFPropertyListRef>(value));
    if (converted) {
      const std::string string = base::SysCFStringRefToUTF8(cf_key);
      static_cast<base::DictionaryValue *>(context)->Set(string, converted);
    }
  }
}

// Callback function for CFArrayApplyFunction. |value| is an entry of the
// CFArray that should be converted into an equivalent entry in the ListValue
// in |context|.
void ArrayEntryToValue(const void* value, void* context) {
  base::Value* converted =
      PolicyLoaderMac::CreateValueFromProperty(
          static_cast<CFPropertyListRef>(value));
  if (converted)
    static_cast<base::ListValue *>(context)->Append(converted);
}

}  // namespace

PolicyLoaderMac::PolicyLoaderMac(const PolicyDefinitionList* policy_list,
                                 MacPreferences* preferences)
    : policy_list_(policy_list),
      preferences_(preferences),
      managed_policy_path_(GetManagedPolicyPath()) {}

PolicyLoaderMac::~PolicyLoaderMac() {}

void PolicyLoaderMac::InitOnFile() {
  if (!managed_policy_path_.empty()) {
    watcher_.Watch(
        managed_policy_path_, false,
        base::Bind(&PolicyLoaderMac::OnFileUpdated, base::Unretained(this)));
  }
}

scoped_ptr<PolicyBundle> PolicyLoaderMac::Load() {
  preferences_->AppSynchronize(kCFPreferencesCurrentApplication);
  scoped_ptr<PolicyBundle> bundle(new PolicyBundle());

  // Load Chrome's policy.
  // TODO(joaodasilva): use a schema for Chrome once it's generated and
  // available from a PolicyDomainDescriptor.
  PolicyMap& chrome_policy =
      bundle->Get(PolicyNamespace(POLICY_DOMAIN_CHROME, std::string()));

  PolicyLoadStatusSample status;
  bool policy_present = false;
  const PolicyDefinitionList::Entry* current;
  for (current = policy_list_->begin; current != policy_list_->end; ++current) {
    base::ScopedCFTypeRef<CFStringRef> name(
        base::SysUTF8ToCFStringRef(current->name));
    base::ScopedCFTypeRef<CFPropertyListRef> value(
        preferences_->CopyAppValue(name, kCFPreferencesCurrentApplication));
    if (!value.get())
      continue;
    policy_present = true;
    bool forced =
        preferences_->AppValueIsForced(name, kCFPreferencesCurrentApplication);
    PolicyLevel level = forced ? POLICY_LEVEL_MANDATORY :
                                 POLICY_LEVEL_RECOMMENDED;
    // TODO(joaodasilva): figure the policy scope.
    base::Value* policy = CreateValueFromProperty(value);
    if (policy)
      chrome_policy.Set(current->name, level, POLICY_SCOPE_USER, policy, NULL);
    else
      status.Add(POLICY_LOAD_STATUS_PARSE_ERROR);
  }

  if (!policy_present)
    status.Add(POLICY_LOAD_STATUS_NO_POLICY);

  // Load policy for the registered components.
  static const struct {
    PolicyDomain domain;
    const char* domain_name;
  } kSupportedDomains[] = {
    { POLICY_DOMAIN_EXTENSIONS, "extensions" },
  };
  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(kSupportedDomains); ++i) {
    DescriptorMap::const_iterator it =
        descriptor_map().find(kSupportedDomains[i].domain);
    if (it != descriptor_map().end()) {
      LoadPolicyForDomain(
          it->second, kSupportedDomains[i].domain_name, bundle.get());
    }
  }

  return bundle.Pass();
}

base::Time PolicyLoaderMac::LastModificationTime() {
  base::PlatformFileInfo file_info;
  if (!file_util::GetFileInfo(managed_policy_path_, &file_info) ||
      file_info.is_directory) {
    return base::Time();
  }

  return file_info.last_modified;
}

// static
base::Value* PolicyLoaderMac::CreateValueFromProperty(
    CFPropertyListRef property) {
  if (CFCast<CFNullRef>(property))
    return base::Value::CreateNullValue();

  if (CFBooleanRef boolean = CFCast<CFBooleanRef>(property))
    return base::Value::CreateBooleanValue(CFBooleanGetValue(boolean));

  if (CFNumberRef number = CFCast<CFNumberRef>(property)) {
    // CFNumberGetValue() converts values implicitly when the conversion is not
    // lossy. Check the type before trying to convert.
    if (CFNumberIsFloatType(number)) {
      double double_value;
      if (CFNumberGetValue(number, kCFNumberDoubleType, &double_value))
        return base::Value::CreateDoubleValue(double_value);
    } else {
      int int_value;
      if (CFNumberGetValue(number, kCFNumberIntType, &int_value))
        return base::Value::CreateIntegerValue(int_value);
    }
  }

  if (CFStringRef string = CFCast<CFStringRef>(property))
    return base::Value::CreateStringValue(base::SysCFStringRefToUTF8(string));

  if (CFDictionaryRef dict = CFCast<CFDictionaryRef>(property)) {
    base::DictionaryValue* dict_value = new base::DictionaryValue();
    CFDictionaryApplyFunction(dict, DictionaryEntryToValue, dict_value);
    return dict_value;
  }

  if (CFArrayRef array = CFCast<CFArrayRef>(property)) {
    base::ListValue* list_value = new base::ListValue();
    CFArrayApplyFunction(array,
                         CFRangeMake(0, CFArrayGetCount(array)),
                         ArrayEntryToValue,
                         list_value);
    return list_value;
  }

  return NULL;
}

void PolicyLoaderMac::LoadPolicyForDomain(
    scoped_refptr<const PolicyDomainDescriptor> descriptor,
    const std::string& domain_name,
    PolicyBundle* bundle) {
  std::string id_prefix(base::mac::BaseBundleID());
  id_prefix.append(".").append(domain_name).append(".");

  for (PolicyDomainDescriptor::SchemaMap::const_iterator it_schema =
           descriptor->components().begin();
       it_schema != descriptor->components().end(); ++it_schema) {
    PolicyMap policy;
    LoadPolicyForComponent(
        id_prefix + it_schema->first, it_schema->second, &policy);
    if (!policy.empty()) {
      bundle->Get(PolicyNamespace(descriptor->domain(), it_schema->first))
          .Swap(&policy);
    }
  }
}

void PolicyLoaderMac::LoadPolicyForComponent(
    const std::string& bundle_id_string,
    const PolicySchema* schema,
    PolicyMap* policy) {
  // TODO(joaodasilva): extensions may be registered in a PolicyDomainDescriptor
  // without a PolicySchema, to allow a graceful update of the Legacy Browser
  // Support extension on Windows. Remove this temporary check once that support
  // is removed.
  if (!schema)
    return;

  base::ScopedCFTypeRef<CFStringRef> bundle_id(
      base::SysUTF8ToCFStringRef(bundle_id_string));
  preferences_->AppSynchronize(bundle_id);

  const PolicySchemaMap* map = schema->GetProperties();
  if (!map) {
    NOTREACHED();
    return;
  }

  for (PolicySchemaMap::const_iterator it = map->begin();
       it != map->end(); ++it) {
    base::ScopedCFTypeRef<CFStringRef> pref_name(
        base::SysUTF8ToCFStringRef(it->first));
    base::ScopedCFTypeRef<CFPropertyListRef> value(
        preferences_->CopyAppValue(pref_name, bundle_id));
    if (!value.get())
      continue;
    bool forced =
        preferences_->AppValueIsForced(pref_name, bundle_id);
    PolicyLevel level = forced ? POLICY_LEVEL_MANDATORY :
                                 POLICY_LEVEL_RECOMMENDED;
    scoped_ptr<base::Value> policy_value(CreateValueFromProperty(value));
    if (policy_value)
      policy->Set(it->first, level, POLICY_SCOPE_USER,
                  policy_value.release(), NULL);
  }
}

void PolicyLoaderMac::OnFileUpdated(const base::FilePath& path, bool error) {
  if (!error)
    Reload(false);
}

}  // namespace policy
