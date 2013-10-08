// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_AUTOFILL_DATA_MODEL_WRAPPER_H_
#define CHROME_BROWSER_UI_AUTOFILL_DATA_MODEL_WRAPPER_H_

#include "base/compiler_specific.h"
#include "base/strings/string16.h"
#include "chrome/browser/ui/autofill/autofill_dialog_types.h"
#include "components/autofill/content/browser/wallet/wallet_items.h"

namespace gfx {
class Image;
}

namespace autofill {

class AutofillDataModel;
class AutofillProfile;
class AutofillType;
class CreditCard;
class FormStructure;

namespace wallet {
class Address;
class FullWallet;
}

// A glue class that allows uniform interactions with autocomplete data sources,
// regardless of their type. Implementations are intended to be lightweight and
// copyable, only holding weak references to their backing model.
class DataModelWrapper {
 public:
  virtual ~DataModelWrapper();

  // Returns the data for a specific autocomplete type.
  virtual base::string16 GetInfo(const AutofillType& type) const = 0;

  // Returns the icon, if any, that represents this model.
  virtual gfx::Image GetIcon();

  // Fills in |inputs| with the data that this model contains (|inputs| is an
  // out-param).
  virtual void FillInputs(DetailInputs* inputs);

  // Gets text to display to the user to summarize this data source. The
  // default implementation assumes this is an address. Both params are required
  // to be non-NULL and will be filled in with text that is vertically compact
  // (but may take up a lot of horizontal space) and horizontally compact (but
  // may take up a lot of vertical space) respectively. The return value will
  // be true and the outparams will be filled in only if the data represented is
  // complete and valid.
  virtual bool GetDisplayText(base::string16* vertically_compact,
                              base::string16* horizontally_compact);

  // Fills in |form_structure| with the data that this model contains. |inputs|
  // and |comparator| are used to determine whether each field in the
  // FormStructure should be filled in or left alone. Returns whether any fields
  // in |form_structure| were found to be matching.
  bool FillFormStructure(
      const DetailInputs& inputs,
      const InputFieldComparator& compare,
      FormStructure* form_structure) const;

 protected:
  DataModelWrapper();

  // Fills in |field| with data from the model.
  virtual void FillFormField(AutofillField* field) const;

 private:
  // Formats address data into a single string using |separator| between
  // fields.
  base::string16 GetAddressDisplayText(const base::string16& separator);

  DISALLOW_COPY_AND_ASSIGN(DataModelWrapper);
};

// A DataModelWrapper that does not hold data and does nothing when told to
// fill in a form.
class EmptyDataModelWrapper : public DataModelWrapper {
 public:
  EmptyDataModelWrapper();
  virtual ~EmptyDataModelWrapper();

  virtual base::string16 GetInfo(const AutofillType& type) const OVERRIDE;

 protected:
  virtual void FillFormField(AutofillField* field) const OVERRIDE;

  DISALLOW_COPY_AND_ASSIGN(EmptyDataModelWrapper);
};

// A DataModelWrapper for Autofill data.
class AutofillDataModelWrapper : public DataModelWrapper {
 public:
  AutofillDataModelWrapper(const AutofillDataModel* data_model, size_t variant);
  virtual ~AutofillDataModelWrapper();

  virtual base::string16 GetInfo(const AutofillType& type) const OVERRIDE;

 protected:
  virtual void FillFormField(AutofillField* field) const OVERRIDE;

  size_t variant() const { return variant_; }

 private:
  const AutofillDataModel* data_model_;
  const size_t variant_;

  DISALLOW_COPY_AND_ASSIGN(AutofillDataModelWrapper);
};

// A DataModelWrapper for Autofill profiles.
class AutofillProfileWrapper : public AutofillDataModelWrapper {
 public:
  AutofillProfileWrapper(const AutofillProfile* profile, size_t variant);
  virtual ~AutofillProfileWrapper();

 protected:
  virtual void FillInputs(DetailInputs* inputs) OVERRIDE;
  virtual void FillFormField(AutofillField* field) const OVERRIDE;

