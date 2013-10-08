// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_CRYPTO_MODULE_PASSWORD_DIALOG_H_
#define CHROME_BROWSER_UI_CRYPTO_MODULE_PASSWORD_DIALOG_H_

#include <string>
#include <vector>

#include "base/callback.h"
#include "base/memory/ref_counted.h"

namespace crypto {
class CryptoModuleBlockingPasswordDelegate;
}

namespace net {
class CryptoModule;
typedef std::vector<scoped_refptr<CryptoModule> > CryptoModuleList;
class X509Certificate;
}

namespace chrome {

// An enum to describe the reason for the password request.
enum CryptoModulePasswordReason {
  kCryptoModulePasswordKeygen,
  kCryptoModulePasswordCertEnrollment,
  kCryptoModulePasswordClientAuth,
  kCryptoModulePasswordListCerts,
  kCryptoModulePasswordCertImport,
  kCryptoModulePasswordCertExport,
};

typedef base::Callback<void(const char*)> CryptoModulePasswordCallback;

// Display a dialog, prompting the user to authenticate to unlock
// |module|. |reason| describes the purpose of the authentication and
// affects the message displayed in the dialog. |server| is the name
// of the server which requested the access.
void ShowCryptoModulePasswordDialog(
    const std::string& module_name,
    bool retry,
    CryptoModulePasswordReason reason,
    const std::string& server,
    const CryptoModulePasswordCallback& callback);

// Returns a CryptoModuleBlockingPasswordDelegate to open a dialog and block
// until returning. Should only be used on a worker thread.
crypto::CryptoModuleBlockingPasswordDelegate*
    NewCryptoModuleBlockingDialogDelegate(CryptoModulePasswordReason reason,
                                          const std::string& server);

// Asynchronously unlock |modules|, if necessary.  |callback| is called when
// done (regardless if any modules were successfully unlocked or not).  Should
// only be called on UI thread.
void UnlockSlotsIfNecessary(const net::CryptoModuleList& modules,
                            CryptoModulePasswordReason reason,
                            const std::string& server,
                            const base::Closure& callback);

// Asynchronously unlock the |cert|'s module, if necessary.  |callback| is
// called when done (regardless if module was successfully unlocked or not).
// Should only be called on UI thread.
void UnlockCertSlotIfNecessary(net::X509Certificate* cert,
                               CryptoModulePasswordReason reason,
                               const std::string& server,
                               const base::Closure& callback);

}  // namespace chrome

#endif  // CHROME_BROWSER_UI_CRYPTO_MODULE_PASSWORD_DIALOG_H_
