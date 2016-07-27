// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CRYPTO_PROOF_SOURCE_CHROMIUM_H_
#define NET_QUIC_CRYPTO_PROOF_SOURCE_CHROMIUM_H_

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/files/file_util.h"
#include "crypto/rsa_private_key.h"
#include "net/base/net_export.h"
#include "net/cert/x509_certificate.h"
#include "net/quic/crypto/proof_source.h"

namespace net {

// ProofSourceChromium implements the QUIC ProofSource interface.
// TODO(rtenneti): implement details of this class.
class NET_EXPORT_PRIVATE ProofSourceChromium : public ProofSource {
 public:
  ProofSourceChromium();
  ~ProofSourceChromium() override;

  // Initializes this object based on the certificate chain in |cert_path|,
  // and the PKCS#8 RSA private key in |key_path|. Signed certificate
  // timestamp may be loaded from |sct_path| if it is non-empty.
  bool Initialize(const base::FilePath& cert_path,
                  const base::FilePath& key_path,
                  const base::FilePath& sct_path);

  // ProofSource interface
  bool GetProof(const IPAddressNumber& server_ip,
                const std::string& hostname,
                const std::string& server_config,
                bool ecdsa_ok,
                const std::vector<std::string>** out_certs,
                std::string* out_signature,
                std::string* out_leaf_cert_sct) override;

 private:
  scoped_ptr<crypto::RSAPrivateKey> private_key_;
  std::vector<std::string> certificates_;
  std::string signed_certificate_timestamp_;

  DISALLOW_COPY_AND_ASSIGN(ProofSourceChromium);
};

}  // namespace net

#endif  // NET_QUIC_CRYPTO_PROOF_SOURCE_CHROMIUM_H_