 private:
  const AutofillProfile* profile_;

  DISALLOW_COPY_AND_ASSIGN(AutofillProfileWrapper);
};

// A DataModelWrapper specifically for Autofill CreditCard data.
class AutofillCreditCardWrapper : public AutofillDataModelWrapper {
 public:
  explicit AutofillCreditCardWrapper(const CreditCard* card);
  virtual ~AutofillCreditCardWrapper();

  virtual base::string16 GetInfo(const AutofillType& type) const OVERRIDE;
  virtual gfx::Image GetIcon() OVERRIDE;
  virtual bool GetDisplayText(base::string16* vertically_compact,
                              base::string16* horizontally_compact) OVERRIDE;

 private:
  const CreditCard* card_;

  DISALLOW_COPY_AND_ASSIGN(AutofillCreditCardWrapper);
};

// A DataModelWrapper for Wallet addresses.
class WalletAddressWrapper : public DataModelWrapper {
 public:
  explicit WalletAddressWrapper(const wallet::Address* address);
  virtual ~WalletAddressWrapper();

  virtual base::string16 GetInfo(const AutofillType& type) const OVERRIDE;
  virtual bool GetDisplayText(base::string16* vertically_compact,
                              base::string16* horizontally_compact) OVERRIDE;

 private:
  const wallet::Address* address_;

  DISALLOW_COPY_AND_ASSIGN(WalletAddressWrapper);
};

// A DataModelWrapper for Wallet instruments.
class WalletInstrumentWrapper : public DataModelWrapper {
 public:
  explicit WalletInstrumentWrapper(
      const wallet::WalletItems::MaskedInstrument* instrument);
  virtual ~WalletInstrumentWrapper();

  virtual base::string16 GetInfo(const AutofillType& type) const OVERRIDE;
  virtual gfx::Image GetIcon() OVERRIDE;
  virtual bool GetDisplayText(base::string16* vertically_compact,
                              base::string16* horizontally_compact) OVERRIDE;

 private:
  const wallet::WalletItems::MaskedInstrument* instrument_;

  DISALLOW_COPY_AND_ASSIGN(WalletInstrumentWrapper);
};

// A DataModelWrapper for FullWallet billing data.
class FullWalletBillingWrapper : public DataModelWrapper {
 public:
  explicit FullWalletBillingWrapper(wallet::FullWallet* full_wallet);
  virtual ~FullWalletBillingWrapper();

  virtual base::string16 GetInfo(const AutofillType& type) const OVERRIDE;
  virtual bool GetDisplayText(base::string16* vertically_compact,
                              base::string16* horizontally_compact) OVERRIDE;

 private:
  wallet::FullWallet* full_wallet_;

  DISALLOW_COPY_AND_ASSIGN(FullWalletBillingWrapper);
};

// A DataModelWrapper for FullWallet shipping data.
class FullWalletShippingWrapper : public DataModelWrapper {
 public:
  explicit FullWalletShippingWrapper(wallet::FullWallet* full_wallet);
  virtual ~FullWalletShippingWrapper();

  virtual base::string16 GetInfo(const AutofillType& type) const OVERRIDE;

 private:
  wallet::FullWallet* full_wallet_;

  DISALLOW_COPY_AND_ASSIGN(FullWalletShippingWrapper);
};

// A DataModelWrapper to copy the output of one section to the input of another.
class DetailOutputWrapper : public DataModelWrapper {
 public:
  explicit DetailOutputWrapper(const DetailOutputMap& outputs);
  virtual ~DetailOutputWrapper();

  virtual base::string16 GetInfo(const AutofillType& type) const OVERRIDE;

 private:
  const DetailOutputMap& outputs_;

  DISALLOW_COPY_AND_ASSIGN(DetailOutputWrapper);
};

}  // namespace autofill

#endif  // CHROME_BROWSER_UI_AUTOFILL_DATA_MODEL_WRAPPER_H_
