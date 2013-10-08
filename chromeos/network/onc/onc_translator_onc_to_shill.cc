// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The implementation of TranslateONCObjectToShill is structured in two parts:
// - The recursion through the existing ONC hierarchy
//     see TranslateONCHierarchy
// - The local translation of an object depending on the associated signature
//     see LocalTranslator::TranslateFields

#include "chromeos/network/onc/onc_translator.h"

#include <string>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/values.h"
#include "chromeos/network/onc/onc_constants.h"
#include "chromeos/network/onc/onc_signature.h"
#include "chromeos/network/onc/onc_translation_tables.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace chromeos {
namespace onc {

namespace {

scoped_ptr<base::StringValue> ConvertValueToString(const base::Value& value) {
  std::string str;
  if (!value.GetAsString(&str))
    base::JSONWriter::Write(&value, &str);
  return make_scoped_ptr(base::Value::CreateStringValue(str));
}

// This class is responsible to translate the local fields of the given
// |onc_object| according to |onc_signature| into |shill_dictionary|. This
// translation should consider (if possible) only fields of this ONC object and
// not nested objects because recursion is handled by the calling function
// TranslateONCHierarchy.
class LocalTranslator {
 public:
  LocalTranslator(const OncValueSignature& onc_signature,
                  const base::DictionaryValue& onc_object,
                  base::DictionaryValue* shill_dictionary)
      : onc_signature_(&onc_signature),
        onc_object_(&onc_object),
        shill_dictionary_(shill_dictionary) {
    field_translation_table_ = GetFieldTranslationTable(onc_signature);
  }

  void TranslateFields();

 private:
  void TranslateOpenVPN();
  void TranslateVPN();
  void TranslateWiFi();
  void TranslateEAP();
  void TranslateNetworkConfiguration();

  // Copies all entries from |onc_object_| to |shill_dictionary_| for which a
  // translation (shill_property_name) is defined by |onc_signature_|.
  void CopyFieldsAccordingToSignature();

  // Adds |value| to |shill_dictionary| at the field shill_property_name given
  // by the associated signature. Takes ownership of |value|. Does nothing if
  // |value| is NULL or the property name cannot be read from the signature.
  void AddValueAccordingToSignature(const std::string& onc_field_name,
                                    scoped_ptr<base::Value> value);

  // If existent, translates the entry at |onc_field_name| in |onc_object_|
  // using |table|. It is an error if no matching table entry is found. Writes
  // the result as entry at |shill_property_name| in |shill_dictionary_|.
  void TranslateWithTableAndSet(const std::string& onc_field_name,
                                const StringTranslationEntry table[],
                                const std::string& shill_property_name);

  const OncValueSignature* onc_signature_;
  const FieldTranslationEntry* field_translation_table_;
  const base::DictionaryValue* onc_object_;
  base::DictionaryValue* shill_dictionary_;

