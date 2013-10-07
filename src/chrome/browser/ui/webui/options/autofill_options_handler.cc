// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/options/autofill_options_handler.h"

#include <vector>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/guid.h"
#include "base/logging.h"
#include "base/strings/string16.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/autofill/personal_data_manager_factory.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/autofill/country_combobox_model.h"
#include "chrome/common/url_constants.h"
#include "components/autofill/core/browser/autofill_country.h"
#include "components/autofill/core/browser/autofill_profile.h"
#include "components/autofill/core/browser/credit_card.h"
#include "components/autofill/core/browser/personal_data_manager.h"
#include "components/autofill/core/browser/phone_number_i18n.h"
#include "components/autofill/core/common/autofill_constants.h"
#include "content/public/browser/web_ui.h"
#include "grit/component_strings.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/webui/web_ui_util.h"

using autofill::AutofillCountry;
using autofill::ServerFieldType;
using autofill::AutofillProfile;
using autofill::CreditCard;
using autofill::PersonalDataManager;

namespace {

const char kSettingsOrigin[] = "Chrome settings";

// Sets data related to the country <select>.
void SetCountryData(DictionaryValue* localized_strings) {
  std::string default_country_code = AutofillCountry::CountryCodeForLocale(
      g_browser_process->GetApplicationLocale());
  localized_strings->SetString("defaultCountryCode", default_country_code);

  autofill::CountryComboboxModel model;
  const std::vector<AutofillCountry*>& countries = model.countries();

  // An ordered list of options to show in the <select>.
  scoped_ptr<ListValue> country_list(new ListValue());
  // A dictionary of postal code and state info, keyed on country code.
  scoped_ptr<DictionaryValue> country_data(new DictionaryValue());
  for (size_t i = 0; i < countries.size(); ++i) {
    scoped_ptr<DictionaryValue> option_details(new DictionaryValue());
    option_details->SetString("name", model.GetItemAt(i));
    option_details->SetString(
        "value",
        countries[i] ? countries[i]->country_code() : "separator");
    country_list->Append(option_details.release());

    if (!countries[i])
      continue;

    scoped_ptr<DictionaryValue> details(new DictionaryValue());
    details->SetString("postalCodeLabel", countries[i]->postal_code_label());
    details->SetString("stateLabel", countries[i]->state_label());
    country_data->Set(countries[i]->country_code(), details.release());

  }
  localized_strings->Set("autofillCountrySelectList", country_list.release());
  localized_strings->Set("autofillCountryData", country_data.release());
}

// Get the multi-valued element for |type| and return it in |ListValue| form.
void GetValueList(const AutofillProfile& profile,
                  ServerFieldType type,
                  scoped_ptr<ListValue>* list) {
  list->reset(new ListValue);

  std::vector<string16> values;
  profile.GetRawMultiInfo(type, &values);

  // |GetRawMultiInfo()| always returns at least one, potentially empty, item.
  if (values.size() == 1 && values.front().empty())
    return;

  for (size_t i = 0; i < values.size(); ++i) {
    (*list)->Set(i, new base::StringValue(values[i]));
  }
}

// Set the multi-valued element for |type| from input |list| values.
void SetValueList(const ListValue* list,
                  ServerFieldType type,
                  AutofillProfile* profile) {
  std::vector<string16> values(list->GetSize());
  for (size_t i = 0; i < list->GetSize(); ++i) {
    string16 value;
    if (list->GetString(i, &value))
      values[i] = value;
  }
  profile->SetRawMultiInfo(type, values);
}

// Get the multi-valued element for |type| and return it in |ListValue| form.
void GetNameList(const AutofillProfile& profile,
                 scoped_ptr<ListValue>* names) {
  names->reset(new ListValue);

  std::vector<string16> first_names;
  std::vector<string16> middle_names;
  std::vector<string16> last_names;
  profile.GetRawMultiInfo(autofill::NAME_FIRST, &first_names);
  profile.GetRawMultiInfo(autofill::NAME_MIDDLE, &middle_names);
  profile.GetRawMultiInfo(autofill::NAME_LAST, &last_names);
  DCHECK_EQ(first_names.size(), middle_names.size());
  DCHECK_EQ(first_names.size(), last_names.size());

  // |GetRawMultiInfo()| always returns at least one, potentially empty, item.
  if (first_names.size() == 1 && first_names.front().empty() &&
      middle_names.front().empty() && last_names.front().empty()) {
    return;
  }

  for (size_t i = 0; i < first_names.size(); ++i) {
    ListValue* name = new ListValue;  // owned by |list|
    name->Set(0, new base::StringValue(first_names[i]));
    name->Set(1, new base::StringValue(middle_names[i]));
    name->Set(2, new base::StringValue(last_names[i]));
    (*names)->Set(i, name);
  }
}

// Set the multi-valued element for |type| from input |list| values.
void SetNameList(const ListValue* names, AutofillProfile* profile) {
  const size_t size = names->GetSize();
  std::vector<string16> first_names(size);
  std::vector<string16> middle_names(size);
  std::vector<string16> last_names(size);

  for (size_t i = 0; i < size; ++i) {
    const ListValue* name;
    bool success = names->GetList(i, &name);
    DCHECK(success);

    string16 first_name;
    success = name->GetString(0, &first_name);
    DCHECK(success);
    first_names[i] = first_name;

    string16 middle_name;
    success = name->GetString(1, &middle_name);
    DCHECK(success);
    middle_names[i] = middle_name;

    string16 last_name;
    success = name->GetString(2, &last_name);
    DCHECK(success);
    last_names[i] = last_name;
  }

  profile->SetRawMultiInfo(autofill::NAME_FIRST, first_names);
  profile->SetRawMultiInfo(autofill::NAME_MIDDLE, middle_names);
  profile->SetRawMultiInfo(autofill::NAME_LAST, last_names);
}

// Pulls the phone number |index|, |phone_number_list|, and |country_code| from
// the |args| input.
void ExtractPhoneNumberInformation(const ListValue* args,
                                   size_t* index,
                                   const ListValue** phone_number_list,
                                   std::string* country_code) {
  // Retrieve index as a |double|, as that is how it comes across from
  // JavaScript.
  double number = 0.0;
  if (!args->GetDouble(0, &number)) {
    NOTREACHED();
    return;
  }
  *index = number;

  if (!args->GetList(1, phone_number_list)) {
    NOTREACHED();
    return;
  }

  if (!args->GetString(2, country_code)) {
    NOTREACHED();
    return;
  }
}

// Searches the |list| for the value at |index|.  If this value is present
// in any of the rest of the list, then the item (at |index|) is removed.
// The comparison of phone number values is done on normalized versions of the
// phone number values.
void RemoveDuplicatePhoneNumberAtIndex(size_t index,
                                       const std::string& country_code,
                                       ListValue* list) {
  string16 new_value;
  if (!list->GetString(index, &new_value)) {
    NOTREACHED() << "List should have a value at index " << index;
    return;
  }

  bool is_duplicate = false;
  std::string app_locale = g_browser_process->GetApplicationLocale();
  for (size_t i = 0; i < list->GetSize() && !is_duplicate; ++i) {
    if (i == index)
      continue;

    string16 existing_value;
    if (!list->GetString(i, &existing_value)) {
      NOTREACHED() << "List should have a value at index " << i;
      continue;
    }
    is_duplicate = autofill::i18n::PhoneNumbersMatch(
        new_value, existing_value, country_code, app_locale);
  }

  if (is_duplicate)
    list->Remove(index, NULL);
}

scoped_ptr<ListValue> ValidatePhoneArguments(const ListValue* args) {
  size_t index = 0;
  std::string country_code;
  const ListValue* extracted_list = NULL;
  ExtractPhoneNumberInformation(args, &index, &extracted_list, &country_code);

  scoped_ptr<ListValue> list(extracted_list->DeepCopy());
  RemoveDuplicatePhoneNumberAtIndex(index, country_code, list.get());
  return list.Pass();
}

}  // namespace

