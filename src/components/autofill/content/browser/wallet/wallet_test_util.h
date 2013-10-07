// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CONTENT_BROWSER_WALLET_WALLET_TEST_UTIL_H_
#define COMPONENTS_AUTOFILL_CONTENT_BROWSER_WALLET_WALLET_TEST_UTIL_H_

#include "base/memory/scoped_ptr.h"
#include "components/autofill/content/browser/wallet/wallet_items.h"

namespace autofill {
namespace wallet {

class Address;
class FullWallet;
class Instrument;

scoped_ptr<Address> GetTestAddress();
scoped_ptr<Address> GetTestMinimalAddress();
scoped_ptr<FullWallet> GetTestFullWallet();
scoped_ptr<FullWallet> GetTestFullWalletInstrumentOnly();
scoped_ptr<Instrument> GetTestInstrument();
scoped_ptr<Instrument> GetTestAddressUpgradeInstrument();
scoped_ptr<Instrument> GetTestExpirationDateChangeInstrument();
scoped_ptr<Instrument> GetTestAddressNameChangeInstrument();
scoped_ptr<WalletItems::LegalDocument> GetTestLegalDocument();
scoped_ptr<WalletItems::MaskedInstrument> GetTestMaskedInstrument();
scoped_ptr<WalletItems::MaskedInstrument> GetTestMaskedInstrumentExpired();
scoped_ptr<WalletItems::MaskedInstrument> GetTestMaskedInstrumentInvalid();
scoped_ptr<WalletItems::MaskedInstrument> GetTestMaskedInstrumentAmex();
scoped_ptr<WalletItems::MaskedInstrument> GetTestNonDefaultMaskedInstrument();
scoped_ptr<WalletItems::MaskedInstrument>
    GetTestMaskedInstrumentWithIdAndAddress(
        const std::string& id, scoped_ptr<Address> address);
scoped_ptr<Address> GetTestSaveableAddress();
scoped_ptr<Address> GetTestShippingAddress();
scoped_ptr<Address> GetTestNonDefaultShippingAddress();
scoped_ptr<WalletItems> GetTestWalletItems();

}  // namespace wallet
}  // namespace autofill

#endif  // COMPONENTS_AUTOFILL_CONTENT_BROWSER_WALLET_WALLET_TEST_UTIL_H_