  DISALLOW_COPY_AND_ASSIGN(LocalTranslator);
};

void LocalTranslator::TranslateFields() {
  if (onc_signature_ == &kNetworkConfigurationSignature)
    TranslateNetworkConfiguration();
  else if (onc_signature_ == &kVPNSignature)
    TranslateVPN();
  else if (onc_signature_ == &kOpenVPNSignature)
    TranslateOpenVPN();
  else if (onc_signature_ == &kWiFiSignature)
    TranslateWiFi();
  else if (onc_signature_ == &kEAPSignature)
    TranslateEAP();
  else
    CopyFieldsAccordingToSignature();
}

void LocalTranslator::TranslateOpenVPN() {
  // Shill supports only one RemoteCertKU but ONC a list.
  // Copy only the first entry if existing.
  const base::ListValue* certKUs = NULL;
  std::string certKU;
  if (onc_object_->GetListWithoutPathExpansion(openvpn::kRemoteCertKU,
                                               &certKUs) &&
      certKUs->GetString(0, &certKU)) {
    shill_dictionary_->SetStringWithoutPathExpansion(
        flimflam::kOpenVPNRemoteCertKUProperty, certKU);
  }

  for (base::DictionaryValue::Iterator it(*onc_object_); !it.IsAtEnd();
       it.Advance()) {
    scoped_ptr<base::Value> translated;
    if (it.key() == vpn::kSaveCredentials ||
        it.key() == openvpn::kRemoteCertKU ||
        it.key() == openvpn::kServerCAPEMs) {
      translated.reset(it.value().DeepCopy());
    } else {
      // Shill wants all Provider/VPN fields to be strings.
      translated = ConvertValueToString(it.value());
    }
    AddValueAccordingToSignature(it.key(), translated.Pass());
  }
}

void LocalTranslator::TranslateVPN() {
  std::string type;
  onc_object_->GetStringWithoutPathExpansion(vpn::kType, &type);
  TranslateWithTableAndSet(type, kVPNTypeTable,
                           flimflam::kProviderTypeProperty);

  CopyFieldsAccordingToSignature();
}

void LocalTranslator::TranslateWiFi() {
  std::string security;
  onc_object_->GetStringWithoutPathExpansion(wifi::kSecurity, &security);
  TranslateWithTableAndSet(security, kWiFiSecurityTable,
                           flimflam::kSecurityProperty);

  // We currently only support managed and no adhoc networks.
  shill_dictionary_->SetStringWithoutPathExpansion(flimflam::kModeProperty,
                                                   flimflam::kModeManaged);
  CopyFieldsAccordingToSignature();
}

void LocalTranslator::TranslateEAP() {
  std::string outer;
  onc_object_->GetStringWithoutPathExpansion(eap::kOuter, &outer);
  TranslateWithTableAndSet(outer, kEAPOuterTable, flimflam::kEapMethodProperty);

  // Translate the inner protocol only for outer tunneling protocols.
  if (outer == eap::kPEAP || outer == eap::kEAP_TTLS) {
    // In ONC the Inner protocol defaults to "Automatic".
    std::string inner = eap::kAutomatic;
    // ONC's Inner == "Automatic" translates to omitting the Phase2 property in
    // Shill.
    onc_object_->GetStringWithoutPathExpansion(eap::kInner, &inner);
    if (inner != eap::kAutomatic) {
      const StringTranslationEntry* table =
          outer == eap::kPEAP ? kEAP_PEAP_InnerTable : kEAP_TTLS_InnerTable;
      TranslateWithTableAndSet(inner, table, flimflam::kEapPhase2AuthProperty);
    }
  }

  CopyFieldsAccordingToSignature();
}

void LocalTranslator::TranslateNetworkConfiguration() {
  std::string type;
  onc_object_->GetStringWithoutPathExpansion(network_config::kType, &type);
  TranslateWithTableAndSet(type, kNetworkTypeTable, flimflam::kTypeProperty);

  // Shill doesn't allow setting the name for non-VPN networks.
  if (type == network_type::kVPN) {
    std::string name;
    onc_object_->GetStringWithoutPathExpansion(network_config::kName, &name);
    shill_dictionary_->SetStringWithoutPathExpansion(
        flimflam::kNameProperty, name);
  }

  CopyFieldsAccordingToSignature();
}

void LocalTranslator::CopyFieldsAccordingToSignature() {
  for (base::DictionaryValue::Iterator it(*onc_object_); !it.IsAtEnd();
       it.Advance()) {
    AddValueAccordingToSignature(it.key(),
                                 make_scoped_ptr(it.value().DeepCopy()));
  }
}

void LocalTranslator::AddValueAccordingToSignature(
    const std::string& onc_name,
    scoped_ptr<base::Value> value) {
  if (!value || !field_translation_table_)
    return;

  std::string shill_property_name;
  if (!GetShillPropertyName(onc_name,
                            field_translation_table_,
                            &shill_property_name))
    return;

  shill_dictionary_->SetWithoutPathExpansion(shill_property_name,
                                             value.release());
}

void LocalTranslator::TranslateWithTableAndSet(
    const std::string& onc_value,
    const StringTranslationEntry table[],
    const std::string& shill_property_name) {
  std::string shill_value;
  if (TranslateStringToShill(table, onc_value, &shill_value)) {
    shill_dictionary_->SetStringWithoutPathExpansion(shill_property_name,
                                                     shill_value);
    return;
  }
  // As we previously validate ONC, this case should never occur. If it still
  // occurs, we should check here. Otherwise the failure will only show up much
  // later in Shill.
  LOG(ERROR) << "Value '" << onc_value
             << "' cannot be translated to Shill property "
             << shill_property_name;
}

// Iterates recursively over |onc_object| and its |signature|. At each object
// applies the local translation using LocalTranslator::TranslateFields. The
// results are written to |shill_dictionary|.
void TranslateONCHierarchy(const OncValueSignature& signature,
                           const base::DictionaryValue& onc_object,
                           base::DictionaryValue* shill_dictionary) {
  // Translates fields of |onc_object| and writes them to |shill_dictionary_|.
  LocalTranslator translator(signature, onc_object, shill_dictionary);
  translator.TranslateFields();

  // Recurse into nested objects.
  for (base::DictionaryValue::Iterator it(onc_object); !it.IsAtEnd();
       it.Advance()) {
    const base::DictionaryValue* inner_object = NULL;
    if (!it.value().GetAsDictionary(&inner_object))
      continue;

    const OncFieldSignature* field_signature =
        GetFieldSignature(signature, it.key());

    TranslateONCHierarchy(*field_signature->value_signature, *inner_object,
                          shill_dictionary);
  }
}

}  // namespace

scoped_ptr<base::DictionaryValue> TranslateONCObjectToShill(
    const OncValueSignature* onc_signature,
    const base::DictionaryValue& onc_object) {
  CHECK(onc_signature != NULL);
  scoped_ptr<base::DictionaryValue> shill_dictionary(new base::DictionaryValue);
  TranslateONCHierarchy(*onc_signature, onc_object, shill_dictionary.get());
  return shill_dictionary.Pass();
}

}  // namespace onc
}  // namespace chromeos