namespace options {

AutofillOptionsHandler::AutofillOptionsHandler()
    : personal_data_(NULL) {
}

AutofillOptionsHandler::~AutofillOptionsHandler() {
  if (personal_data_)
    personal_data_->RemoveObserver(this);
}

/////////////////////////////////////////////////////////////////////////////
// OptionsPageUIHandler implementation:
void AutofillOptionsHandler::GetLocalizedValues(
    DictionaryValue* localized_strings) {
  DCHECK(localized_strings);

  static OptionsStringResource resources[] = {
    { "autofillAddresses", IDS_AUTOFILL_ADDRESSES_GROUP_NAME },
    { "autofillCreditCards", IDS_AUTOFILL_CREDITCARDS_GROUP_NAME },
    { "autofillAddAddress", IDS_AUTOFILL_ADD_ADDRESS_BUTTON },
    { "autofillAddCreditCard", IDS_AUTOFILL_ADD_CREDITCARD_BUTTON },
    { "autofillEditProfileButton", IDS_AUTOFILL_EDIT_PROFILE_BUTTON },
    { "helpButton", IDS_AUTOFILL_HELP_LABEL },
    { "addAddressTitle", IDS_AUTOFILL_ADD_ADDRESS_CAPTION },
    { "editAddressTitle", IDS_AUTOFILL_EDIT_ADDRESS_CAPTION },
    { "addCreditCardTitle", IDS_AUTOFILL_ADD_CREDITCARD_CAPTION },
    { "editCreditCardTitle", IDS_AUTOFILL_EDIT_CREDITCARD_CAPTION },
#if defined(OS_MACOSX)
    { "auxiliaryProfilesEnabled", IDS_AUTOFILL_USE_MAC_ADDRESS_BOOK },
#endif  // defined(OS_MACOSX)
  };

  RegisterStrings(localized_strings, resources, arraysize(resources));
  RegisterTitle(localized_strings, "autofillOptionsPage",
                IDS_AUTOFILL_OPTIONS_TITLE);

  localized_strings->SetString("helpUrl", autofill::kHelpURL);
  SetAddressOverlayStrings(localized_strings);
  SetCreditCardOverlayStrings(localized_strings);
}

void AutofillOptionsHandler::InitializeHandler() {
  personal_data_ = autofill::PersonalDataManagerFactory::GetForProfile(
      Profile::FromWebUI(web_ui()));
  // personal_data_ is NULL in guest mode on Chrome OS.
  if (personal_data_)
    personal_data_->AddObserver(this);
}

void AutofillOptionsHandler::InitializePage() {
  if (personal_data_)
    LoadAutofillData();
}

void AutofillOptionsHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "removeData",
      base::Bind(&AutofillOptionsHandler::RemoveData,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "loadAddressEditor",
      base::Bind(&AutofillOptionsHandler::LoadAddressEditor,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "loadCreditCardEditor",
      base::Bind(&AutofillOptionsHandler::LoadCreditCardEditor,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setAddress",
      base::Bind(&AutofillOptionsHandler::SetAddress, base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setCreditCard",
      base::Bind(&AutofillOptionsHandler::SetCreditCard,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "validatePhoneNumbers",
      base::Bind(&AutofillOptionsHandler::ValidatePhoneNumbers,
                 base::Unretained(this)));
}

/////////////////////////////////////////////////////////////////////////////
// PersonalDataManagerObserver implementation:
void AutofillOptionsHandler::OnPersonalDataChanged() {
  LoadAutofillData();
}

void AutofillOptionsHandler::SetAddressOverlayStrings(
    DictionaryValue* localized_strings) {
  localized_strings->SetString("autofillEditAddressTitle",
      l10n_util::GetStringUTF16(IDS_AUTOFILL_EDIT_ADDRESS_CAPTION));
  localized_strings->SetString("autofillFirstNameLabel",
      l10n_util::GetStringUTF16(IDS_AUTOFILL_FIELD_LABEL_FIRST_NAME));
  localized_strings->SetString("autofillMiddleNameLabel",
      l10n_util::GetStringUTF16(IDS_AUTOFILL_FIELD_LABEL_MIDDLE_NAME));
  localized_strings->SetString("autofillLastNameLabel",
      l10n_util::GetStringUTF16(IDS_AUTOFILL_FIELD_LABEL_LAST_NAME));
  localized_strings->SetString("autofillCompanyNameLabel",
      l10n_util::GetStringUTF16(IDS_AUTOFILL_FIELD_LABEL_COMPANY_NAME));
  localized_strings->SetString("autofillAddrLine1Label",
      l10n_util::GetStringUTF16(IDS_AUTOFILL_FIELD_LABEL_ADDRESS_LINE_1));
  localized_strings->SetString("autofillAddrLine2Label",
      l10n_util::GetStringUTF16(IDS_AUTOFILL_FIELD_LABEL_ADDRESS_LINE_2));
  localized_strings->SetString("autofillCityLabel",
      l10n_util::GetStringUTF16(IDS_AUTOFILL_FIELD_LABEL_CITY));
  localized_strings->SetString("autofillCountryLabel",
      l10n_util::GetStringUTF16(IDS_AUTOFILL_FIELD_LABEL_COUNTRY));
  localized_strings->SetString("autofillPhoneLabel",
      l10n_util::GetStringUTF16(IDS_AUTOFILL_FIELD_LABEL_PHONE));
  localized_strings->SetString("autofillEmailLabel",
      l10n_util::GetStringUTF16(IDS_AUTOFILL_FIELD_LABEL_EMAIL));
  localized_strings->SetString("autofillAddFirstNamePlaceholder",
      l10n_util::GetStringUTF16(IDS_AUTOFILL_FIELD_LABEL_ADD_FIRST_NAME));
  localized_strings->SetString("autofillAddMiddleNamePlaceholder",
      l10n_util::GetStringUTF16(IDS_AUTOFILL_FIELD_LABEL_ADD_MIDDLE_NAME));
  localized_strings->SetString("autofillAddLastNamePlaceholder",
      l10n_util::GetStringUTF16(IDS_AUTOFILL_FIELD_LABEL_ADD_LAST_NAME));
  localized_strings->SetString("autofillAddPhonePlaceholder",
      l10n_util::GetStringUTF16(IDS_AUTOFILL_FIELD_LABEL_ADD_PHONE));
  localized_strings->SetString("autofillAddEmailPlaceholder",
      l10n_util::GetStringUTF16(IDS_AUTOFILL_FIELD_LABEL_ADD_EMAIL));
  SetCountryData(localized_strings);
}

void AutofillOptionsHandler::SetCreditCardOverlayStrings(
    DictionaryValue* localized_strings) {
  localized_strings->SetString("autofillEditCreditCardTitle",
      l10n_util::GetStringUTF16(IDS_AUTOFILL_EDIT_CREDITCARD_CAPTION));
  localized_strings->SetString("nameOnCardLabel",
      l10n_util::GetStringUTF16(IDS_AUTOFILL_FIELD_LABEL_NAME_ON_CARD));
  localized_strings->SetString("creditCardNumberLabel",
      l10n_util::GetStringUTF16(IDS_AUTOFILL_FIELD_LABEL_CREDIT_CARD_NUMBER));
  localized_strings->SetString("creditCardExpirationDateLabel",
      l10n_util::GetStringUTF16(IDS_AUTOFILL_FIELD_LABEL_EXPIRATION_DATE));
}

void AutofillOptionsHandler::LoadAutofillData() {
  if (!IsPersonalDataLoaded())
    return;

  ListValue addresses;
  for (std::vector<AutofillProfile*>::const_iterator i =
           personal_data_->web_profiles().begin();
       i != personal_data_->web_profiles().end(); ++i) {
    ListValue* entry = new ListValue();
    entry->Append(new StringValue((*i)->guid()));
    entry->Append(new StringValue((*i)->Label()));
    addresses.Append(entry);
  }

  web_ui()->CallJavascriptFunction("AutofillOptions.setAddressList", addresses);

  ListValue credit_cards;
  const std::vector<CreditCard*>& cards = personal_data_->GetCreditCards();
  for (std::vector<CreditCard*>::const_iterator iter = cards.begin();
       iter != cards.end(); ++iter) {
    const CreditCard* card = *iter;
    // TODO(estade): this should be a dictionary.
    ListValue* entry = new ListValue();
    entry->Append(new StringValue(card->guid()));
    entry->Append(new StringValue(card->Label()));
    entry->Append(new StringValue(
        webui::GetBitmapDataUrlFromResource(
            CreditCard::IconResourceId(card->type()))));
    entry->Append(new StringValue(card->TypeForDisplay()));
    credit_cards.Append(entry);
  }

  web_ui()->CallJavascriptFunction("AutofillOptions.setCreditCardList",
                                   credit_cards);
}

void AutofillOptionsHandler::RemoveData(const ListValue* args) {
  DCHECK(IsPersonalDataLoaded());

  std::string guid;
  if (!args->GetString(0, &guid)) {
    NOTREACHED();
    return;
  }

  personal_data_->RemoveByGUID(guid);
}

void AutofillOptionsHandler::LoadAddressEditor(const ListValue* args) {
  DCHECK(IsPersonalDataLoaded());

  std::string guid;
  if (!args->GetString(0, &guid)) {
    NOTREACHED();
    return;
  }

  AutofillProfile* profile = personal_data_->GetProfileByGUID(guid);
  if (!profile) {
    // There is a race where a user can click once on the close button and
    // quickly click again on the list item before the item is removed (since
    // the list is not updated until the model tells the list an item has been
    // removed). This will activate the editor for a profile that has been
    // removed. Do nothing in that case.
    return;
  }

  DictionaryValue address;
  address.SetString("guid", profile->guid());
  scoped_ptr<ListValue> list;
  GetNameList(*profile, &list);
  address.Set("fullName", list.release());
  address.SetString("companyName", profile->GetRawInfo(autofill::COMPANY_NAME));
  address.SetString("addrLine1",
                    profile->GetRawInfo(autofill::ADDRESS_HOME_LINE1));
  address.SetString("addrLine2",
                    profile->GetRawInfo(autofill::ADDRESS_HOME_LINE2));
  address.SetString("city", profile->GetRawInfo(autofill::ADDRESS_HOME_CITY));
  address.SetString("state", profile->GetRawInfo(autofill::ADDRESS_HOME_STATE));
  address.SetString("postalCode",
                    profile->GetRawInfo(autofill::ADDRESS_HOME_ZIP));
  address.SetString("country",
                    profile->GetRawInfo(autofill::ADDRESS_HOME_COUNTRY));
  GetValueList(*profile, autofill::PHONE_HOME_WHOLE_NUMBER, &list);
  address.Set("phone", list.release());
  GetValueList(*profile, autofill::EMAIL_ADDRESS, &list);
  address.Set("email", list.release());

  web_ui()->CallJavascriptFunction("AutofillOptions.editAddress", address);
}

void AutofillOptionsHandler::LoadCreditCardEditor(const ListValue* args) {
  DCHECK(IsPersonalDataLoaded());

  std::string guid;
  if (!args->GetString(0, &guid)) {
    NOTREACHED();
    return;
  }

  CreditCard* credit_card = personal_data_->GetCreditCardByGUID(guid);
  if (!credit_card) {
    // There is a race where a user can click once on the close button and
    // quickly click again on the list item before the item is removed (since
    // the list is not updated until the model tells the list an item has been
    // removed). This will activate the editor for a profile that has been
    // removed. Do nothing in that case.
    return;
  }

  DictionaryValue credit_card_data;
  credit_card_data.SetString("guid", credit_card->guid());
  credit_card_data.SetString(
      "nameOnCard",
      credit_card->GetRawInfo(autofill::CREDIT_CARD_NAME));
  credit_card_data.SetString(
      "creditCardNumber",
      credit_card->GetRawInfo(autofill::CREDIT_CARD_NUMBER));
  credit_card_data.SetString(
      "expirationMonth",
      credit_card->GetRawInfo(autofill::CREDIT_CARD_EXP_MONTH));
  credit_card_data.SetString(
      "expirationYear",
      credit_card->GetRawInfo(autofill::CREDIT_CARD_EXP_4_DIGIT_YEAR));

  web_ui()->CallJavascriptFunction("AutofillOptions.editCreditCard",
                                   credit_card_data);
}

void AutofillOptionsHandler::SetAddress(const ListValue* args) {
  if (!IsPersonalDataLoaded())
    return;

  std::string guid;
  if (!args->GetString(0, &guid)) {
    NOTREACHED();
    return;
  }

  AutofillProfile profile(guid, kSettingsOrigin);

  std::string country_code;
  string16 value;
  const ListValue* list_value;
  if (args->GetList(1, &list_value))
    SetNameList(list_value, &profile);

  if (args->GetString(2, &value))
    profile.SetRawInfo(autofill::COMPANY_NAME, value);

  if (args->GetString(3, &value))
    profile.SetRawInfo(autofill::ADDRESS_HOME_LINE1, value);

  if (args->GetString(4, &value))
    profile.SetRawInfo(autofill::ADDRESS_HOME_LINE2, value);

  if (args->GetString(5, &value))
    profile.SetRawInfo(autofill::ADDRESS_HOME_CITY, value);

  if (args->GetString(6, &value))
    profile.SetRawInfo(autofill::ADDRESS_HOME_STATE, value);

  if (args->GetString(7, &value))
    profile.SetRawInfo(autofill::ADDRESS_HOME_ZIP, value);

  if (args->GetString(8, &country_code))
    profile.SetRawInfo(autofill::ADDRESS_HOME_COUNTRY,
                       ASCIIToUTF16(country_code));

  if (args->GetList(9, &list_value))
    SetValueList(list_value, autofill::PHONE_HOME_WHOLE_NUMBER, &profile);

  if (args->GetList(10, &list_value))
    SetValueList(list_value, autofill::EMAIL_ADDRESS, &profile);

  if (!base::IsValidGUID(profile.guid())) {
    profile.set_guid(base::GenerateGUID());
    personal_data_->AddProfile(profile);
  } else {
    personal_data_->UpdateProfile(profile);
  }
}

void AutofillOptionsHandler::SetCreditCard(const ListValue* args) {
  if (!IsPersonalDataLoaded())
    return;

  std::string guid;
  if (!args->GetString(0, &guid)) {
    NOTREACHED();
    return;
  }

  CreditCard credit_card(guid, kSettingsOrigin);

  string16 value;
  if (args->GetString(1, &value))
    credit_card.SetRawInfo(autofill::CREDIT_CARD_NAME, value);

  if (args->GetString(2, &value))
    credit_card.SetRawInfo(autofill::CREDIT_CARD_NUMBER, value);

  if (args->GetString(3, &value))
    credit_card.SetRawInfo(autofill::CREDIT_CARD_EXP_MONTH, value);

  if (args->GetString(4, &value))
    credit_card.SetRawInfo(autofill::CREDIT_CARD_EXP_4_DIGIT_YEAR, value);

  if (!base::IsValidGUID(credit_card.guid())) {
    credit_card.set_guid(base::GenerateGUID());
    personal_data_->AddCreditCard(credit_card);
  } else {
    personal_data_->UpdateCreditCard(credit_card);
  }
}

void AutofillOptionsHandler::ValidatePhoneNumbers(const ListValue* args) {
  if (!IsPersonalDataLoaded())
    return;

  scoped_ptr<ListValue> list_value = ValidatePhoneArguments(args);

  web_ui()->CallJavascriptFunction(
    "AutofillEditAddressOverlay.setValidatedPhoneNumbers", *list_value);
}

bool AutofillOptionsHandler::IsPersonalDataLoaded() const {
  return personal_data_ && personal_data_->IsDataLoaded();
}

}  // namespace options
