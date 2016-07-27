// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/crypto/proof_source_chromium.h"

using std::string;
using std::vector;

namespace net {

ProofSourceChromium::ProofSourceChromium() {}

ProofSourceChromium::~ProofSourceChromium() {}

bool ProofSourceChromium::Initialize(const base::FilePath& cert_path,
                                     const base::FilePath& key_path,
                                     const base::FilePath& sct_path) {
  return false;
}

bool ProofSourceChromium::GetProof(const IPAddressNumber& server_ip,
                                   const string& hostname,
                                   const string& server_config,
                                   bool ecdsa_ok,
                                   const vector<string>** out_certs,
                                   string* out_signature,
                                   string* out_leaf_cert_sct) {
  return false;
}

}  // namespace net
