// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CONTENT_BROWSER_WALLET_WALLET_SERVICE_URL_H_
#define COMPONENTS_AUTOFILL_CONTENT_BROWSER_WALLET_WALLET_SERVICE_URL_H_

class GURL;

namespace autofill {
namespace wallet {

GURL GetGetWalletItemsUrl();
GURL GetGetFullWalletUrl();
GURL GetManageInstrumentsUrl();
GURL GetManageAddressesUrl();
GURL GetAcceptLegalDocumentsUrl();
GURL GetAuthenticateInstrumentUrl();
GURL GetSendStatusUrl();
GURL GetSaveToWalletNoEscrowUrl();
GURL GetSaveToWalletUrl();
GURL GetPassiveAuthUrl();

// URL to visit for presenting the user with a sign-in dialog.
GURL GetSignInUrl();

// The the URL to use as a continue parameter in the sign-in URL.
// A redirect to this URL will occur once sign-in is complete.
GURL GetSignInContinueUrl();

// Returns true if |url| is an acceptable variant of the sign-in continue
// url.  Can be used for detection of navigation to the continue url.
bool IsSignInContinueUrl(const GURL& url);

// Whether calls to Online Wallet are hitting the production server rather than
// a sandbox or some malicious endpoint.
bool IsUsingProd();

}  // namespace wallet
}  // namespace autofill

#endif  // COMPONENTS_AUTOFILL_CONTENT_BROWSER_WALLET_WALLET_SERVICE_URL_H_
